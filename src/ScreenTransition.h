#pragma once

#include <QPixmap>
#include <QWidget>

class QVariantAnimation;

// The slide shown when the desktop moves from one screen to another.
//
// It is a child of the MDI area's viewport, raised above the group windows,
// and it paints two pictures of that viewport: the screen being left and the
// screen arriving, side by side, sliding across until the new one is in place.
// Nothing real is moved, so the new desktop can be built before the animation
// starts and is simply uncovered when it ends.
class ScreenTransition : public QWidget {
    Q_OBJECT
public:
    explicit ScreenTransition(QWidget *parent);

    // `direction` is +1 when the screen arriving comes in from the right, -1
    // when it comes in from the left. Both pictures are grabs of the parent,
    // taken before and after the screen was swapped. A run already under way
    // is replaced.
    void run(const QPixmap &outgoing, const QPixmap &incoming, int direction);

    // Holds one picture over the desktop, still. Used while the screen is
    // swapped underneath: the desktop is torn down and rebuilt, and whatever
    // of that reaches the screen is covered until the slide proper starts.
    void hold(const QPixmap &picture);

    bool running() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_outgoing;
    QPixmap m_incoming;
    // +1 or -1, as given to run().
    int m_direction = 1;
    // 0 at the start of the slide, 1 when the new screen has arrived.
    qreal m_progress = 0.0;
    QVariantAnimation *m_slide = nullptr;
};
