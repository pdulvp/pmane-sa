#pragma once

// The machine's own output volume, as the desktop's mixer sees it.
//
// Spoken to through wpctl, WirePlumber's command line, which is what a
// PipeWire desktop keeps its mixer behind. There is no library to link and
// no D-Bus interface to call: the session's sound is the session manager's,
// and this is the way it is asked.

// Whether there is a mixer to talk to at all. False on a machine without
// WirePlumber, where no volume control should be offered.
bool systemVolumeAvailable();

// The default output's volume, 0 to 1. Negative when it could not be read.
double systemVolume();

// Whether the default output is muted.
bool systemVolumeMuted();

// Sets them. Out-of-range volumes are clamped rather than refused.
void setSystemVolume(double volume);
void setSystemVolumeMuted(bool muted);
