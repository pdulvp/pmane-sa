#include "CompatTools.h"

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

const CompatTool *compatToolById(const QString &id) {
    if (id.isEmpty()) return nullptr;
    for (const CompatTool &tool : installedCompatTools()) {
        if (tool.id == id) return &tool;
    }
    return nullptr;
}

QString protonPrefixFor(const QString &exePath, const QString &baseDir) {
    const QFileInfo info(exePath);

    // Where they all live: the folder the launcher profile names, and the
    // application's own data directory until one does.
    const QString ours = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/prefixes");
    const QString base = baseDir.isEmpty() ? ours : baseDir;

    // Two programs of the same name must not share one install, so the path
    // they were started from settles it.
    const QByteArray digest =
        QCryptographicHash::hash(info.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1);
    const QString hash = QString::fromLatin1(digest.toHex().left(8));

    // Prefixes made before these folders were given readable names are still
    // where they were: a game's saves live in there.
    const QString legacy =
        ours + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('-') + hash;
    if (QFileInfo::exists(legacy)) return legacy;

    // Otherwise a name worth reading in a file manager: the program, under
    // the folder it was installed in, which is what tells two copies of the
    // same game apart at a glance.
    const QString title = info.dir().dirName();
    QString name = title.isEmpty() || title == info.completeBaseName()
                       ? info.completeBaseName()
                       : title + QStringLiteral(" - ") + info.completeBaseName();
    name.replace(QLatin1Char('/'), QLatin1Char('-'));

    // A marker naming the program the prefix was built for, so a second
    // program that would have taken the same folder is given one of its own
    // instead of moving into this one.
    const QString path = base + QLatin1Char('/') + name;
    const QString marker = path + QStringLiteral("/.program");
    QFile markerFile(marker);
    if (QFileInfo::exists(path)) {
        if (markerFile.open(QIODevice::ReadOnly)) {
            const QString owner = QString::fromUtf8(markerFile.readAll()).trimmed();
            markerFile.close();
            if (owner == info.absoluteFilePath()) return path;
        }
        return path + QStringLiteral(" (") + hash + QLatin1Char(')');
    }

    if (QDir().mkpath(path) && markerFile.open(QIODevice::WriteOnly)) {
        markerFile.write(info.absoluteFilePath().toUtf8());
        markerFile.close();
    }
    return path;
}
