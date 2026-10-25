#pragma once

#include <QString>

// What a program item points at. Each kind is launched a different way.
//
// The classification is by file name alone, never by opening the file: groups
// routinely hold paths on volumes that are not mounted when the group is read
// back, and those items still have to be listed. Folders are the one
// exception, having no extension to go by.
bool isWindowsExecutable(const QString &path);
bool isDosBoxConfig(const QString &path);
// A ScummVM game, identified by one of its data files. ScummVM is pointed at
// the folder holding it, not at the file itself.
bool isScummVmGame(const QString &path);
// A console ROM or disc image RetroArch plays.
bool isRetroArchRom(const QString &path);
// A freedesktop application launcher, the kind the desktop's own menus are
// built from: it names the program to run rather than being one.
bool isDesktopEntry(const QString &path);

// A folder, opened in the file manager. The one kind that looks at the file
// system, there being no extension to go by. When its volume is not mounted, a
// path with nothing resembling a file extension is treated as a folder anyway.
bool isFolder(const QString &path);

// A document rather than a program: opened with whatever the desktop has
// registered for the type instead of being run.
bool isDocument(const QString &path);

// Whether a dropped file is something a Program Group can hold at all.
bool isLaunchable(const QString &path);
