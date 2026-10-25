#include "DesktopArea.h"

#include "MinimizedGroupIcon.h"

#include <QPainter>
#include <QPaintEvent>

DesktopArea::DesktopArea(QWidget *parent) : QMdiArea(parent) {
    // A group window dragged off the edge, or left outside after the Program
    // Manager window was made smaller, is still on the desktop, and the scroll
    // bars are how it is reached again. Progman's client area scrolled the same
    // way.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void DesktopArea::scrollContentsBy(int dx, int dy) {
    QMdiArea::scrollContentsBy(dx, dy);
    // Scrolling blits what was already drawn and repaints only the strip that
    // came into view, which would move the wallpaper and the screen name with
    // it. Both belong to the viewport rather than to its contents, so the whole
    // viewport is repainted instead.
    viewport()->update();
}

void DesktopArea::setScreenName(const QString &name) {
    if (m_screenName == name) return;
    m_screenName = name;
    viewport()->update();
}

void DesktopArea::setWallpaper(const QString &path) {
    if (m_wallpaperPath == path) return;
    m_wallpaperPath = path;
    m_wallpaper = path.isEmpty() ? QPixmap() : QPixmap(path);
    m_scaled = QPixmap();
    viewport()->update();
}

const QPixmap &DesktopArea::scaledWallpaper() const {
    const QSize area = viewport()->size();
    if (m_scaled.size() != area) {
        m_scaled = m_wallpaper.scaled(area, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    }
    return m_scaled;
}

void DesktopArea::paintEvent(QPaintEvent *event) {
    QMdiArea::paintEvent(event);

    if (!m_wallpaper.isNull() && !viewport()->size().isEmpty()) {
        QPainter wall(viewport());
        // Scaled to cover rather than to fit, so there is never a band of
        // desktop colour down an edge; the overhang is trimmed evenly.
        const QPixmap picture = scaledWallpaper();
        const QRect area = viewport()->rect();
        wall.drawPixmap(area, picture,
                        QRect((picture.width() - area.width()) / 2,
                              (picture.height() - area.height()) / 2, area.width(),
                              area.height()));
    }

    if (m_screenName.isEmpty()) return;

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 3.2);
    font.setBold(true);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    painter.setFont(font);

    // Legible against either a dark or a light desktop without competing with
    // the group windows in front of it. A wallpaper has no single colour behind
    // the name to derive from, so over one the name is drawn in white, faint
    // enough to read as part of the picture.
    QColor ink;
    if (m_wallpaper.isNull()) {
        const QColor desktop = background().color();
        ink = desktop.lightnessF() < 0.5 ? desktop.lighter(160) : desktop.darker(130);
        ink.setAlpha(140);
    } else {
        ink = QColor(255, 255, 255, 130);
    }
    painter.setPen(ink);

    // Bottom right, clear of the row of minimized groups along the bottom left
    // and of the cascade of windows opening from the top left.
    const int margin = 16;
    const QRect area = viewport()->rect().adjusted(
        margin, margin, -margin, -(MinimizedGroupIcon::kCellHeight + margin));
    if (!area.isValid()) return;
    painter.drawText(area, Qt::AlignRight | Qt::AlignBottom, m_screenName);
}
