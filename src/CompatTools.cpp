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

// Steam keeps compatibilitytools.d under its install directory. Which one that
// is depends on how Steam was installed, and on most machines several of these
// are symlinks to the same place.
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

// The name the tool declares in compatibilitytool.vdf. The file is Valve's
// template, whose comments also mention the key, so only a line beginning with
// the quoted key is accepted.
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

// Where a non-system wine is usually installed. Lutris and the distribution
// packages of several builds both use one directory per version with the binary
// under bin/.
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
    QStringList seen; // the same build is often reachable through several paths

    // The one on the PATH first: it is what "wine" means on this machine, and
    // what an unconfigured desktop should use.
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
            // Either <root>/<build>/bin/wine or <root>/<build>/wine, the two
            // layouts in common use.
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
            // without it the directory is a leftover or a partial download.
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

// Where a launcher's prefixes live: the folder it names, or this application's
// data directory until one is set.
QString prefixesBase(const QString &baseDir) {
    if (!baseDir.isEmpty()) return baseDir;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/prefixes");
}

// Alongside the automatic ones, in whichever folder the launcher uses: the same
// place, except that the name is chosen rather than derived.
QString namedPrefixPath(const QString &name, const QString &baseDir) {
    return prefixesBase(baseDir) + QLatin1Char('/') + name;
}

// The per-item prefix. `build` distinguishes a launch from a query: the folder
// and the marker naming its program are created the first time a program is
// actually run, never by a caller only asking where it would run.
QString prefixPathFor(const QString &exePath, const QString &baseDir, const QString &itemId,
                      bool build);

// The marker file inside a prefix, which records who owns it.
const char *kMarkerFile = ".program";

// Whether this item may use that prefix: an empty folder is free, one claimed
// by this item is its own, and one claimed by another item is not, whatever
// program either runs. A prefix from before the markers recorded an item goes
// to the first item that asks for it, which is the one already using it.
bool freeFor(const QString &path, const QString &exePath, const QString &itemId) {
    if (!QFileInfo::exists(path)) return true;
    const PrefixOwner owner = prefixOwner(path);
    // Claimed by an item: available to that item only.
    if (!owner.itemId.isEmpty()) return owner.itemId == itemId;
    // Claimed by a program but no item, so created before items had ids. It
    // goes to the first item that asks for it, which is the one already using
    // it.
    if (!owner.program.isEmpty()) return owner.program == exePath;
    // No marker at all: older still, from when a prefix was only named. The
    // same rule applies, there being nothing else to go by.
    return true;
}

// Records the prefix as this item's, so the next item asking for it is given
// one of its own instead.
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
    // The name is typed rather than chosen, so anything invalid in a folder
    // name is removed.
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

    // The folder the launcher profile names, or this application's data
    // directory until one is set.
    const QString ours = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/prefixes");
    const QString base = prefixesBase(baseDir);

    // Two programs with the same name must not share a prefix, so the path they
    // were started from distinguishes them.
    const QByteArray digest =
        QCryptographicHash::hash(info.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1);
    const QString hash = QString::fromLatin1(digest.toHex().left(8));

    // Prefixes created before these folders had readable names are left where
    // they are, a game's saves being inside. A second icon for the same program
    // does not reuse one, which belongs to the item that claimed it.
    const QString legacy =
        ours + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('-') + hash;
    if (QFileInfo::exists(legacy) && freeFor(legacy, info.absoluteFilePath(), itemId)) {
        if (build) claim(legacy, info.absoluteFilePath(), itemId);
        return legacy;
    }

    // Otherwise a name that is readable in a file manager: the program, under
    // the folder it was installed in, which distinguishes two copies of the
    // same game.
    const QString title = info.dir().dirName();
    QString name = title.isEmpty() || title == info.completeBaseName()
                       ? info.completeBaseName()
                       : title + QStringLiteral(" - ") + info.completeBaseName();
    name.replace(QLatin1Char('/'), QLatin1Char('-'));

    // A marker naming the program the prefix was created for and the item that
    // claimed it, so neither another program of the same name nor a second icon
    // for this one reuses it.
    const QString path = base + QLatin1Char('/') + name;
    if (freeFor(path, info.absoluteFilePath(), itemId)) {
        if (build) claim(path, info.absoluteFilePath(), itemId);
        return path;
    }

    // Already claimed. Before generating a new name, try the folder a second
    // program of the same name would have been given when the path was the only
    // distinguishing feature: an item already using that one keeps it.
    const QString byPath = path + QStringLiteral(" (") + hash + QLatin1Char(')');
    if (QFileInfo::exists(byPath) && freeFor(byPath, info.absoluteFilePath(), itemId)) {
        if (build) claim(byPath, info.absoluteFilePath(), itemId);
        return byPath;
    }

    // A second icon for the same program is distinguished by its id, which is
    // the one thing it does not share with the first.
    const QString mine = itemId.isEmpty() ? hash : itemId;
    const QString own = path + QStringLiteral(" (") + mine + QLatin1Char(')');
    if (build && freeFor(own, info.absoluteFilePath(), itemId)) {
        claim(own, info.absoluteFilePath(), itemId);
    }
    return own;
}

} // namespace
