#pragma once

#include <QString>

// What a program item points at. Each kind is launched a different way.
//
// The classification is by file name alone, never by opening the file:
// groups routinely hold paths on volumes that are not mounted when the
// group is read back in, and those items still have to be listed.
bool isWindowsExecutable(const QString &path);
bool isDosBoxConfig(const QString &path);
// A ScummVM game, identified by one of its data files. ScummVM is pointed
// at the folder that holds it, not at the file itself.
bool isScummVmGame(const QString &path);
// A console ROM or disc image RetroArch plays.
bool isRetroArchRom(const QString &path);
// A freedesktop application launcher, as the desktop's own menus are made
// of: it names the program to run rather than being one.
bool isDesktopEntry(const QString &path);

// A document rather than a program: opened with whatever the desktop has
// registered for it instead of being run.
bool isDocument(const QString &path);

// Whether a dropped file is something a Program Group can hold at all.
bool isLaunchable(const QString &path);
