#pragma once

#include <QMdiArea>
#include <QPixmap>
#include <QString>

// The Program Manager desktop. As well as being the MDI area the group windows
// live in, it draws the name of the screen currently loaded, so which workspace
// is on screen can be seen at a glance.
class DesktopArea : public QMdiArea {
public:
    explicit DesktopArea(QWidget *parent = nullptr);

    void setScreenName(const QString &name);

    // The picture to show behind the group windows, or an empty path for the
    // scheme's plain desktop colour. Drawn as a desktop shows a wallpaper:
    // scaled to cover and centred, never stretched out of proportion.
    void setWallpaper(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    // The desktop itself does not scroll; the arrangement of group windows on
    // it does. The wallpaper and the screen name are therefore redrawn in place
    // rather than shifted with the windows.
    void scrollContentsBy(int dx, int dy) override;

private:
    // The picture at the size it is shown, cached so a repaint is not a
    // rescale: the desktop is repainted behind every window that moves across
    // it.
    const QPixmap &scaledWallpaper() const;

    QString m_screenName;
    QString m_wallpaperPath;
    QPixmap m_wallpaper;
    mutable QPixmap m_scaled;
};
