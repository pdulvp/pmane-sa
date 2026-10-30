#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct Launcher;
struct LauncherRunner;

// One libretro core installed beside an emulator, as its `.info` file
// describes it. Nothing here is configured: RetroArch ships a `.info` for
// every core it installs, which is where the file types a core plays and the
// name to show for it come from.
struct LibretroCore {
    // The file RetroArch loads, e.g. "mesen_libretro.so". This is what an item
    // stores, so a core keeps working when the install moves.
    QString file;
    // "Nintendo - NES / Famicom (Mesen)", the name to show.
    QString displayName;
    // "Nintendo Entertainment System", kept for the tooltip: the display name
    // is already long and two cores for one system are told apart by it.
    QString systemName;
    // The file types it plays, lower case and without the dot.
    QStringList extensions;

    bool handles(const QString &extension) const;
};

// The cores of one install. A runner is an install: it has its own
// `retroarch.cfg`, which names the folder its cores and their `.info` files
// are in, so two RetroArch builds on one machine have nothing in common here.
//
// Read once per runner and kept for the session: resolving a runner given as
// an alias means asking an interactive shell, which is slow enough that the
// properties dialog cannot do it while the command is being typed.
QVector<LibretroCore> libretroCores(const Launcher &launcher, const LauncherRunner &runner);

// Those of them that play this file, by display name. Empty when nothing was
// found, which is also what a launcher that uses no cores returns.
QVector<LibretroCore> libretroCoresFor(const Launcher &launcher, const LauncherRunner &runner,
                                       const QString &path);
