#pragma once

#include "ProgramItem.h"

#include <QString>

// Starts a program item: under wine or the Proton build it names, through the
// emulator that plays it, by following a .desktop file, or, for a document, by
// handing it to the application the desktop has registered for the type.
//
// Returns an empty string once the program is starting, otherwise a message
// explaining why it is not, for the caller to show the user.
QString launchProgramItem(const ProgramItem &item);
