#pragma once

#include <QString>
#include <QIcon>

// A single launchable entry inside a Program Group (equivalent to a
// PROGRAM ITEM in windows 3.1's progman.ini).
struct ProgramItem {
    QString name;          // Description shown under the icon
    QString command;       // Path to the .exe (or other file) to run under wine
    QString arguments;     // Extra command line arguments
    QString workingDir;    // Working directory when launched
    // Which launcher profile starts this item, as "<launcher>/<profile>" --
    // one of the ones set up in Options > Launchers. Empty means whichever
    // profile its launcher leads with, and so does a profile since deleted.
    QString launcher;
    QIcon icon;            // Icon extracted from the executable
    bool legacyIcon = false; // Came from 16-bit (NE) or .ico artwork
    // Whether this icon is drawn the Windows XP way: tipped into a shallow
    // perspective and lifted off the desktop with a shadow. Per item, since
    // a desktop usually mixes artwork that suits it with artwork that does
    // not.
    bool xpStyleIcon = false;
    // Which of the icons its file holds the item wears. An executable or an
    // .ico often carries several; -1 means whichever one the file leads with,
    // and anything else is a choice made in the properties dialog, which
    // survives the icon being derived again.
    int iconIndex = -1;
    // The file the artwork comes from: the executable itself for most items,
    // the .ico a dosbox conf points at, or whatever the user picked instead.
    // Empty falls back to the command.
    QString iconSource;
};
