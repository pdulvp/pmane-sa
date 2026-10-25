#pragma once

// Whether the Program Manager starts when the session does.
//
// A desktop session starts whatever it finds in ~/.config/autostart, using the
// same .desktop files applications are launched from elsewhere. The setting is
// therefore the presence or absence of that file. It is read rather than
// cached, since another tool can remove it at any time.
bool autostartEnabled();

// Writes the file, or removes it. Returns false when the file could not be
// written or removed, which the caller reports rather than leaving the checkbox
// looking as though it worked.
bool setAutostartEnabled(bool enabled);
