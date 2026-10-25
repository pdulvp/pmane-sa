#include "AlignmentGuides.h"

#include <QPainter>

#include <climits>

#include <algorithm>

namespace {

// The line nearest `position` among `guides`, or -1 when the nearest is further
// away than `within`.
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

// Every line within `within` of `position`, so an edge resting between two
// neighbours' sides shows both rather than only the nearer.
void addGuidesWithin(QVector<int> &values, const QVector<int> &guides, int position,
                     int within) {
    for (int guide : guides) {
        if (std::abs(guide - position) <= within) addOnce(values, guide);
    }
}

} // namespace

AlignmentGuides::AlignmentGuides(QWidget *parent) : QWidget(parent) {
    // An overlay across the whole desktop: it must not intercept pointer events
    // meant for the groups, and it has no background of its own.
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

QRect AlignmentGuides::applyMove(const QRect &rect, int reach) {
    QVector<int> shownXs;
    QVector<int> shownYs;
    QVector<int> heldXs;
    QVector<int> heldYs;

    // Both sides of each axis are considered: a group being moved can align
    // either its left or its right side to a neighbour's. The nearer of the two
    // wins.
    const auto shift = [&](int near, int far, const QVector<int> &guides,
                           QVector<int> &shown, QVector<int> &heldOn) {
        addGuidesWithin(shown, guides, near, kShowDistance);
        addGuidesWithin(shown, guides, far, kShowDistance);

        const int atNear = nearestGuide(guides, near, reach);
        const int atFar = nearestGuide(guides, far, reach);
        const int byNear = atNear >= 0 ? atNear - near : INT_MAX;
        const int byFar = atFar >= 0 ? atFar - far : INT_MAX;
        if (byNear == INT_MAX && byFar == INT_MAX) return 0;

        if (qAbs(byNear) <= qAbs(byFar)) {
            addOnce(heldOn, atNear);
            return byNear;
        }
        addOnce(heldOn, atFar);
        return byFar;
    };

    // A rectangle's right() is its last pixel, so the line its right side sits
    // on is one past it, which is the convention the resize path uses.
    const int dx = shift(rect.left(), rect.right() + 1, m_xs, shownXs, heldXs);
    const int dy = shift(rect.top(), rect.bottom() + 1, m_ys, shownYs, heldYs);

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
        raise();
        show();
    }

    // A move, never a resize: the size is unchanged.
    return rect.translated(dx, dy);
}

QRect AlignmentGuides::apply(const QRect &rect, Qt::Edges edges, const QSize &minimumSize,
                             int reach) {
    QRect held = rect;
    QVector<int> shownXs;
    QVector<int> shownYs;
    QVector<int> heldXs;
    QVector<int> heldYs;

    // Each side snaps independently: a corner drag meets a vertical and a
    // horizontal line separately, and either may find none. A right or bottom
    // edge aligns to the line and so sits one pixel inside it, a rectangle's
    // right() being its last pixel rather than the one after.
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
        // The group windows are siblings, and this has to draw in front of them.
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

    // The theme's selection colour, the one colour in a scheme meant to stand
    // out against both the desktop and a window interior.
    const QColor base = palette().color(QPalette::Highlight);
    QColor near = base;
    near.setAlpha(kNearAlpha);
    QColor holding = base;
    holding.setAlpha(kHeldAlpha);

    // A band centred on the line, so the position an edge snaps to is the middle
    // of what is drawn rather than one of its sides.
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
