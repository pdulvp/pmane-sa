#pragma once

// Opening the desktop shell's own search.
//
// GNOME keeps its search inside the overview, and gives an ordinary
// application no way to put text into it: FocusSearch() and the rest of
// org.gnome.Shell's methods are refused to callers the shell does not
// trust, and under Wayland nothing can type into the shell on a program's
// behalf either. What is left -- and what is allowed -- is opening the
// overview, which lands with its search box focused and ready.
//
// So a search here can only hand the search over, not carry anything into
// it: whatever has been typed already would be lost, which is why this is
// done as the box is reached rather than as it is typed into.

// Whether there is a shell to hand it to. False on a desktop that is not
// GNOME, where nothing should offer to.
bool desktopSearchAvailable();

// Opens it. False when the shell refused or is not there.
bool openDesktopSearch();
