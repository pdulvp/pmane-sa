#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Steam records what it has installed in "app manifests": one .acf file per
// game, in the steamapps directory of the library folder the game was installed
// into. A manifest names the game and carries the id Steam starts it by, which
// is all an item needs, Steam locating the game itself.
struct SteamGame {
    QString appId;    // the id steam://rungameid takes
    QString name;     // as Steam knows it
    QString manifest; // the .acf file, which is what an item points at
    QString iconPath; // the icon Steam cached, empty when it has none
};

// Whether a path is one of these manifests.
bool isSteamManifest(const QString &path);

// The library folders Steam itself knows about, read from the
// libraryfolders.vdf beside its own steamapps. One is Steam's own directory and
// the rest are wherever games have been installed.
QStringList discoveredSteamLibraries();

// Every manifest across `libraries`, read and sorted by name.
QVector<SteamGame> steamGamesIn(const QStringList &libraries);

// One manifest, read. The name is empty when the file is not a manifest.
SteamGame readSteamManifest(const QString &path);

// The id a manifest names, which the launcher passes to Steam.
QString steamAppId(const QString &manifestPath);

// The icon Steam cached for an app: the small square among its per-app artwork,
// which suits an icon grid, rather than the banners and hero images beside it.
// Empty when Steam has cached none.
QString steamIconPath(const QString &appId);
