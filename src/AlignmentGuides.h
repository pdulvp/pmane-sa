#pragma once

#include <QRect>
#include <QVector>
#include <QWidget>

// The lines a group's edges align to while it is dragged: the sides of the
// surrounding groups, drawn across the desktop as a dragged edge comes near
// one, and snapping that edge to the line once it is nearer still.
//
// Only the lines within reach of the dragged edge are drawn: a desktop full of
// groups would otherwise show a thicket of them, and only the nearest matters.
//
// It is a widget rather than something the desktop paints so that it draws in
// front of the group windows, which are children of the same viewport; the
// desktop's background is behind them all.
class AlignmentGuides : public QWidget {
    Q_OBJECT
public:
    // How near a dragged edge has to come for its line to be drawn, and how
    // near for the edge to snap to it during the drag. The line is drawn from
    // further off than it snaps, so it appears before the snap happens.
    static constexpr int kShowDistance = 20;
    // A band rather than a hairline, centred on the position it marks, and
    // translucent enough to see a group's icons through. A line merely within
    // reach is faint; the one the edge is snapped to is solid, so the two are
    // distinguishable while dragging.
    static constexpr int kLineWidth = 3;
    static constexpr int kNearAlpha = 90;
    static constexpr int kHeldAlpha = 230;
    static constexpr int kSnapDistance = 10;

    explicit AlignmentGuides(QWidget *parent = nullptr);

    // The sides of the other groups, as x and y positions in the parent's
    // coordinates. Taken once when a drag starts, so a group is never offered
    // its own sides.
    void setGuides(const QVector<int> &xs, const QVector<int> &ys);
    // Snaps each dragged edge of `rect` to a guide within reach, draws the
    // guides within reach of it, and returns the snapped rectangle. `edges` are
    // the sides being dragged; `minimumSize` is the window's minimum, below
    // which a snap is skipped. `reach` is how near an edge has to be to snap:
    // the default is the reach used during the drag, while a release passes the
    // wider one, so an edge released while its line shows lands on it. A
    // negative reach draws the lines and snaps nothing, which is what Shift
    // during a resize does.
    QRect apply(const QRect &rect, Qt::Edges edges, const QSize &minimumSize,
                int reach = kSnapDistance);
    // The same for a group being moved rather than resized: all four sides move
    // together, so the whole rectangle is shifted onto whichever lines its sides
    // come near and its size is unchanged. Either axis may snap without the
    // other.
    QRect applyMove(const QRect &rect, int reach = kSnapDistance);

    // Called when the drag ends: the lines are hidden and the guides dropped.
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<int> m_xs;       // every available line
    QVector<int> m_ys;
    QVector<int> m_shownXs;  // those within reach, which are the ones drawn
    QVector<int> m_shownYs;
    QVector<int> m_heldXs;   // of those, the ones an edge is snapped to
    QVector<int> m_heldYs;
};
