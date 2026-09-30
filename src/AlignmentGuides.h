#pragma once

#include <QRect>
#include <QVector>
#include <QWidget>

// The lines a group's edges line up with while it is being resized: the
// sides of the groups around it, drawn across the desktop as a dragged edge
// comes near one, and holding that edge on the line once it is nearer still.
//
// Only the lines within reach of the edge being dragged are drawn: a desk
// full of groups would otherwise be a cage of them, and the one that matters
// is the one about to be met.
//
// It is a widget rather than something the desktop paints so that it comes
// out in front of the group windows: they are children of the same viewport,
// and the desktop's own background is behind them all.
class AlignmentGuides : public QWidget {
    Q_OBJECT
public:
    // How near a dragged edge has to come for its line to be drawn, and how
    // near for the edge to be held on it while the drag is under way. Drawn
    // from further off than it is held, so the line arrives first and the
    // hold is never a surprise.
    static constexpr int kShowDistance = 20;
    // A band rather than a hairline, drawn centred on the position it stands
    // for, and see-through enough to read a group's icons through. A line
    // merely within reach is faint; the one the edge is actually being held
    // on comes up solid, so which is which is readable while dragging.
    static constexpr int kLineWidth = 3;
    static constexpr int kNearAlpha = 90;
    static constexpr int kHeldAlpha = 230;
    static constexpr int kSnapDistance = 10;

    explicit AlignmentGuides(QWidget *parent = nullptr);

    // The sides of the other groups, as x and y positions in the parent's
    // coordinates. Taken once when a resize starts, so that a group is never
    // offered its own sides.
    void setGuides(const QVector<int> &xs, const QVector<int> &ys);
    // Holds each dragged edge of `rect` to a guide within reach and draws
    // the guides in reach of it, returning the held rectangle. `edges` are
    // the sides being dragged; `minimumSize` is what the window will not go
    // below, a hold that would shrink it further being left alone.
    // `reach` is how near an edge has to be to be held; the default is the
    // hold that happens under the pointer, and letting go asks for the wider
    // one, so an edge let go of while its line is showing lands on it. A
    // negative reach draws the lines and holds nothing, which is what Shift
    // during a resize asks for.
    QRect apply(const QRect &rect, Qt::Edges edges, const QSize &minimumSize,
                int reach = kSnapDistance);
    // The resize is over: the lines go away and the guides are forgotten.
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<int> m_xs;       // every line on offer
    QVector<int> m_ys;
    QVector<int> m_shownXs;  // the ones within reach, which are the ones drawn
    QVector<int> m_shownYs;
    QVector<int> m_heldXs;   // and of those, the ones an edge is sitting on
    QVector<int> m_heldYs;
};
