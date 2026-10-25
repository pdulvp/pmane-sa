#pragma once

// What can be done with the Program Manager's own window, and what the strip
// along the top shows.
//
// The desktop is usually the whole screen and the only thing on it: a machine
// in a cabinet, a television, a PC that boots into games. On one of those, a
// window that can be shrunk or minimized can be lost with nothing to bring it
// back, so the window is held in place. With the lock off it behaves like any
// other window.
struct KioskSettings {
    // The window cannot leave full screen: no restore button, no minimize, and
    // a double click on the strip does not shrink it. Off restores all three.
    bool locked = true;

    // What the strip shows. Each item is present unless turned off: a kiosk may
    // want only the clock, or nothing at all.
    bool showCalendar = true;   // the clock, and the month it opens
    bool showVolume = true;     // the speaker and its slider
    bool showCalculator = true; // the calculator pane
    bool showSearch = true;     // the magnifier, which hands over to the shell
};

// The last saved settings, with the defaults above filling in what is missing.
// Read once and cached for the session, like the launchers.
const KioskSettings &kioskSettings();
void setKioskSettings(const KioskSettings &settings);
