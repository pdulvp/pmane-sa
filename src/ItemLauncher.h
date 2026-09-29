#pragma once

#include "ProgramItem.h"

#include <QString>

// Starts a program item: under wine or the Proton build it names, through
// the emulator that plays it, or -- for a document -- by handing it to
// whatever application the desktop has registered for the type.
//
// Returns an empty string once it is on its way, otherwise a message
// saying why it is not, for the caller to put in front of the user.
QString launchProgramItem(const ProgramItem &item);
