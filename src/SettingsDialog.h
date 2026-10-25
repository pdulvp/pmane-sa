#pragma once

#include <QDialog>

#include "Kiosk.h"

class QCheckBox;
class QToolButton;

// "Settings" dialog: the options governing how the desktop behaves, as opposed
// to the Colors dialog, which governs how it looks.
//
// Laid out in sections rather than as a column of checkboxes: what the strip
// along the top shows is a handful of optional items, and a row of tiles reads
// more quickly than their names in a list.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(bool useDesktopWallpaper, bool autostart, const KioskSettings &kiosk,
                   QWidget *parent = nullptr);

    bool useDesktopWallpaper() const;
    // Whether the session should start the Program Manager. Not one of this
    // application's settings, see Autostart.h, so it is applied rather than
    // saved.
    bool autostart() const;
    KioskSettings kiosk() const;

signals:
    // Emitted while the dialog is open, so the desktop behind shows the choice
    // being made.
    void desktopWallpaperPreviewed(bool use);

private:
    // A heading with a rule under it, which is what delimits a section.
    QWidget *sectionHeading(const QString &text);
    // One option on a row of its own: its name, with the checkbox at the right
    // end, aligned with the others.
    QCheckBox *switchRow(const QString &text, bool on);
    // One of the optional strip items: an icon with its name below it, shown
    // pressed when enabled.
    QToolButton *tile(const QStringList &iconNames, const QString &text, bool on);

    QCheckBox *m_wallpaper;
    QCheckBox *m_autostart;
    QCheckBox *m_locked;
    QToolButton *m_calendar;
    QToolButton *m_volume;
    QToolButton *m_calculator;
    QToolButton *m_search;
};
