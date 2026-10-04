#pragma once

#include <QObject>
#include <QSize>
#include <QString>

class QProcess;

// The picture the GNOME desktop is wearing, so the Program Manager can wear
// the same one behind its group windows.
//
// Read with gsettings rather than through the desktop portal: the portal
// publishes the appearance settings but not the background, and pulling GIO
// in to read one string is more than this is worth.
class GnomeWallpaper : public QObject {
    Q_OBJECT
public:
    explicit GnomeWallpaper(QObject *parent = nullptr);

    // The image GNOME is showing now, empty when there is none to be found
    // -- no gsettings to ask, no background set, or a file that has gone.
    // `preferred` is the size it will be shown at: a themed wallpaper offers
    // one picture per screen size and the nearest is taken.
    static QString currentPath(bool dark, const QSize &preferred = QSize());

signals:
    // Someone changed the desktop's background. What it is now is read back
    // with currentPath(); the signal carries nothing because the setting may
    // name a theme file whose picture depends on the screen.
    void changed();

private:
    QProcess *m_monitor = nullptr;
};
