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

// Every line within `within` of `position`, so that an edge coming to rest
// between two neighbours' sides sees both of them and not just the nearer.
void addGuidesWithin(QVector<int> &values, const QVector<int> &guides, int position,
                     int within) {
    for (int guide : guides) {
        if (std::abs(guide - position) <= within) addOnce(values, guide);
    }
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
    m_heldXs.clear();
    m_heldYs.clear();
}

QRect AlignmentGuides::apply(const QRect &rect, Qt::Edges edges, const QSize &minimumSize,
                             int reach) {
    QRect held = rect;
    QVector<int> shownXs;
    QVector<int> shownYs;
    QVector<int> heldXs;
    QVector<int> heldYs;

    // Each side is held on its own: a corner drag meets a vertical line and
    // a horizontal one independently, and either may find nothing. A right or
    // bottom edge lines up with the line, so it sits one pixel inside it: a
    // rectangle's right() is its last pixel, not the one after.
    if (edges & Qt::LeftEdge) {
        addGuidesWithin(shownXs, m_xs, rect.left(), kShowDistance);
        const int guide = nearestGuide(m_xs, rect.left(), reach);
        if (guide >= 0 && rect.right() - guide + 1 >= minimumSize.width()) {
            held.setLeft(guide);
            addOnce(heldXs, guide);
        }
    }
    if (edges & Qt::RightEdge) {
        addGuidesWithin(shownXs, m_xs, rect.right() + 1, kShowDistance);
        const int guide = nearestGuide(m_xs, rect.right() + 1, reach);
        if (guide >= 0 && guide - rect.left() >= minimumSize.width()) {
            held.setRight(guide - 1);
            addOnce(heldXs, guide);
        }
    }
    if (edges & Qt::TopEdge) {
        addGuidesWithin(shownYs, m_ys, rect.top(), kShowDistance);
        const int guide = nearestGuide(m_ys, rect.top(), reach);
        if (guide >= 0 && rect.bottom() - guide + 1 >= minimumSize.height()) {
            held.setTop(guide);
            addOnce(heldYs, guide);
        }
    }
    if (edges & Qt::BottomEdge) {
        addGuidesWithin(shownYs, m_ys, rect.bottom() + 1, kShowDistance);
        const int guide = nearestGuide(m_ys, rect.bottom() + 1, reach);
        if (guide >= 0 && guide - rect.top() >= minimumSize.height()) {
            held.setBottom(guide - 1);
            addOnce(heldYs, guide);
        }
    }

    if (shownXs != m_shownXs || shownYs != m_shownYs || heldXs != m_heldXs ||
        heldYs != m_heldYs) {
        m_shownXs = shownXs;
        m_shownYs = shownYs;
        m_heldXs = heldXs;
        m_heldYs = heldYs;
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
    m_heldXs.clear();
    m_heldYs.clear();
    hide();
}

void AlignmentGuides::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);

    // The theme's selection colour: the one colour in a scheme that is meant
    // to stand out against both the desktop and a window interior.
    const QColor base = palette().color(QPalette::Highlight);
    QColor near = base;
    near.setAlpha(kNearAlpha);
    QColor holding = base;
    holding.setAlpha(kHeldAlpha);

    // A band centred on the line, so what the edge is held to is the middle
    // of what is drawn rather than one side of it.
    const qreal half = kLineWidth / 2.0;
    for (int x : m_shownXs) {
        painter.fillRect(QRectF(x - half, 0, kLineWidth, height()),
                         m_heldXs.contains(x) ? holding : near);
    }
    for (int y : m_shownYs) {
        painter.fillRect(QRectF(0, y - half, width(), kLineWidth),
                         m_heldYs.contains(y) ? holding : near);
    }
}
