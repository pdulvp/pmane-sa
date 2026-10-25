#pragma once

#include "ProgramItem.h"

// Fills in what a program item does not already carry: the name and icon a
// DOSBox config or ScummVM game declares for itself, the icon the desktop gives
// a document, and the artwork embedded in an executable. Falls back through
// progressively more generic icons so an item always has one.
//
// Anything the user typed in the properties dialog is left alone, so this is
// safe to run over an item more than once.
void completeProgramItem(ProgramItem &item);

// The working directory an item starts in. Derived rather than stored: a
// program needs the folder it sits in, where its data files are, and the few
// items that need something else determine it themselves, so there is nothing
// here to configure. Empty means inherit ours, which is what a Steam game
// needs: Steam starts it from wherever it installed it.
QString workingDirFor(const ProgramItem &item);

// The file an item's icon artwork comes from: the one the user picked in the
// properties dialog, else the one found when the item was created, else the
// command itself. Also what the "Change Icon" listing opens on.
QString iconSourceFile(const ProgramItem &item);
