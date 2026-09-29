#include "DesktopArea.h"

#include "MinimizedGroupIcon.h"

#include <QPainter>
#include <QPaintEvent>

DesktopArea::DesktopArea(QWidget *parent) : QMdiArea(parent) {}

void DesktopArea::setProfileName(const QString &name) {
    if (m_profileName == name) return;
    m_profileName = name;
    viewport()->update();
}

void DesktopArea::paintEvent(QPaintEvent *event) {
    QMdiArea::paintEvent(event);
    if (m_profileName.isEmpty()) return;

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 3.2);
    font.setBold(true);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    painter.setFont(font);

    // Legible against either a dark or a light desktop without ever
    // competing with the group windows in front of it.
    const QColor desktop = background().color();
    QColor ink = desktop.lightnessF() < 0.5 ? desktop.lighter(160) : desktop.darker(130);
    ink.setAlpha(140);
    painter.setPen(ink);

    // Bottom right, clear of the row of minimized groups along the bottom
    // left, and clear of the cascade of windows opening from the top left.
    const int margin = 16;
    const QRect area = viewport()->rect().adjusted(
        margin, margin, -margin, -(MinimizedGroupIcon::kCellHeight + margin));
    if (!area.isValid()) return;
    painter.drawText(area, Qt::AlignRight | Qt::AlignBottom, m_profileName);
}
