#include "MinimizedGroupIcon.h"

#include <QContextMenuEvent>
#include "Theme.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int kGlyphWidth = 46;
// The height shrinks with the caption so the client area, and the preview icons
// in it, keep their intended size.
constexpr int kGlyphHeight = 34;
constexpr int kGlyphTop = 3;
constexpr int kCaptionHeight = 5;

// A miniature of the group window: a caption bar in the scheme's title colour,
// with the group's program icons laid out inside.
void paintGroupGlyph(QPainter &p, const QRect &r, const QPalette &pal,
                     const QVector<QIcon> &preview) {
    const QColor line = pal.buttonText().color();

    p.fillRect(r, pal.button());
    p.setPen(line);
    p.drawRect(r.adjusted(0, 0, -1, -1));

    const QRect caption(r.x() + 2, r.y() + 2, r.width() - 4, kCaptionHeight);
    p.fillRect(caption, pal.highlight());

    const QRect client(r.x() + 2, caption.bottom() + 1, r.width() - 4,
                       r.bottom() - caption.bottom() - 3);
    p.fillRect(client, pal.base());

    if (preview.isEmpty()) return;

    const int cellW = client.width() / MinimizedGroupIcon::kPreviewColumns;
    const int cellH = client.height() / MinimizedGroupIcon::kPreviewRows;
    const int iconSize = qMax(4, qMin(cellW, cellH) - 3);

    for (int i = 0; i < preview.size() && i < MinimizedGroupIcon::kMaxPreviewIcons; ++i) {
        const int col = i % MinimizedGroupIcon::kPreviewColumns;
        const int row = i / MinimizedGroupIcon::kPreviewColumns;
        const QRect cell(client.x() + col * cellW, client.y() + row * cellH, cellW, cellH);
        const QRect target(cell.x() + (cell.width() - iconSize) / 2,
                           cell.y() + (cell.height() - iconSize) / 2, iconSize, iconSize);
        preview[i].paint(&p, target, Qt::AlignCenter);
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

void MinimizedGroupIcon::setSelected(bool selected) {
    if (m_selected == selected) return;
    m_selected = selected;
    update();
}

void MinimizedGroupIcon::setDesktopColor(const QColor &color) {
    m_desktop = color;
    update();
}

void MinimizedGroupIcon::setPreviewIcons(const QVector<QIcon> &icons) {
    m_preview = icons;
    update();
}

void MinimizedGroupIcon::paintEvent(QPaintEvent *) {
    QPainter p(this);

    const QRect glyph((width() - kGlyphWidth) / 2, kGlyphTop, kGlyphWidth, kGlyphHeight);
    paintGroupGlyph(p, glyph, palette(), m_preview);

    // The label has to stay legible against whatever colour the desktop is.
    const QColor textColor = readableTextOn(m_desktop);

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
    setSelected(true);
    emit clicked();
    // Accepted here: propagating it would reach the desktop behind, which
    // treats a press as a click on bare background and would clear this
    // selection.
    event->accept();
}

void MinimizedGroupIcon::mouseDoubleClickEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    emit restoreRequested();
}

void MinimizedGroupIcon::contextMenuEvent(QContextMenuEvent *event) {
    // Selected as well, so the menu clearly applies to this icon rather than to
    // the one last clicked.
    setSelected(true);
    emit clicked();
    emit contextMenuRequested(event->globalPos());
    event->accept();
}
