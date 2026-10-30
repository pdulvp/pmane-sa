#pragma once

#include <QIcon>
#include <QMetaType>
#include <QSize>
#include <QString>

#include "WindowScale.h"

// A single launchable entry inside a Program Group, equivalent to a
// PROGRAM ITEM in Windows 3.1's progman.ini.
struct ProgramItem {
    // Identifies this item as distinct from any other with the same command.
    // Two icons can point at one executable with different settings, such as a
    // different prefix or scale, so they cannot be told apart by what they
    // point at. Assigned when the item is created; see Ids.h.
    QString id;
    QString name;          // Description shown under the icon
    // Whether the name was typed by the user. Discovery may improve on a name
    // it derived itself, a .desktop file name being a poor label, but it must
    // never overwrite a name the user chose, and the two cannot be told apart
    // by inspection: "XnView" is both a plausible user choice and the name of
    // XnView.desktop.
    bool customName = false;
    QString command;       // Path to the .exe (or other file) to run under wine
    QString arguments;     // Program arguments, added after the launcher's own
    // Which launcher profile starts this item, as "<launcher>/<profile>", from
    // the ones set up in Options > Launchers. Empty, or a profile since
    // deleted, means its launcher's default.
    QString launcher;
    // Which emulator core plays it, by the core's file name, e.g.
    // "dolphin_libretro.so". Empty means the one the launcher's table gives
    // the file type, which is what every item of a desktop made before cores
    // could be picked per item uses. Several cores often claim one extension:
    // an .iso is a GameCube disc to Dolphin and a PSP one to PPSSPP, and only
    // the item can say which it is.
    QString core;
    // Which of the launcher's named wine prefixes it runs in. Empty, the usual
    // case, or a name since removed from the launcher, means the per-item
    // prefix every program gets.
    QString prefix;
    // Windows programs: run the program at this percentage of its own size,
    // inside gamescope. 100 means unscaled. Stored as a whole percentage so a
    // scale of 1.1 is exact. The size it draws at can only be measured from a
    // window it has actually opened, so it is stored once found rather than
    // re-measured at every launch.
    int windowScalePercent = 100;
    // How that scale is achieved. Gamescope scales the image and needs
    // `nativeSize`; wine's own scaling makes the program draw larger and needs
    // no measurement, which is the only method that works for a resizable
    // window.
    WindowScaleMode windowScaleMode = WindowScaleMode::Gamescope;
    QSize nativeSize;
    // A ReShade effect to run it through, by the name gamescope uses. Empty for
    // none. Only gamescope can apply one, so naming an effect runs the program
    // through gamescope even unscaled.
    QString shader;
    QIcon icon;            // Icon extracted from the executable
    bool legacyIcon = false; // Came from 16-bit (NE) or .ico artwork
    // Whether this icon is drawn in the Windows XP style: tilted into a shallow
    // perspective with a drop shadow. Per item, since a desktop usually mixes
    // artwork that suits it with artwork that does not.
    bool xpStyleIcon = false;
    // Which of the icons in its file the item uses. An executable or an .ico
    // often holds several; -1 means the file's first one, and any other value
    // is a choice made in the properties dialog, which survives the icon being
    // derived again.
    int iconIndex = -1;
    // The file the artwork comes from: the executable itself for most items,
    // the .ico a dosbox conf points at, or a file the user picked. Empty falls
    // back to the command.
    QString iconSource;
};

// So a whole item can be carried on a list row, which is how a drag moves an
// item from one group to another without either side taking it apart field by
// field.
Q_DECLARE_METATYPE(ProgramItem)
