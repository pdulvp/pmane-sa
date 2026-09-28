#include "MinimizedGroupIcon.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int kGlyph = 32;
constexpr int kGlyphTop = 4;

// Progman's group icon: a little window with a caption bar and a couple of
// program icons inside it.
void paintGroupGlyph(QPainter &p, const QRect &r, const QPalette &pal) {
    const QColor face = pal.button().color();
    const QColor caption = pal.highlight().color();
    const QColor line = pal.buttonText().color();

    p.fillRect(r, face);
    p.setPen(line);
    p.drawRect(r.adjusted(0, 0, -1, -1));

    const QRect captionBar(r.x() + 2, r.y() + 2, r.width() - 4, 6);
    p.fillRect(captionBar, caption);

    // Two miniature program icons sitting in the group.
    const int size = 7;
    const int top = captionBar.bottom() + 4;
    const QRect a(r.x() + 5, top, size, size);
    const QRect b(r.x() + 17, top, size, size);
    for (const QRect &icon : {a, b}) {
        p.fillRect(icon, pal.base().color());
        p.setPen(line);
        p.drawRect(icon);
    }
}

} // namespace

MinimizedGroupIcon::MinimizedGroupIcon(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title) {
    setFixedSize(kCellWidth, kCellHeight);
    setCursor(Qt::ArrowCursor);
    setToolTip(title);
}

void MinimizedGroupIcon::setTitle(const QString &title) {
    m_title = title;
    setToolTip(title);
    update();
}

void MinimizedGroupIcon::setDesktopColor(const QColor &color) {
    m_desktop = color;
    update();
}

void MinimizedGroupIcon::paintEvent(QPaintEvent *) {
    QPainter p(this);

    const QRect glyph((width() - kGlyph) / 2, kGlyphTop, kGlyph, kGlyph);
    paintGroupGlyph(p, glyph, palette());

    // The label has to stay readable against whatever colour the desktop is,
    // so pick black or white from its luminance rather than a fixed colour.
    const bool lightDesktop = m_desktop.isValid() && m_desktop.lightnessF() > 0.55;
    const QColor textColor = lightDesktop ? Qt::black : Qt::white;

    const QRect labelArea(2, glyph.bottom() + 3, width() - 4, height() - glyph.bottom() - 5);
    const int flags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
    const QRect bounds = p.fontMetrics().boundingRect(labelArea, flags, m_title);

    if (m_selected) {
        p.fillRect(bounds.adjusted(-2, -1, 2, 1), palette().highlight());
        p.setPen(palette().highlightedText().color());
    } else {
        p.setPen(textColor);
    }
    p.drawText(labelArea, flags, m_title);
}

void MinimizedGroupIcon::mousePressEvent(QMouseEvent *event) {
    m_selected = true;
    update();
    QWidget::mousePressEvent(event);
}

void MinimizedGroupIcon::mouseDoubleClickEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    emit restoreRequested();
}
