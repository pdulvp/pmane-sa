#pragma once

#include <QMdiArea>
#include <QPixmap>
#include <QString>

// The Program Manager desktop. Beyond being the MDI area the group
// windows live in, it signs itself with the name of the screen currently
// loaded, so which workspace is on screen is readable at a glance.
class DesktopArea : public QMdiArea {
public:
    explicit DesktopArea(QWidget *parent = nullptr);

    void setScreenName(const QString &name);

    // The picture to show behind the group windows, or an empty path for
    // the scheme's plain desktop colour. Shown the way a desktop shows a
    // wallpaper: scaled to cover and centred, never stretched out of shape.
    void setWallpaper(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    // The desktop itself does not scroll: what scrolls is the arrangement
    // of group windows on it, so the wallpaper and the screen's name are
    // drawn again where they were rather than shifted with the windows.
    void scrollContentsBy(int dx, int dy) override;

private:
    // What the picture looks like at the size it is being shown at, kept
    // so a repaint is not a rescale: the desktop is repainted behind every
    // window that moves across it.
    const QPixmap &scaledWallpaper() const;

    QString m_screenName;
    QString m_wallpaperPath;
    QPixmap m_wallpaper;
    mutable QPixmap m_scaled;
};
