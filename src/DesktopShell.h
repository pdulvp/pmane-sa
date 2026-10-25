#pragma once

// Opening the desktop shell's own search.
//
// GNOME keeps its search inside the overview and gives an ordinary application
// no way to put text into it: FocusSearch() and the rest of org.gnome.Shell's
// methods are refused to untrusted callers, and under Wayland nothing can type
// into the shell on a program's behalf either. What remains, and is allowed, is
// opening the overview, which appears with its search box focused.
//
// A search here can therefore only hand the search over, not carry text into
// it, which is why it is triggered when the box is reached rather than as it is
// typed into.

// Whether there is a shell to hand the search to. False on a desktop that is
// not GNOME, where nothing should offer it.
bool desktopSearchAvailable();

// Opens it. False when the shell refused the call or is not present.
bool openDesktopSearch();
