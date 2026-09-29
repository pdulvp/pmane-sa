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

QString protonPrefixFor(const QString &exePath) {
    const QFileInfo info(exePath);
    // Named after the executable so the directory is recognisable, keyed by
    // a digest of its full path so two games of the same name stay apart.
    const QByteArray digest =
        QCryptographicHash::hash(info.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1);
    const QString name =
        info.completeBaseName() + QLatin1Char('-') + QString::fromLatin1(digest.toHex().left(8));

    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/prefixes/") + name;
}
