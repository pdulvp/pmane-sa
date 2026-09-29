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
    // Which compatibility layer runs a Windows executable: empty for the
    // system wine, otherwise the id of an installed Steam compatibility
    // tool (a Proton build). Ignored for anything but an .exe.
    QString runner;
    QIcon icon;            // Icon extracted from the executable
    bool legacyIcon = false; // Came from 16-bit (NE) or .ico artwork
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
