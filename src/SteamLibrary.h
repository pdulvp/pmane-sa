#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Steam keeps what it has installed in "app manifests": one .acf file per
// game, in the steamapps directory of whichever library folder the game was
// installed into. A manifest names the game and carries the id Steam starts
// it by, which is the whole of what an item needs -- the game itself is
// Steam's to find.
struct SteamGame {
    QString appId;    // what steam://rungameid wants
    QString name;     // as Steam knows it
    QString manifest; // the .acf, which is what an item points at
    QString iconPath; // the icon Steam cached, empty when it has none
};

// Whether a path is one of those manifests.
bool isSteamManifest(const QString &path);

// The library folders Steam itself knows about, read from the
// libraryfolders.vdf beside its own steamapps. One is Steam's own directory
// and the rest are wherever games have been installed since.
QStringList discoveredSteamLibraries();

// Every manifest across `libraries`, read. Sorted by name, which is the
// order anyone would look for a game in.
QVector<SteamGame> steamGamesIn(const QStringList &libraries);

// One manifest, read. The name is empty when the file is not one.
SteamGame readSteamManifest(const QString &path);

// The id a manifest names, for the launcher to hand to Steam.
QString steamAppId(const QString &manifestPath);

// The icon Steam cached for an app: a small square among the artwork it
// keeps per app, which is the one that suits an icon grid rather than the
// banners and hero images beside it. Empty when Steam has cached none.
QString steamIconPath(const QString &appId);
