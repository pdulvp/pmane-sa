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
    QIcon icon;            // Icon extracted from the executable
    bool legacyIcon = false; // Came from 16-bit (NE) or .ico artwork
};
