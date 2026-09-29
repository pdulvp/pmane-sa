#pragma once

#include "ProgramItem.h"

// Fills in what a program item does not already carry: the name and icon a
// DOSBox config or ScummVM game declares for itself, the icon the desktop
// gives a document, the artwork embedded in an executable, and a working
// directory. Falls back through progressively more generic icons so an
// item always has one.
//
// Whatever the user typed in the properties dialog is left alone, so this
// is safe to run over an item more than once.
void completeProgramItem(ProgramItem &item);

// The file an item's icon artwork comes from: the one the user picked in the
// properties dialog, else the one discovery found when the item was created,
// else the command itself. What the "Change Icon" listing opens on.
QString iconSourceFile(const ProgramItem &item);
