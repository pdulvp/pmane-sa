#pragma once

#include <QDialog>

class QCheckBox;

// "Settings" dialog: the switches that say how the desktop behaves, as
// against the Colors dialog's say in how it looks.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(bool useDesktopWallpaper, QWidget *parent = nullptr);

    bool useDesktopWallpaper() const;

signals:
    // Switched while the dialog is still open, so the desktop behind shows
    // what is being chosen.
    void desktopWallpaperPreviewed(bool use);

private:
    QCheckBox *m_wallpaper;
};
