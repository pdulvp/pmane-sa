#pragma once

// Whether the Program Manager starts when the session does.
//
// A desktop session starts whatever it finds in ~/.config/autostart, by the
// same .desktop files an application is launched from anywhere else. So the
// setting is not a setting at all: it is that file, there or not there. It
// is read rather than remembered, since a session's own tweak tool can take
// it away without asking us.
bool autostartEnabled();

// Writes the file, or removes it. False when the file could not be written
// or could not be removed, which is worth saying rather than leaving the
// switch looking as though it took.
bool setAutostartEnabled(bool enabled);
