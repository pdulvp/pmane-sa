#include "CompatTools.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace {

// Steam keeps compatibilitytools.d under its install directory. Which one
// that is varies with how Steam was installed, and on most machines
// several of these are symlinks to the same place.
QStringList steamInstallPaths() {
    const QString home = QDir::homePath();
    const QStringList candidates = {
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
    };

    QStringList paths;
    for (const QString &candidate : candidates) {
        const QString resolved = QFileInfo(candidate).canonicalFilePath();
        if (resolved.isEmpty() || paths.contains(resolved)) continue;
        paths.append(resolved);
    }
    return paths;
}

// The name the tool gives itself in compatibilitytool.vdf. The file is
// Valve's own template, whose comments mention the key in passing, so only
// a line that opens with the quoted key counts.
QString readDisplayName(const QString &toolDir) {
    QFile file(toolDir + QStringLiteral("/compatibilitytool.vdf"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    static const QRegularExpression entry(
        QStringLiteral("^\\s*\"display_name\"\\s*\"([^\"]*)\""));
    const QString text = QString::fromUtf8(file.readAll());
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = entry.match(line);
        if (match.hasMatch()) return match.captured(1).trimmed();
    }
    return {};
}

// Where a wine that is not the system one tends to live. Lutris and the
// distribution packages of several builds both keep them as a directory per
// version with the binary under bin/.
QStringList wineSearchRoots() {
    const QString home = QDir::homePath();
    return {
        QStringLiteral("/opt"),
        QStringLiteral("/usr/lib/wine"),
        home + QStringLiteral("/.local/share/lutris/runners/wine"),
        home + QStringLiteral("/.var/app/net.lutris.Lutris/data/lutris/runners/wine"),
    };
}

QVector<WineBuild> scanWineBuilds() {
    QVector<WineBuild> builds;
    QStringList seen; // the same build is often reachable by several paths

    // The one on the PATH first: it is what "wine" means on this machine,
    // and the one a desktop that has never been configured should get.
    const QString system = QStandardPaths::findExecutable(QStringLiteral("wine"));
    if (!system.isEmpty()) {
        builds.append({QStringLiteral("system"),
                       QCoreApplication::translate("CompatTools", "Wine (system)"), system});
        seen << QFileInfo(system).canonicalFilePath();
    }

    for (const QString &root : wineSearchRoots()) {
        const QStringList entries =
            QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            // Either <root>/<build>/bin/wine or <root>/<build>/wine, which is
            // how the two layouts in the wild differ.
            const QString dir = root + QLatin1Char('/') + entry;
            QString binary = dir + QStringLiteral("/bin/wine");
            if (!QFileInfo(binary).isExecutable()) binary = dir + QStringLiteral("/wine");
            if (!QFileInfo(binary).isExecutable()) continue;

            const QString resolved = QFileInfo(binary).canonicalFilePath();
            if (seen.contains(resolved)) continue;
            seen << resolved;
            builds.append({entry, entry, binary});
        }
    }
    return builds;
}

QVector<CompatTool> scanCompatTools() {
    QVector<CompatTool> tools;
    for (const QString &steamPath : steamInstallPaths()) {
        const QString toolsDir = steamPath + QStringLiteral("/compatibilitytools.d");
        const QStringList entries =
            QDir(toolsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            const QString dir = toolsDir + QLatin1Char('/') + entry;
            const QString proton = dir + QStringLiteral("/proton");
            // The proton script is what makes a directory a usable tool;
            // anything else in there is a leftover or a partial download.
            if (!QFileInfo(proton).isExecutable()) continue;

            CompatTool tool;
            tool.id = entry;
            tool.displayName = readDisplayName(dir);
            if (tool.displayName.isEmpty()) tool.displayName = entry;
            tool.protonPath = proton;
            tool.steamPath = steamPath;
            tools.append(tool);
        }
    }

    std::sort(tools.begin(), tools.end(), [](const CompatTool &a, const CompatTool &b) {
        return a.displayName.localeAwareCompare(b.displayName) < 0;
    });
    return tools;
}

} // namespace

const QVector<CompatTool> &installedCompatTools() {
    static const QVector<CompatTool> tools = scanCompatTools();
    return tools;
}

const QVector<WineBuild> &installedWineBuilds() {
    static const QVector<WineBuild> builds = scanWineBuilds();
    return builds;
}

const CompatTool *compatToolById(const QString &id) {
    if (id.isEmpty()) return nullptr;
    for (const CompatTool &tool : installedCompatTools()) {
        if (tool.id == id) return &tool;
    }
    return nullptr;
}

namespace {

// Where a launcher's prefixes live: the folder it names, and this
// application's own data directory until one does.
QString prefixesBase(const QString &baseDir) {
    if (!baseDir.isEmpty()) return baseDir;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/prefixes");
}

// Beside the automatic ones, under whichever folder the launcher keeps them
// in -- the same place as before, only the name is chosen rather than worked
// out.
QString namedPrefixPath(const QString &name, const QString &baseDir) {
    return prefixesBase(baseDir) + QLatin1Char('/') + name;
}

// The per-item prefix. `build` is what tells the launch path from a question
// about it: the folder and the marker naming its program are made the first
// time a program is actually run, and never by something merely asking where
// it would run.
QString prefixPathFor(const QString &exePath, const QString &baseDir, const QString &itemId,
                      bool build);

// The marker file inside a prefix, which says whose it is.
const char *kMarkerFile = ".program";

// Whether this item may have that prefix: an empty folder is free, one
// claimed by this item is its own, and one claimed by another item is not --
// whatever program either of them runs. A prefix from before the markers
// carried an item goes to whichever item asks for it first, which is the one
// that was using it already.
bool freeFor(const QString &path, const QString &exePath, const QString &itemId) {
    if (!QFileInfo::exists(path)) return true;
    const PrefixOwner owner = prefixOwner(path);
    // Claimed by an item: its own, and nobody else's.
    if (!owner.itemId.isEmpty()) return owner.itemId == itemId;
    // Claimed by a program but no item -- made before items had identities.
    // It goes to whichever item asks for it first, which is the one that has
    // been running out of it.
    if (!owner.program.isEmpty()) return owner.program == exePath;
    // No marker at all: older still, from when a prefix was named and left
    // at that. Same rule, there being nothing to tell it apart by.
    return true;
}

// Says the prefix is this item's, so that the next item asking for it is
// given one of its own instead of moving in.
void claim(const QString &path, const QString &exePath, const QString &itemId) {
    if (!QDir().mkpath(path)) return;
    QFile marker(path + QLatin1Char('/') + QLatin1String(kMarkerFile));
    if (!marker.open(QIODevice::WriteOnly)) return;
    marker.write(exePath.toUtf8());
    if (!itemId.isEmpty()) {
        marker.write("\n");
        marker.write(itemId.toUtf8());
    }
    marker.close();
}

} // namespace

QString prefixesFolder(const QString &baseDir) { return prefixesBase(baseDir); }

QString prefixFolderName(const QString &prefixName) {
    QString name = prefixName.trimmed();
    // A name is typed, not picked: anything a folder cannot hold goes.
    name.replace(QRegularExpression(QStringLiteral("[/\\\\:]+")), QStringLiteral("-"));
    while (name.startsWith(QLatin1Char('.'))) name.remove(0, 1);
    return name.trimmed();
}

QString itemPrefixPath(const QString &exePath, const QString &baseDir,
                       const QString &prefixName, const QString &itemId) {
    const QString name = prefixFolderName(prefixName);
    if (name.isEmpty()) return prefixPathFor(exePath, baseDir, itemId, false);
    return namedPrefixPath(name, baseDir);
}

QString itemPrefixFor(const QString &exePath, const QString &baseDir,
                      const QString &prefixName, const QString &itemId) {
    const QString name = prefixFolderName(prefixName);
    if (name.isEmpty()) return prefixPathFor(exePath, baseDir, itemId, true);

    return namedPrefixPath(name, baseDir);
}

PrefixOwner prefixOwner(const QString &prefixPath) {
    PrefixOwner owner;
    QFile marker(prefixPath + QLatin1Char('/') + QLatin1String(kMarkerFile));
    if (!marker.open(QIODevice::ReadOnly)) return owner;

    // The program on the first line, the item that claimed it on the second.
    const QStringList lines = QString::fromUtf8(marker.readAll()).split(QLatin1Char('\n'));
    if (!lines.isEmpty()) owner.program = lines.first().trimmed();
    if (lines.size() > 1) owner.itemId = lines.at(1).trimmed();
    return owner;
}

QString protonPrefixFor(const QString &exePath, const QString &baseDir) {
    return prefixPathFor(exePath, baseDir, QString(), true);
}

namespace {

QString prefixPathFor(const QString &exePath, const QString &baseDir, const QString &itemId,
                      bool build) {
    const QFileInfo info(exePath);

    // Where they all live: the folder the launcher profile names, and the
    // application's own data directory until one does.
    const QString ours = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/prefixes");
    const QString base = prefixesBase(baseDir);

    // Two programs of the same name must not share one install, so the path
    // they were started from settles it.
    const QByteArray digest =
        QCryptographicHash::hash(info.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1);
    const QString hash = QString::fromLatin1(digest.toHex().left(8));

    // Prefixes made before these folders were given readable names are still
    // where they were: a game's saves live in there. A second icon for the
    // same program does not move in, though -- it was built for the one that
    // has it.
    const QString legacy =
        ours + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('-') + hash;
    if (QFileInfo::exists(legacy) && freeFor(legacy, info.absoluteFilePath(), itemId)) {
        if (build) claim(legacy, info.absoluteFilePath(), itemId);
        return legacy;
    }

    // Otherwise a name worth reading in a file manager: the program, under
    // the folder it was installed in, which is what tells two copies of the
    // same game apart at a glance.
    const QString title = info.dir().dirName();
    QString name = title.isEmpty() || title == info.completeBaseName()
                       ? info.completeBaseName()
                       : title + QStringLiteral(" - ") + info.completeBaseName();
    name.replace(QLatin1Char('/'), QLatin1Char('-'));

    // A marker naming the program the prefix was built for and the item that
    // claimed it, so that neither another program of the same name nor a
    // second icon for this one moves into it.
    const QString path = base + QLatin1Char('/') + name;
    if (freeFor(path, info.absoluteFilePath(), itemId)) {
        if (build) claim(path, info.absoluteFilePath(), itemId);
        return path;
    }

    // Taken. Before minting a new one, the folder a second program of the
    // same name would have been given back when the path was all there was
    // to tell them apart: an item that has been running out of that one
    // keeps it.
    const QString byPath = path + QStringLiteral(" (") + hash + QLatin1Char(')');
    if (QFileInfo::exists(byPath) && freeFor(byPath, info.absoluteFilePath(), itemId)) {
        if (build) claim(byPath, info.absoluteFilePath(), itemId);
        return byPath;
    }

    // A second icon for the same program is told apart by its own identity,
    // which is the one thing it does not share with the first.
    const QString mine = itemId.isEmpty() ? hash : itemId;
    const QString own = path + QStringLiteral(" (") + mine + QLatin1Char(')');
    if (build && freeFor(own, info.absoluteFilePath(), itemId)) {
        claim(own, info.absoluteFilePath(), itemId);
    }
    return own;
}

} // namespace
