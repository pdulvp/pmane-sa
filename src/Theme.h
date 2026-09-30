#pragma once

#include <QColor>
#include <QString>
#include <QVector>

// A colour scheme, in the spirit of the Windows 3.1 Control Panel's
// Color applet: a named, fixed set of the handful of colours the whole
// UI is built from.
struct Theme {
    QString name;
    QColor desktop;           // the Program Manager background
    QColor titleActive;       // active window caption bar
    QColor titleActiveText;
    QColor titleInactive;
    QColor titleInactiveText;
    QColor windowBg;          // group window interior
    QColor windowText;
    QColor buttonFace;        // menus, dialogs, window frames
    QColor buttonText;
    QColor highlight;         // selected icon label
    QColor highlightText;
};

// Black or white, whichever stays readable on this colour. A theme's desktop
// is anything from pale wheat to bright red, and what is drawn straight onto
// it -- a minimized group's label, the menu strip -- has to hold up either
// way.
QColor readableTextOn(const QColor &background);

const QVector<Theme> &builtinThemes();
const Theme &themeByName(const QString &name);

// The scheme the desktop is wearing at the moment, for the parts of the UI a
// palette cannot carry -- a caption colour on something that paints its own
// surface, a dialog's banner. Set as the theme is applied; anything that only
// needs Window, Base or Highlight should keep taking them from the palette.
const Theme &currentTheme();
void setCurrentTheme(const QString &name);
