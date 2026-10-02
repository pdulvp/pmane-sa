#pragma once

#include <QColor>
#include <QIcon>
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

// A desktop's own icon by name, recoloured to `ink`.
//
// The icons a current desktop ships are "symbolic": one flat colour, meant
// to be tinted by whoever draws them. Left alone they come out near-black,
// which is invisible on a dark caption and wrong on a coloured one -- so
// each is given the colour of whatever surface it is going to sit on.
//
// The names are tried in order, so a desktop that has only the older
// spelling still gets an icon. Null when none of them is installed.
QIcon tintedIcon(const QStringList &names, const QColor &ink);

const QVector<Theme> &builtinThemes();
const Theme &themeByName(const QString &name);

// The scheme the desktop is wearing at the moment, for the parts of the UI a
// palette cannot carry -- a caption colour on something that paints its own
// surface, a dialog's banner. Set as the theme is applied; anything that only
// needs Window, Base or Highlight should keep taking them from the palette.
const Theme &currentTheme();
void setCurrentTheme(const QString &name);

// The same scheme after dark: the paper and the ink change places and the
// surfaces go down to where a lit screen is comfortable in an unlit room,
// while the colour the scheme is recognisable by -- its caption, its
// highlight -- is kept. Rose stays rosy; it simply stops being bright.
//
// Derived rather than written out, so every scheme has a dark half without
// anyone having to choose twelve more sets of colours.
Theme darkVariant(const Theme &light);

// Whether the desktop is wearing the dark half of its scheme. currentTheme()
// answers with it, so anything that asks for the scheme gets the one on
// show without having to know which half it is.
bool darkMode();
void setDarkMode(bool dark);

// A scheme part way between two others, for moving from one to the next
// without the whole desktop changing colour between one frame and the
// following one. `t` runs 0 to 1, from `from` to `to`.
Theme blendThemes(const Theme &from, const Theme &to, qreal t);

// Wears exactly these colours, whatever scheme they came from. The name and
// the dark flag are left as they are, so what the desktop is set to remains
// what it was asked for.
void setCurrentThemeColours(const Theme &theme);
