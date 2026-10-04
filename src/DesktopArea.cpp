#include "DesktopArea.h"

#include "MinimizedGroupIcon.h"

#include <QPainter>
#include <QPaintEvent>

DesktopArea::DesktopArea(QWidget *parent) : QMdiArea(parent) {
    // A group window dragged off the edge, or left out there when the
    // Program Manager's own window was made smaller, is still on the
    // desktop: the bars are how it is reached again. Progman's own client
    // area scrolled the same way.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void DesktopArea::scrollContentsBy(int dx, int dy) {
    QMdiArea::scrollContentsBy(dx, dy);
    // Scrolling blits what was already drawn and repaints only the strip
    // that came into view, which would carry the wallpaper and the screen's
    // name along with it. Both belong to the viewport rather than to what
    // is arranged on it, so the whole of it is drawn again.
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
        // Covered rather than fitted, so there is never a band of desktop
        // colour down an edge; what overhangs is trimmed evenly.
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

    // Legible against either a dark or a light desktop without ever
    // competing with the group windows in front of it. A wallpaper has no
    // one colour behind the name to be derived from, so there it is written
    // in white, faint enough to read as part of the picture.
    QColor ink;
    if (m_wallpaper.isNull()) {
        const QColor desktop = background().color();
        ink = desktop.lightnessF() < 0.5 ? desktop.lighter(160) : desktop.darker(130);
        ink.setAlpha(140);
    } else {
        ink = QColor(255, 255, 255, 130);
    }
    painter.setPen(ink);

    // Bottom right, clear of the row of minimized groups along the bottom
    // left, and clear of the cascade of windows opening from the top left.
    const int margin = 16;
    const QRect area = viewport()->rect().adjusted(
        margin, margin, -margin, -(MinimizedGroupIcon::kCellHeight + margin));
    if (!area.isValid()) return;
    painter.drawText(area, Qt::AlignRight | Qt::AlignBottom, m_screenName);
}
