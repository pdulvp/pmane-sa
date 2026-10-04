#pragma once

#include <QDialog>

#include "Kiosk.h"

class QCheckBox;
class QToolButton;

// "Settings" dialog: the switches that say how the desktop behaves, as
// against the Colors dialog's say in how it looks.
//
// Laid out as sections rather than as a column of checkboxes: what the
// strip along the top carries is a handful of things to have or not have,
// and a row of tiles showing each one is a quicker read than their names in
// a list.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(bool useDesktopWallpaper, const KioskSettings &kiosk,
                   QWidget *parent = nullptr);

    bool useDesktopWallpaper() const;
    KioskSettings kiosk() const;

signals:
    // Switched while the dialog is still open, so the desktop behind shows
    // what is being chosen.
    void desktopWallpaperPreviewed(bool use);

private:
    // A heading with a rule under it, which is what makes a section a
    // section.
    QWidget *sectionHeading(const QString &text);
    // One switch on a line of its own: its name, and the switch at the far
    // end of the row where every other row's switch is.
    QCheckBox *switchRow(const QString &text, bool on);
    // One of the things the strip can carry: an icon with its name under
    // it, pressed in when it is on.
    QToolButton *tile(const QStringList &iconNames, const QString &text, bool on);

    QCheckBox *m_wallpaper;
    QCheckBox *m_locked;
    QToolButton *m_calendar;
    QToolButton *m_volume;
    QToolButton *m_calculator;
    QToolButton *m_search;
};
