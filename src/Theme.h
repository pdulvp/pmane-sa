#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QVector>

// A colour scheme, in the spirit of the Windows 3.1 Control Panel's Color
// applet: a named, fixed set of the few colours the whole UI is built from.
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

// Black or white, whichever stays legible on this colour. A theme's desktop
// ranges from pale wheat to bright red, and what is drawn directly on it, such
// as a minimized group's label or the menu strip, has to work either way.
QColor readableTextOn(const QColor &background);

// A themed desktop icon by name, recoloured to `ink`.
//
// The icons a current desktop ships are "symbolic": one flat colour, meant to
// be tinted by the caller. Untinted they come out near-black, which is
// invisible on a dark caption and wrong on a coloured one, so each is given the
// colour of the surface it will sit on.
//
// The names are tried in order, so a desktop that has only the older name still
// gets an icon. Null when none is installed.
QIcon tintedIcon(const QStringList &names, const QColor &ink);

const QVector<Theme> &builtinThemes();
const Theme &themeByName(const QString &name);

// The scheme currently in use, for the parts of the UI a palette cannot carry:
// a caption colour on a widget that paints its own surface, or a dialog's
// banner. Set when the theme is applied; anything that only needs Window, Base
// or Highlight should take them from the palette instead.
const Theme &currentTheme();
void setCurrentTheme(const QString &name);

// The dark version of a scheme: background and text swap roles and the surfaces
// darken to a level comfortable in an unlit room, while the colours the scheme
// is recognisable by, its caption and highlight, are kept. A rose scheme stays
// rose, only darker.
//
// Derived rather than written out, so every scheme has a dark version without a
// second set of colours being chosen for each.
Theme darkVariant(const Theme &light);

// Whether the dark version of the scheme is in use. currentTheme() already
// accounts for it, so callers get the colours actually in use without having to
// check.
bool darkMode();
void setDarkMode(bool dark);

// A scheme part way between two others, for animating from one to the next
// rather than changing the whole desktop's colour in a single frame. `t` runs
// from 0 at `from` to 1 at `to`.
Theme blendThemes(const Theme &from, const Theme &to, qreal t);

// Uses exactly these colours, whatever scheme they came from. The name and the
// dark flag are left unchanged, so the configured theme is still the one that
// was asked for.
void setCurrentThemeColours(const Theme &theme);
