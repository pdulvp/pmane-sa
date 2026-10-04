#pragma once

#include <QIcon>
#include <QMetaType>
#include <QSize>
#include <QString>

#include "WindowScale.h"

// A single launchable entry inside a Program Group (equivalent to a
// PROGRAM ITEM in windows 3.1's progman.ini).
struct ProgramItem {
    // This item, as against any other with the same command. Two icons may
    // point at one executable and be set up differently -- a different
    // prefix, a different scale -- so what tells them apart cannot be worked
    // out from what they point at. Filled in when the item is made; see
    // Ids.h.
    QString id;
    QString name;          // Description shown under the icon
    // Whether that name was typed by a person. Discovery improves on a name
    // it worked out itself -- a .desktop file is a poor label, and the
    // application it describes names itself properly -- but it must never
    // take back a name someone chose, and it cannot tell the two apart by
    // looking: "XnView" is both what a person may want and what
    // XnView.desktop is called.
    bool customName = false;
    QString command;       // Path to the .exe (or other file) to run under wine
    QString arguments;     // Program arguments, added after the launcher's own
    // Which launcher profile starts this item, as "<launcher>/<profile>" --
    // one of the ones set up in Options > Launchers. Empty means whichever
    // profile its launcher leads with, and so does a profile since deleted.
    QString launcher;
    // Which of the launcher's named wine prefixes it runs in, by name. Empty
    // -- the ordinary case -- is the prefix of its own that every program
    // gets, and so is a name since removed from the launcher.
    QString prefix;
    // Windows programs: run the program at this percentage of its own size,
    // inside gamescope. 100 leaves it alone. Kept as a whole number of
    // percent so that a scale of 1.1 is exact. The size it draws at has to
    // be measured from a window it has actually put up, so it is kept here
    // once found rather than guessed at every launch.
    int windowScalePercent = 100;
    // How that scale is achieved. Gamescope scales the picture and needs
    // `nativeSize`; wine's own scaling tells the program to draw bigger and
    // needs nothing measured, which is the only thing that works for a
    // window the program lets you resize.
    WindowScaleMode windowScaleMode = WindowScaleMode::Gamescope;
    QSize nativeSize;
    // A ReShade effect to run it through, by the name gamescope knows it by.
    // Empty for none. Only gamescope can apply one, so naming one runs the
    // program through gamescope even at its own size.
    QString shader;
    QIcon icon;            // Icon extracted from the executable
    bool legacyIcon = false; // Came from 16-bit (NE) or .ico artwork
    // Whether this icon is drawn the Windows XP way: tipped into a shallow
    // perspective and lifted off the desktop with a shadow. Per item, since
    // a desktop usually mixes artwork that suits it with artwork that does
    // not.
    bool xpStyleIcon = false;
    // Which of the icons its file holds the item wears. An executable or an
    // .ico often carries several; -1 means whichever one the file leads with,
    // and anything else is a choice made in the properties dialog, which
    // survives the icon being derived again.
    int iconIndex = -1;
    // The file the artwork comes from: the executable itself for most items,
    // the .ico a dosbox conf points at, or whatever the user picked instead.
    // Empty falls back to the command.
    QString iconSource;
};

// So that a whole item can be carried on a list row, which is how a drag
// hands one group's item to another without either of them having to take
// it apart field by field.
Q_DECLARE_METATYPE(ProgramItem)
