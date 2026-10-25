#pragma once

#include <QString>

// What a freedesktop .desktop file says about the application it launches. Only
// the keys a Program Group needs are read: its name, its icon and what to run.
struct DesktopEntry {
    QString name;     // Name=, in the closest translation to the user's locale
    QString icon;     // Icon=: an icon theme name, or a path to an image
    QString exec;     // Exec=, field codes and all
    QString path;     // Path=: the directory the application wants to start in
    QString type;     // Type=: Application, Link or Directory
    bool terminal = false;  // Terminal=: the application needs a terminal window
    // NoDisplay= or Hidden=: a helper the desktop keeps out of its own menus,
    // such as a file-type handler, or an entry the user removed.
    bool noDisplay = false;

    bool isValid() const { return !exec.isEmpty(); }
};

// Reads the [Desktop Entry] group of a .desktop file. A file that is not one, or
// that launches nothing, is returned with an empty exec.
DesktopEntry readDesktopEntry(const QString &path);

// Every application the desktop offers in its own menus, as paths to the
// .desktop files describing them: the applications directories XDG defines, the
// user's own first, with one entry per desktop-file id so a copy in the home
// directory replaces the system one it overrides.
QStringList installedDesktopEntries();

// The Exec= line as a command and its arguments, with the field codes the
// specification defines (%f, %U, %i and the rest) removed: they stand for a file
// or url to open, which a Program Group item does not provide.
QStringList desktopEntryCommand(const DesktopEntry &entry);
