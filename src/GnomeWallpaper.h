#pragma once

#include <QObject>
#include <QSize>
#include <QString>

class QProcess;

// The picture the GNOME desktop is using, so the Program Manager can show the
// same one behind its group windows.
//
// Read with gsettings rather than through the desktop portal: the portal
// publishes the appearance settings but not the background, and pulling in GIO
// to read one string is not worth it.
class GnomeWallpaper : public QObject {
    Q_OBJECT
public:
    explicit GnomeWallpaper(QObject *parent = nullptr);

    // The image GNOME is currently showing, empty when there is none to be
    // found: no gsettings available, no background set, or a missing file.
    // `preferred` is the size it will be shown at, a themed wallpaper offering
    // one picture per screen size, of which the nearest is taken.
    static QString currentPath(bool dark, const QSize &preferred = QSize());

signals:
    // The desktop background changed. Read the new one with currentPath(); the
    // signal carries nothing, since the setting may name a theme file whose
    // picture depends on the screen.
    void changed();

private:
    QProcess *m_monitor = nullptr;
};
