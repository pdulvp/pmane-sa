#include "SteamLibrary.h"

#include "Launchers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

namespace {

// Where Steam itself may be installed. Which one it is varies with how it
// was installed, and on most machines several of these lead to the same
// place.
QStringList steamInstallPaths() {
    const QString home = QDir::homePath();
    const QStringList candidates = {
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.local/share/Steam"),
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

// Valve's own text format, as far as anything here needs it: a key and a
// value, both quoted, one to a line. Nesting is not read -- the keys wanted
// are unique within the files they are read from.
QString valueFor(const QString &text, const QString &key) {
    const QRegularExpression entry(
        QStringLiteral("\"%1\"\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(key)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = entry.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString readAll(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

bool isSteamManifest(const QString &path) {
    const QString name = QFileInfo(path).fileName();
    return name.startsWith(QLatin1String("appmanifest_"), Qt::CaseInsensitive) &&
           name.endsWith(QLatin1String(".acf"), Qt::CaseInsensitive);
}

QStringList discoveredSteamLibraries() {
    QStringList libraries;
    for (const QString &steam : steamInstallPaths()) {
        const QString text =
            readAll(steam + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (text.isEmpty()) continue;

        // A "path" per library folder, Steam's own among them.
        static const QRegularExpression pathEntry(
            QStringLiteral("\"path\"\\s*\"([^\"]*)\""));
        QRegularExpressionMatchIterator matches = pathEntry.globalMatch(text);
        while (matches.hasNext()) {
            const QString path = matches.next().captured(1);
            const QString resolved = QFileInfo(path).canonicalFilePath();
            if (resolved.isEmpty() || libraries.contains(resolved)) continue;
            libraries.append(resolved);
        }
    }
    return libraries;
}

SteamGame readSteamManifest(const QString &path) {
    SteamGame game;
    if (!isSteamManifest(path)) return game;

    const QString text = readAll(path);
    if (text.isEmpty()) return game;

    game.appId = valueFor(text, QStringLiteral("appid"));
    game.name = valueFor(text, QStringLiteral("name"));
    game.manifest = path;
    if (game.appId.isEmpty()) return SteamGame();
    // A manifest without a name is still playable; it is only unlabelled.
    if (game.name.isEmpty()) game.name = game.appId;
    game.iconPath = steamIconPath(game.appId);
    return game;
}

QString steamAppId(const QString &manifestPath) {
    return readSteamManifest(manifestPath).appId;
}

QVector<SteamGame> steamGamesIn(const QStringList &libraries) {
    QVector<SteamGame> games;
    QStringList seen; // the same library reached by two paths

    for (const QString &library : libraries) {
        // A library folder holds steamapps; being handed the steamapps
        // directory itself is the easy mistake, so both are taken.
        QString apps = library + QStringLiteral("/steamapps");
        if (!QDir(apps).exists()) apps = library;

        const QFileInfoList files =
            QDir(apps).entryInfoList({QStringLiteral("appmanifest_*.acf")}, QDir::Files);
        for (const QFileInfo &file : files) {
            const SteamGame game = readSteamManifest(file.absoluteFilePath());
            if (game.appId.isEmpty() || seen.contains(game.appId)) continue;
            seen.append(game.appId);
            games.append(game);
        }
    }

    std::sort(games.begin(), games.end(), [](const SteamGame &a, const SteamGame &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return games;
}

QString steamIconPath(const QString &appId) {
    if (appId.isEmpty()) return {};

    // Steam keeps a folder of artwork per app: banners, hero images, a logo
    // -- and the icon, which is the one named by its hash rather than by
    // what it is. That is the small square an icon grid wants; the rest are
    // the wrong shape entirely.
    static const QRegularExpression iconFile(QStringLiteral("^[0-9a-f]{40}\\.(jpg|png)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    for (const QString &steam : steamInstallPaths()) {
        const QString dir =
            steam + QStringLiteral("/appcache/librarycache/") + appId;
        const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            if (iconFile.match(file.fileName()).hasMatch()) return file.absoluteFilePath();
        }
        // Older Steam kept them flat, named by app id.
        for (const QString &suffix : {QStringLiteral("_icon.jpg"), QStringLiteral("_icon.png")}) {
            const QString flat =
                steam + QStringLiteral("/appcache/librarycache/") + appId + suffix;
            if (QFile::exists(flat)) return flat;
        }
        // Nothing square cached: the logo is at least the game's own
        // artwork, and an icon grid can scale it down.
        const QString logo = dir + QStringLiteral("/logo.png");
        if (QFile::exists(logo)) return logo;
    }
    return {};
}
