#include "AlignmentGuides.h"

#include <QPainter>

#include <algorithm>

namespace {

// The line nearest `position` among `guides`, or -1 when the nearest is
// further off than `within`.
int nearestGuide(const QVector<int> &guides, int position, int within) {
    int best = -1;
    int bestDistance = within + 1;
    for (int guide : guides) {
        const int distance = std::abs(guide - position);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = guide;
        }
    }
    return bestDistance <= within ? best : -1;
}

void addOnce(QVector<int> &values, int value) {
    if (value >= 0 && !values.contains(value)) values.append(value);
}

} // namespace

AlignmentGuides::AlignmentGuides(QWidget *parent) : QWidget(parent) {
    // A pane over the whole desktop: it must not stand between the groups
    // and the pointer, and it has nothing of its own to fill.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void AlignmentGuides::setGuides(const QVector<int> &xs, const QVector<int> &ys) {
    m_xs = xs;
    m_ys = ys;
    m_shownXs.clear();
    m_shownYs.clear();
}

QRect AlignmentGuides::apply(const QRect &rect, Qt::Edges edges, const QSize &minimumSize,
                             int reach) {
    QRect held = rect;
    QVector<int> shownXs;
    QVector<int> shownYs;

    // Each side is held on its own: a corner drag meets a vertical line and
    // a horizontal one independently, and either may find nothing.
    if (edges & Qt::LeftEdge) {
        addOnce(shownXs, nearestGuide(m_xs, rect.left(), kShowDistance));
        const int guide = nearestGuide(m_xs, rect.left(), reach);
        if (guide >= 0 && rect.right() - guide + 1 >= minimumSize.width()) held.setLeft(guide);
    }
    if (edges & Qt::RightEdge) {
        // A right edge lines up with the line, so it sits one pixel inside
        // it: a rectangle's right() is its last pixel, not the one after.
        addOnce(shownXs, nearestGuide(m_xs, rect.right() + 1, kShowDistance));
        const int guide = nearestGuide(m_xs, rect.right() + 1, reach);
        if (guide >= 0 && guide - rect.left() >= minimumSize.width()) held.setRight(guide - 1);
    }
    if (edges & Qt::TopEdge) {
        addOnce(shownYs, nearestGuide(m_ys, rect.top(), kShowDistance));
        const int guide = nearestGuide(m_ys, rect.top(), reach);
        if (guide >= 0 && rect.bottom() - guide + 1 >= minimumSize.height()) held.setTop(guide);
    }
    if (edges & Qt::BottomEdge) {
        addOnce(shownYs, nearestGuide(m_ys, rect.bottom() + 1, kShowDistance));
        const int guide = nearestGuide(m_ys, rect.bottom() + 1, reach);
        if (guide >= 0 && guide - rect.top() >= minimumSize.height()) held.setBottom(guide - 1);
    }

    if (shownXs != m_shownXs || shownYs != m_shownYs) {
        m_shownXs = shownXs;
        m_shownYs = shownYs;
        update();
    }

    if (m_shownXs.isEmpty() && m_shownYs.isEmpty()) {
        hide();
    } else if (parentWidget()) {
        setGeometry(parentWidget()->rect());
        // The group windows are siblings, and this belongs in front of them.
        raise();
        show();
    }
    return held;
}

void AlignmentGuides::clear() {
    m_xs.clear();
    m_ys.clear();
    m_shownXs.clear();
    m_shownYs.clear();
    hide();
}

void AlignmentGuides::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);

    // The desktop's highlight, which every theme keeps clear of both its
    // window and its background colours.
    QColor ink = palette().color(QPalette::Highlight);
    ink.setAlpha(200);
    QPen pen(ink);
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);

    for (int x : m_shownXs) painter.drawLine(x, 0, x, height());
    for (int y : m_shownYs) painter.drawLine(0, y, width(), y);
}
