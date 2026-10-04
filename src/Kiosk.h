#pragma once

// What the Program Manager lets anyone in front of it do with its own
// window, and what it keeps in the strip along the top.
//
// The desktop is usually the whole screen and the only thing on it: a
// machine in a cabinet, a television in the corner, a cupboard PC that
// boots into games. On one of those, a window that can be shrunk or
// minimized is a window that can be lost behind nothing at all, so the
// Program Manager holds it where it is. On an ordinary desktop it is a
// window like any other, which is what turning this off gives back.
struct KioskSettings {
    // The window cannot be brought back out of full screen: no restore
    // button, no minimize, and a double click on the strip will not shrink
    // it. Off restores all three.
    bool locked = true;

    // What the strip carries. Each is there unless it is turned off: a
    // kiosk may want the clock and nothing else, or nothing at all.
    bool showCalendar = true;   // the clock, and the month it drops open
    bool showVolume = true;     // the speaker and its slider
    bool showCalculator = true; // the sums pane
    bool showSearch = true;     // the magnifier, which hands over to the shell
};

// What was last saved, filled in with the defaults above. Read once and kept
// for the session, like the launchers.
const KioskSettings &kioskSettings();
void setKioskSettings(const KioskSettings &settings);
