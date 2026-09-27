#include "PeIconExtractor.h"

#include <QFile>
#include <QImage>
#include <QMap>
#include <QVector>
#include <QtEndian>

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

// Extracts the group-icon image from a modern PE32/PE32+ executable.
QImage extractPeIconImage(const Reader &r, const uchar *d, qint64 size, quint32 peOffset) {
    qint64 coffOffset = peOffset + 4;
    quint16 numSections, sizeOfOptionalHeader;
    if (!r.u16(coffOffset + 2, numSections)) return {};
    if (!r.u16(coffOffset + 16, sizeOfOptionalHeader)) return {};

    qint64 optHeaderOffset = coffOffset + 20;
    quint16 magic;
    if (!r.u16(optHeaderOffset, magic)) return {};

    qint64 dataDirOffset;
    if (magic == 0x10b) { // PE32
        dataDirOffset = optHeaderOffset + 96;
    } else if (magic == 0x20b) { // PE32+
        dataDirOffset = optHeaderOffset + 112;
    } else {
        return {};
    }

    quint32 resourceRva, resourceSize;
    if (!r.u32(dataDirOffset + 2 * 8, resourceRva)) return {};
    if (!r.u32(dataDirOffset + 2 * 8 + 4, resourceSize)) return {};
    if (resourceRva == 0 || resourceSize == 0) return {};

    qint64 sectionTableOffset = optHeaderOffset + sizeOfOptionalHeader;
    QVector<Section> sections;
    for (int i = 0; i < numSections; ++i) {
        qint64 o = sectionTableOffset + i * 40;
        quint32 virtualSize, virtualAddress, rawSize, rawOffset;
        if (!r.u32(o + 8, virtualSize)) return {};
        if (!r.u32(o + 12, virtualAddress)) return {};
        if (!r.u32(o + 16, rawSize)) return {};
        if (!r.u32(o + 20, rawOffset)) return {};
        sections.append({virtualAddress, virtualSize, rawOffset, rawSize});
    }

    qint64 resourceBaseOffset = rvaToOffset(sections, resourceRva);
    if (resourceBaseOffset < 0) return {};

    auto resOffsetToFile = [&](quint32 relOffset) -> qint64 {
        return rvaToOffset(sections, resourceRva + relOffset);
    };

    // Level 1: resource type directory -> RT_GROUP_ICON
    ResourceDirEntry groupIconType;
    if (!findEntryById(r, resourceBaseOffset, RT_GROUP_ICON, groupIconType) ||
        !groupIconType.isDirectory) {
        return {};
    }

    // Level 2: pick the first icon-group resource present.
    qint64 groupTypeDirOffset = resOffsetToFile(groupIconType.offset);
    if (groupTypeDirOffset < 0) return {};
    ResourceDirEntry groupIconRes;
    if (!firstEntry(r, groupTypeDirOffset, groupIconRes) || !groupIconRes.isDirectory) {
        return {};
    }

    // Level 3: language directory -> leaf data entry.
    qint64 groupLangDirOffset = resOffsetToFile(groupIconRes.offset);
    if (groupLangDirOffset < 0) return {};
    ResourceDirEntry groupLeaf;
    if (!firstEntry(r, groupLangDirOffset, groupLeaf) || groupLeaf.isDirectory) {
        return {};
    }

    qint64 groupDataEntryOffset = resOffsetToFile(groupLeaf.offset);
    if (groupDataEntryOffset < 0) return {};
    quint32 groupDataRva, groupDataSize;
    if (!r.u32(groupDataEntryOffset, groupDataRva)) return {};
    if (!r.u32(groupDataEntryOffset + 4, groupDataSize)) return {};

    qint64 groupDataOffset = rvaToOffset(sections, groupDataRva);
    if (groupDataOffset < 0 || !r.bytes(groupDataOffset, groupDataSize)) return {};

    quint16 idCount;
    if (!r.u16(groupDataOffset + 4, idCount)) return {};

    QVector<GrpIconEntry> candidates;
    for (int i = 0; i < idCount; ++i) {
        qint64 o = groupDataOffset + 6 + i * 14;
        if (!r.bytes(o, 14)) break;
        GrpIconEntry e;
        e.width = d[o];
        e.height = d[o + 1];
        quint16 bitCount;
        r.u16(o + 6, bitCount);
        e.bitCount = bitCount;
        quint16 id;
        r.u16(o + 12, id);
        e.id = id;
        candidates.append(e);
    }
    if (candidates.isEmpty()) return {};

    // Prefer a classic 32x32 icon (closest to the win3.1 look); otherwise
    // take the largest/highest quality one available.
    auto score = [](const GrpIconEntry &e) {
        int w = e.width ? e.width : 256;
        int exact32 = (w == 32) ? 1000 : 0;
        return exact32 + w * 10 + e.bitCount;
    };
    GrpIconEntry best = candidates.first();
    for (const GrpIconEntry &e : candidates) {
        if (score(e) > score(best)) best = e;
    }

    // Find RT_ICON entry matching best.id
    ResourceDirEntry iconType;
    if (!findEntryById(r, resourceBaseOffset, RT_ICON, iconType) || !iconType.isDirectory) {
        return {};
    }
    qint64 iconTypeDirOffset = resOffsetToFile(iconType.offset);
    if (iconTypeDirOffset < 0) return {};
    ResourceDirEntry iconRes;
    if (!findEntryById(r, iconTypeDirOffset, best.id, iconRes) || !iconRes.isDirectory) {
        return {};
    }
    qint64 iconLangDirOffset = resOffsetToFile(iconRes.offset);
    if (iconLangDirOffset < 0) return {};
    ResourceDirEntry iconLeaf;
    if (!firstEntry(r, iconLangDirOffset, iconLeaf) || iconLeaf.isDirectory) {
        return {};
    }
    qint64 iconDataEntryOffset = resOffsetToFile(iconLeaf.offset);
    if (iconDataEntryOffset < 0) return {};
    quint32 iconDataRva, iconDataSize;
    if (!r.u32(iconDataEntryOffset, iconDataRva)) return {};
    if (!r.u32(iconDataEntryOffset + 4, iconDataSize)) return {};

    qint64 iconDataOffset = rvaToOffset(sections, iconDataRva);
    if (iconDataOffset < 0 || !r.bytes(iconDataOffset, iconDataSize)) return {};

    QByteArray raw(reinterpret_cast<const char *>(d + iconDataOffset), int(iconDataSize));
    return decodeIconImage(raw);
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

// Extracts the group-icon image from an old 16-bit NE executable.
QImage extractNeIconImage(const Reader &r, const uchar *d, qint64 size, qint64 neHeaderOffset) {
    QMap<quint16, QVector<NeResource>> resources = parseNeResourceTable(r, neHeaderOffset);

    const QVector<NeResource> iconList = resources.value(RT_ICON);
    if (iconList.isEmpty()) return {};

    const QVector<NeResource> groupList = resources.value(RT_GROUP_ICON);

    auto readRaw = [&](const NeResource &res) -> QByteArray {
        if (!r.bytes(res.offset, res.length)) return {};
        return QByteArray(reinterpret_cast<const char *>(d + res.offset), int(res.length));
    };

    if (!groupList.isEmpty()) {
        QByteArray groupData = readRaw(groupList.first());
        Reader gr{reinterpret_cast<const uchar *>(groupData.constData()), groupData.size()};
        quint16 idCount;
        if (groupData.size() >= 6 && gr.u16(4, idCount)) {
            QVector<GrpIconEntry> candidates;
            for (int i = 0; i < idCount; ++i) {
                qint64 o = 6 + i * 14;
                if (!gr.bytes(o, 14)) break;
                GrpIconEntry e;
                e.width = quint8(groupData[o]);
                e.height = quint8(groupData[o + 1]);
                quint16 bitCount;
                gr.u16(o + 6, bitCount);
                e.bitCount = bitCount;
                quint16 id;
                gr.u16(o + 12, id);
                e.id = id;
                candidates.append(e);
            }
            if (!candidates.isEmpty()) {
                auto score = [](const GrpIconEntry &e) {
                    int w = e.width ? e.width : 256;
                    int exact32 = (w == 32) ? 1000 : 0;
                    return exact32 + w * 10 + e.bitCount;
                };
                GrpIconEntry best = candidates.first();
                for (const GrpIconEntry &e : candidates) {
                    if (score(e) > score(best)) best = e;
                }
                for (const NeResource &res : iconList) {
                    if (res.id == best.id) {
                        QImage img = decodeIconImage(readRaw(res));
                        if (!img.isNull()) return img;
                    }
                }
            }
        }
    }

    // No usable group-icon resource: fall back to whichever RT_ICON
    // decodes successfully, preferring the largest.
    QImage bestImage;
    for (const NeResource &res : iconList) {
        QImage img = decodeIconImage(readRaw(res));
        if (img.isNull()) continue;
        if (bestImage.isNull() || img.width() * img.height() > bestImage.width() * bestImage.height()) {
            bestImage = img;
        }
    }
    return bestImage;
}

} // namespace

QIcon PeIconExtractor::extractIcoFile(const QString &icoPath) {
    QFile file(icoPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray bytes = file.readAll();
    file.close();

    const uchar *d = reinterpret_cast<const uchar *>(bytes.constData());
    qint64 size = bytes.size();
    Reader r{d, size};

    quint16 reserved, type, count;
    if (!r.u16(0, reserved) || !r.u16(2, type) || !r.u16(4, count)) return {};
    // type 1 is an icon, 2 a cursor; anything else is not an .ico at all.
    if (reserved != 0 || (type != 1 && type != 2) || count == 0) return {};

    // An .ico directory entry is like a PE group-icon entry, except the
    // trailing field is a 4-byte file offset instead of a 2-byte resource id.
    struct IcoEntry {
        quint8 width;
        quint16 bitCount;
        quint32 offset;
        quint32 length;
    };

    QVector<IcoEntry> entries;
    for (int i = 0; i < count; ++i) {
        qint64 o = 6 + qint64(i) * 16;
        if (!r.bytes(o, 16)) break;
        IcoEntry e;
        e.width = d[o];
        quint16 bitCount;
        r.u16(o + 6, bitCount);
        e.bitCount = bitCount;
        quint32 length, offset;
        r.u32(o + 8, length);
        r.u32(o + 12, offset);
        e.length = length;
        e.offset = offset;
        entries.append(e);
    }
    if (entries.isEmpty()) return {};

    auto score = [](const IcoEntry &e) {
        int w = e.width ? e.width : 256;
        int exact32 = (w == 32) ? 1000 : 0;
        return exact32 + w * 10 + e.bitCount;
    };
    IcoEntry best = entries.first();
    for (const IcoEntry &e : entries) {
        if (score(e) > score(best)) best = e;
    }

    if (!r.bytes(best.offset, best.length)) return {};
    QByteArray raw(reinterpret_cast<const char *>(d + best.offset), int(best.length));
    QImage img = decodeIconImage(raw);
    if (img.isNull()) return {};

    return QIcon(QPixmap::fromImage(img));
}

QIcon PeIconExtractor::extractIcon(const QString &exePath) {
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray bytes = file.readAll();
    file.close();

    const uchar *d = reinterpret_cast<const uchar *>(bytes.constData());
    qint64 size = bytes.size();
    Reader r{d, size};

    if (size < 0x40 || d[0] != 'M' || d[1] != 'Z') return {};

    quint32 newHeaderOffset;
    if (!r.u32(0x3c, newHeaderOffset)) return {};
    if (!r.bytes(newHeaderOffset, 4)) return {};

    QImage img;
    if (d[newHeaderOffset] == 'P' && d[newHeaderOffset + 1] == 'E' &&
        d[newHeaderOffset + 2] == 0 && d[newHeaderOffset + 3] == 0) {
        img = extractPeIconImage(r, d, size, newHeaderOffset);
    } else if (d[newHeaderOffset] == 'N' && d[newHeaderOffset + 1] == 'E') {
        img = extractNeIconImage(r, d, size, newHeaderOffset);
    } else {
        return {}; // unrecognized executable format (e.g. OS/2 LE/LX)
    }

    if (img.isNull()) return {};
    return QIcon(QPixmap::fromImage(img));
}
