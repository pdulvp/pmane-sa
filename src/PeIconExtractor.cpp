#include "PeIconExtractor.h"

#include <QFile>
#include <QImage>
#include <QMap>
#include <QVector>
#include <QtEndian>

#include <functional>

namespace {

constexpr quint16 RT_ICON = 3;
constexpr quint16 RT_GROUP_ICON = 14;

// Small bounds-checked little-endian reader over the raw file bytes.
struct Reader {
    const uchar *data;
    qint64 size;

    bool u16(qint64 off, quint16 &out) const {
        if (off < 0 || off + 2 > size) return false;
        out = quint16(data[off]) | (quint16(data[off + 1]) << 8);
        return true;
    }
    bool u32(qint64 off, quint32 &out) const {
        if (off < 0 || off + 4 > size) return false;
        out = quint32(data[off]) | (quint32(data[off + 1]) << 8) |
              (quint32(data[off + 2]) << 16) | (quint32(data[off + 3]) << 24);
        return true;
    }
    bool bytes(qint64 off, qint64 len) const {
        return off >= 0 && len >= 0 && off + len <= size;
    }
};

struct Section {
    quint32 virtualAddress;
    quint32 virtualSize;
    quint32 rawOffset;
    quint32 rawSize;
};

struct ResourceDirEntry {
    quint32 nameOrId;
    quint32 offset;
    bool isDirectory;
};

// Resolves a Relative Virtual Address to a raw file offset using the
// section table.
qint64 rvaToOffset(const QVector<Section> &sections, quint32 rva) {
    for (const Section &s : sections) {
        quint32 span = s.virtualSize ? s.virtualSize : s.rawSize;
        if (rva >= s.virtualAddress && rva < s.virtualAddress + span) {
            return qint64(s.rawOffset) + (rva - s.virtualAddress);
        }
    }
    return -1;
}

bool readDirEntries(const Reader &r, qint64 dirOffset, QVector<ResourceDirEntry> &out) {
    quint16 namedCount, idCount;
    if (!r.u16(dirOffset + 12, namedCount)) return false;
    if (!r.u16(dirOffset + 14, idCount)) return false;
    int total = namedCount + idCount;
    qint64 entryOffset = dirOffset + 16;
    for (int i = 0; i < total; ++i) {
        quint32 nameField, dataField;
        if (!r.u32(entryOffset, nameField)) return false;
        if (!r.u32(entryOffset + 4, dataField)) return false;
        ResourceDirEntry e;
        e.nameOrId = nameField & 0x7fffffff; // ignore string-named entries' content, just skip matching by id
        e.isDirectory = (dataField & 0x80000000) != 0;
        e.offset = dataField & 0x7fffffff;
        out.append(e);
        entryOffset += 8;
    }
    return true;
}

// Finds a numeric-id entry (not a named/string entry) within a resource
// directory at fileOffset.
bool findEntryById(const Reader &r, qint64 dirFileOffset, quint32 id, ResourceDirEntry &found) {
    QVector<ResourceDirEntry> entries;
    if (!readDirEntries(r, dirFileOffset, entries)) return false;
    for (const ResourceDirEntry &e : entries) {
        if (e.nameOrId == id) {
            found = e;
            return true;
        }
    }
    return false;
}

bool firstEntry(const Reader &r, qint64 dirFileOffset, ResourceDirEntry &found) {
    QVector<ResourceDirEntry> entries;
    if (!readDirEntries(r, dirFileOffset, entries)) return false;
    if (entries.isEmpty()) return false;
    found = entries.first();
    return true;
}

struct GrpIconEntry {
    quint8 width;
    quint8 height;
    quint16 bitCount;
    quint16 id;
};

// How much this desktop wants a given image: a classic 32x32 first (closest
// to the win3.1 look), otherwise the largest and deepest one on offer.
int iconScore(int width, int bitCount) {
    const int w = width ? width : 256;
    return (w == 32 ? 1000 : 0) + w * 10 + bitCount;
}

// Decodes a legacy ICO "raw" image (either a PNG blob, or an old-style
// DIB: BITMAPINFOHEADER + optional palette + XOR bitmap + AND mask) into
// a QImage with a proper alpha channel.
QImage decodeIconImage(const QByteArray &raw) {
    if (raw.size() >= 8 &&
        (uchar)raw[0] == 0x89 && raw[1] == 'P' && raw[2] == 'N' && raw[3] == 'G') {
        QImage img;
        img.loadFromData(raw, "PNG");
        return img;
    }

    const uchar *d = reinterpret_cast<const uchar *>(raw.constData());
    qint64 size = raw.size();
    Reader r{d, size};

    quint32 headerSize;
    if (!r.u32(0, headerSize)) return {};

    int width, fullHeight;
    quint16 bitCount;
    qint64 paletteOffset;
    int paletteEntrySize;

    if (headerSize == 12) {
        // Old OS/2-style BITMAPCOREHEADER, used by some Windows 2.x/3.0
        // era icons: 16-bit width/height, 3-byte (no padding) palette
        // entries.
        quint16 w16, h16, planes;
        if (!r.u16(4, w16)) return {};
        if (!r.u16(6, h16)) return {};
        if (!r.u16(8, planes)) return {};
        if (!r.u16(10, bitCount)) return {};
        width = w16;
        fullHeight = h16;
        paletteOffset = 12;
        paletteEntrySize = 3;
    } else {
        quint32 widthU, heightU;
        quint16 planes;
        if (!r.u32(4, widthU)) return {};
        if (!r.u32(8, heightU)) return {};
        if (!r.u16(12, planes)) return {};
        if (!r.u16(14, bitCount)) return {};
        width = int(widthU);
        fullHeight = int(heightU);
        paletteOffset = headerSize;
        paletteEntrySize = 4;
    }

    int height = fullHeight / 2; // XOR + AND mask combined
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return {};

    int numColors = 0;
    if (bitCount <= 8) {
        quint32 clrUsed = 0;
        if (headerSize != 12) r.u32(32, clrUsed);
        numColors = clrUsed ? int(clrUsed) : (1 << bitCount);
    }
    QVector<QRgb> palette;
    if (numColors > 0) {
        if (!r.bytes(paletteOffset, qint64(numColors) * paletteEntrySize)) return {};
        palette.reserve(numColors);
        for (int i = 0; i < numColors; ++i) {
            qint64 o = paletteOffset + i * paletteEntrySize;
            quint8 b = d[o], g = d[o + 1], red = d[o + 2];
            palette.append(qRgb(red, g, b));
        }
    }

    qint64 xorOffset = paletteOffset + qint64(numColors) * paletteEntrySize;
    qint64 xorRowBytes = ((qint64(width) * bitCount + 31) / 32) * 4;
    qint64 andRowBytes = ((qint64(width) + 31) / 32) * 4;
    qint64 xorSize = xorRowBytes * height;
    qint64 andOffset = xorOffset + xorSize;

    if (!r.bytes(xorOffset, xorSize)) return {};
    bool haveAnd = r.bytes(andOffset, andRowBytes * height);

    QImage img(width, height, QImage::Format_ARGB32);

    for (int fileRow = 0; fileRow < height; ++fileRow) {
        int y = height - 1 - fileRow; // DIB rows are stored bottom-up
        qint64 rowOff = xorOffset + fileRow * xorRowBytes;
        qint64 andRowOff = andOffset + fileRow * andRowBytes;
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));

        for (int x = 0; x < width; ++x) {
            QRgb color;
            quint8 alpha = 255;

            switch (bitCount) {
            case 1: {
                quint8 byte = d[rowOff + x / 8];
                int bit = (byte >> (7 - (x % 8))) & 1;
                color = bit < palette.size() ? palette[bit] : qRgb(0, 0, 0);
                break;
            }
            case 4: {
                quint8 byte = d[rowOff + x / 2];
                int idx = (x % 2 == 0) ? (byte >> 4) : (byte & 0x0f);
                color = idx < palette.size() ? palette[idx] : qRgb(0, 0, 0);
                break;
            }
            case 8: {
                quint8 idx = d[rowOff + x];
                color = idx < palette.size() ? palette[idx] : qRgb(0, 0, 0);
                break;
            }
            case 24: {
                qint64 o = rowOff + x * 3;
                color = qRgb(d[o + 2], d[o + 1], d[o]);
                break;
            }
            case 32: {
                qint64 o = rowOff + x * 4;
                alpha = d[o + 3];
                color = qRgb(d[o + 2], d[o + 1], d[o]);
                break;
            }
            default:
                color = qRgb(192, 192, 192);
                break;
            }

            if (bitCount != 32) {
                if (haveAnd) {
                    quint8 byte = d[andRowOff + x / 8];
                    int bit = (byte >> (7 - (x % 8))) & 1;
                    alpha = bit ? 0 : 255;
                } else {
                    alpha = 255;
                }
            } else if (alpha == 0) {
                // Some 32bpp icons still rely on the AND mask even though
                // they carry an (unused) alpha channel of all zeroes.
                if (haveAnd) {
                    quint8 byte = d[andRowOff + x / 8];
                    int bit = (byte >> (7 - (x % 8))) & 1;
                    alpha = bit ? 0 : 255;
                } else {
                    alpha = 255;
                }
            }

            line[x] = qRgba(qRed(color), qGreen(color), qBlue(color), alpha);
        }
    }

    return img;
}

// The GRPICONDIR layout, shared by PE and NE executables: a short header,
// then one 14-byte entry per image in the group.
QVector<GrpIconEntry> parseGroupDir(const QByteArray &blob) {
    QVector<GrpIconEntry> members;
    Reader r{reinterpret_cast<const uchar *>(blob.constData()), blob.size()};
    quint16 idCount;
    if (!r.u16(4, idCount)) return members;
    for (int i = 0; i < idCount; ++i) {
        qint64 o = 6 + qint64(i) * 14;
        if (!r.bytes(o, 14)) break;
        GrpIconEntry e;
        e.width = quint8(blob[o]);
        e.height = quint8(blob[o + 1]);
        r.u16(o + 6, e.bitCount);
        r.u16(o + 12, e.id);
        members.append(e);
    }
    return members;
}

// Which image of a group this desktop would rather show.
GrpIconEntry bestMember(const QVector<GrpIconEntry> &members) {
    GrpIconEntry best = members.first();
    for (const GrpIconEntry &e : members) {
        if (iconScore(e.width, e.bitCount) > iconScore(best.width, best.bitCount)) best = e;
    }
    return best;
}

// The resource directory of a modern PE32/PE32+ executable, resolved down
// to raw file offsets.
struct PeResTable {
    QVector<Section> sections;
    quint32 resourceRva = 0;
    qint64 baseOffset = -1;

    bool valid() const { return baseOffset >= 0; }
    qint64 toFile(quint32 relOffset) const { return rvaToOffset(sections, resourceRva + relOffset); }
};

PeResTable parsePeResources(const Reader &r, quint32 peOffset) {
    PeResTable t;

    qint64 coffOffset = peOffset + 4;
    quint16 numSections, sizeOfOptionalHeader;
    if (!r.u16(coffOffset + 2, numSections)) return t;
    if (!r.u16(coffOffset + 16, sizeOfOptionalHeader)) return t;

    qint64 optHeaderOffset = coffOffset + 20;
    quint16 magic;
    if (!r.u16(optHeaderOffset, magic)) return t;

    qint64 dataDirOffset;
    if (magic == 0x10b) { // PE32
        dataDirOffset = optHeaderOffset + 96;
    } else if (magic == 0x20b) { // PE32+
        dataDirOffset = optHeaderOffset + 112;
    } else {
        return t;
    }

    // Entry 2 of the data directory is the resource table.
    quint32 resourceRva, resourceSize;
    if (!r.u32(dataDirOffset + 2 * 8, resourceRva)) return t;
    if (!r.u32(dataDirOffset + 2 * 8 + 4, resourceSize)) return t;
    if (resourceRva == 0 || resourceSize == 0) return t;

    qint64 sectionTableOffset = optHeaderOffset + sizeOfOptionalHeader;
    for (int i = 0; i < numSections; ++i) {
        qint64 o = sectionTableOffset + i * 40;
        quint32 virtualSize, virtualAddress, rawSize, rawOffset;
        if (!r.u32(o + 8, virtualSize)) return t;
        if (!r.u32(o + 12, virtualAddress)) return t;
        if (!r.u32(o + 16, rawSize)) return t;
        if (!r.u32(o + 20, rawOffset)) return t;
        t.sections.append({virtualAddress, virtualSize, rawOffset, rawSize});
    }

    t.resourceRva = resourceRva;
    t.baseOffset = rvaToOffset(t.sections, resourceRva);
    return t;
}

// The bytes of a resource, given its level-2 entry: down through the
// language directory to the leaf that points at the payload.
QByteArray peLeafData(const Reader &r, const uchar *d, const PeResTable &t,
                      const ResourceDirEntry &res) {
    if (!res.isDirectory) return {};
    qint64 langDirOffset = t.toFile(res.offset);
    if (langDirOffset < 0) return {};
    ResourceDirEntry leaf;
    if (!firstEntry(r, langDirOffset, leaf) || leaf.isDirectory) return {};

    qint64 dataEntryOffset = t.toFile(leaf.offset);
    if (dataEntryOffset < 0) return {};
    quint32 dataRva, dataSize;
    if (!r.u32(dataEntryOffset, dataRva)) return {};
    if (!r.u32(dataEntryOffset + 4, dataSize)) return {};

    qint64 dataOffset = rvaToOffset(t.sections, dataRva);
    if (dataOffset < 0 || !r.bytes(dataOffset, dataSize)) return {};
    return QByteArray(reinterpret_cast<const char *>(d + dataOffset), int(dataSize));
}

// The icon groups a PE holds, one per logical icon.
QVector<ResourceDirEntry> peIconGroups(const Reader &r, const PeResTable &t) {
    QVector<ResourceDirEntry> groups;

    ResourceDirEntry groupIconType;
    if (!findEntryById(r, t.baseOffset, RT_GROUP_ICON, groupIconType) ||
        !groupIconType.isDirectory) {
        return groups;
    }
    qint64 groupTypeDirOffset = t.toFile(groupIconType.offset);
    if (groupTypeDirOffset < 0) return groups;

    QVector<ResourceDirEntry> entries;
    if (!readDirEntries(r, groupTypeDirOffset, entries)) return groups;
    for (const ResourceDirEntry &e : entries) {
        if (e.isDirectory) groups.append(e);
    }
    return groups;
}

QByteArray peIconData(const Reader &r, const uchar *d, const PeResTable &t, quint16 id) {
    ResourceDirEntry iconType;
    if (!findEntryById(r, t.baseOffset, RT_ICON, iconType) || !iconType.isDirectory) return {};
    qint64 iconTypeDirOffset = t.toFile(iconType.offset);
    if (iconTypeDirOffset < 0) return {};
    ResourceDirEntry iconRes;
    if (!findEntryById(r, iconTypeDirOffset, id, iconRes)) return {};
    return peLeafData(r, d, t, iconRes);
}

// A resource located inside an NE (16-bit "New Executable") resource table.
struct NeResource {
    quint16 id; // numeric id, already masked (high bit stripped)
    qint64 offset;
    qint64 length;
};

// Parses the NE resource table into a type-id -> resource list map. Only
// numeric (non-string) type and resource ids are collected, which covers
// RT_ICON/RT_GROUP_ICON in every executable we have ever seen in the wild.
QMap<quint16, QVector<NeResource>> parseNeResourceTable(const Reader &r, qint64 neHeaderOffset) {
    QMap<quint16, QVector<NeResource>> result;

    quint16 rsrcTabRelOffset;
    if (!r.u16(neHeaderOffset + 0x24, rsrcTabRelOffset)) return result;
    qint64 tableOffset = neHeaderOffset + rsrcTabRelOffset;

    quint16 alignShift;
    if (!r.u16(tableOffset, alignShift)) return result;

    qint64 cursor = tableOffset + 2;
    for (;;) {
        quint16 rawTypeId;
        if (!r.u16(cursor, rawTypeId)) break;
        if (rawTypeId == 0) break; // terminator

        quint16 resourceCount;
        if (!r.u16(cursor + 2, resourceCount)) break;
        cursor += 8; // rtTypeID(2) + rtResourceCount(2) + rtReserved(4)

        bool typeIsNumeric = (rawTypeId & 0x8000) != 0;
        quint16 typeId = rawTypeId & 0x7fff;

        for (int i = 0; i < resourceCount; ++i) {
            quint16 rnOffset, rnLength, rnFlags, rnId;
            if (!r.u16(cursor, rnOffset)) return result;
            if (!r.u16(cursor + 2, rnLength)) return result;
            if (!r.u16(cursor + 4, rnFlags)) return result;
            if (!r.u16(cursor + 6, rnId)) return result;
            cursor += 12; // + rnHandle(2) + rnUsage(2)

            if (typeIsNumeric && (rnId & 0x8000)) {
                NeResource res;
                res.id = rnId & 0x7fff;
                res.offset = qint64(rnOffset) << alignShift;
                res.length = qint64(rnLength) << alignShift;
                result[typeId].append(res);
            }
        }
    }
    return result;
}

// One icon a file offers, with its payload left unread until somebody
// actually wants to decode it: listing the icons of an executable that
// embeds dozens should not decode all of them.
struct IconSlot {
    int index = 0;
    int width = 0;    // 0 when the container does not say
    int bitCount = 0;
    std::function<QByteArray()> payload;
};

// Returning false stops the walk.
using SlotVisitor = std::function<bool(const IconSlot &)>;

void walkPeIcons(const Reader &r, const uchar *d, quint32 peOffset, const SlotVisitor &visit) {
    const PeResTable t = parsePeResources(r, peOffset);
    if (!t.valid()) return;

    const QVector<ResourceDirEntry> groups = peIconGroups(r, t);
    for (int i = 0; i < groups.size(); ++i) {
        const QVector<GrpIconEntry> members = parseGroupDir(peLeafData(r, d, t, groups[i]));
        if (members.isEmpty()) continue;
        const GrpIconEntry best = bestMember(members);

        IconSlot slot;
        slot.index = i;
        slot.width = best.width;
        slot.bitCount = best.bitCount;
        slot.payload = [&r, d, &t, best] { return peIconData(r, d, t, best.id); };
        if (!visit(slot)) return;
    }
}

void walkNeIcons(const Reader &r, const uchar *d, qint64 neHeaderOffset, const SlotVisitor &visit) {
    const QMap<quint16, QVector<NeResource>> resources = parseNeResourceTable(r, neHeaderOffset);
    const QVector<NeResource> iconList = resources.value(RT_ICON);
    if (iconList.isEmpty()) return;

    auto readRaw = [&r, d](const NeResource &res) -> QByteArray {
        if (!r.bytes(res.offset, res.length)) return {};
        return QByteArray(reinterpret_cast<const char *>(d + res.offset), int(res.length));
    };

    int listed = 0;
    for (const NeResource &group : resources.value(RT_GROUP_ICON)) {
        const QVector<GrpIconEntry> members = parseGroupDir(readRaw(group));
        if (members.isEmpty()) continue;
        const GrpIconEntry best = bestMember(members);

        for (const NeResource &icon : iconList) {
            if (icon.id != best.id) continue;
            IconSlot slot;
            slot.index = listed;
            slot.width = best.width;
            slot.bitCount = best.bitCount;
            slot.payload = [&readRaw, icon] { return readRaw(icon); };
            ++listed;
            if (!visit(slot)) return;
            break;
        }
    }
    if (listed > 0) return;

    // No usable group-icon resource: the individual icon resources are all
    // there is to offer, so they are listed one by one instead.
    for (int i = 0; i < iconList.size(); ++i) {
        const NeResource &icon = iconList[i];
        IconSlot slot;
        slot.index = i;
        slot.payload = [&readRaw, icon] { return readRaw(icon); };
        if (!visit(slot)) return;
    }
}

void walkIcoIcons(const Reader &r, const uchar *d, const SlotVisitor &visit) {
    quint16 reserved, type, count;
    if (!r.u16(0, reserved) || !r.u16(2, type) || !r.u16(4, count)) return;
    // type 1 is an icon, 2 a cursor; anything else is not an .ico at all.
    if (reserved != 0 || (type != 1 && type != 2) || count == 0) return;

    // An .ico directory entry is like a PE group-icon entry, except the
    // trailing field is a 4-byte file offset instead of a 2-byte resource id.
    for (int i = 0; i < count; ++i) {
        qint64 o = 6 + qint64(i) * 16;
        if (!r.bytes(o, 16)) break;
        quint16 bitCount;
        r.u16(o + 6, bitCount);
        quint32 length, offset;
        r.u32(o + 8, length);
        r.u32(o + 12, offset);

        IconSlot slot;
        slot.index = i;
        slot.width = d[o];
        slot.bitCount = bitCount;
        slot.payload = [&r, d, offset, length]() -> QByteArray {
            if (!r.bytes(offset, length)) return {};
            return QByteArray(reinterpret_cast<const char *>(d + offset), int(length));
        };
        if (!visit(slot)) return;
    }
}

// Hands `visit` every icon the file holds, whichever container it is, and
// reports which container that was. The file is read once and its bytes
// stay alive for the whole walk.
void forEachIcon(const QString &path, PeIconExtractor::Source &source, const SlotVisitor &visit) {
    source = PeIconExtractor::Source::None;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = file.readAll();
    file.close();

    const uchar *d = reinterpret_cast<const uchar *>(bytes.constData());
    const qint64 size = bytes.size();
    const Reader r{d, size};

    if (size >= 2 && d[0] == 'M' && d[1] == 'Z') {
        if (size < 0x40) return;
        quint32 newHeaderOffset;
        if (!r.u32(0x3c, newHeaderOffset)) return;
        if (!r.bytes(newHeaderOffset, 4)) return;

        if (d[newHeaderOffset] == 'P' && d[newHeaderOffset + 1] == 'E' &&
            d[newHeaderOffset + 2] == 0 && d[newHeaderOffset + 3] == 0) {
            source = PeIconExtractor::Source::Pe;
            walkPeIcons(r, d, newHeaderOffset, visit);
        } else if (d[newHeaderOffset] == 'N' && d[newHeaderOffset + 1] == 'E') {
            source = PeIconExtractor::Source::Ne;
            walkNeIcons(r, d, newHeaderOffset, visit);
        }
        // Anything else is an unrecognized executable format (e.g. OS/2
        // LE/LX) and holds nothing we know how to read.
        return;
    }

    source = PeIconExtractor::Source::Ico;
    walkIcoIcons(r, d, visit);
}

} // namespace

QVector<PeIconExtractor::IconInfo> PeIconExtractor::listIcons(const QString &path,
                                                              Source *source) {
    QVector<IconInfo> list;
    Source found = Source::None;

    forEachIcon(path, found, [&list](const IconSlot &slot) {
        const QImage image = decodeIconImage(slot.payload());
        if (!image.isNull()) {
            IconInfo info;
            info.index = slot.index;
            info.icon = QIcon(QPixmap::fromImage(image));
            info.width = image.width();
            info.height = image.height();
            info.bitCount = slot.bitCount;
            list.append(info);
        }
        return true;
    });

    if (source) *source = list.isEmpty() ? Source::None : found;
    return list;
}

QIcon PeIconExtractor::extractIconAt(const QString &path, int index, Source *source) {
    if (source) *source = Source::None;

    Source found = Source::None;
    QImage chosen;
    int chosenScore = -1;

    forEachIcon(path, found, [&](const IconSlot &slot) {
        if (index >= 0) {
            if (slot.index != index) return true;
            chosen = decodeIconImage(slot.payload());
            return false; // the one that was asked for, decoded or not
        }
        // Nothing asked for in particular: an executable leads with its main
        // icon, while an .ico is a set of sizes to pick the best from.
        if (found != Source::Ico) {
            chosen = decodeIconImage(slot.payload());
            return chosen.isNull();
        }
        const int score = iconScore(slot.width, slot.bitCount);
        if (score <= chosenScore) return true;
        const QImage image = decodeIconImage(slot.payload());
        if (image.isNull()) return true;
        chosen = image;
        chosenScore = score;
        return true;
    });

    // The file's icons have changed since the item was set up and the one it
    // remembers is gone: better its default icon than none at all.
    if (chosen.isNull() && index >= 0) return extractIconAt(path, -1, source);

    if (chosen.isNull()) return {};
    if (source) *source = found;
    return QIcon(QPixmap::fromImage(chosen));
}

QIcon PeIconExtractor::extractIcoFile(const QString &icoPath) {
    return extractIconAt(icoPath, -1);
}

QIcon PeIconExtractor::extractIcon(const QString &exePath, Source *source) {
    return extractIconAt(exePath, -1, source);
}
