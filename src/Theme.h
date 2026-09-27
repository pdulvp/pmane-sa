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

const QVector<Theme> &builtinThemes();
const Theme &themeByName(const QString &name);
