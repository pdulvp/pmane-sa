#include "ScreenTransition.h"

#include <QPainter>
#include <QVariantAnimation>

namespace {
// Long enough for the direction to register, short enough that stepping
// through several screens in a row is not a wait.
constexpr int kSlideMs = 260;
} // namespace

ScreenTransition::ScreenTransition(QWidget *parent) : QWidget(parent) {
    // Clicks and wheel steps during the slide belong to the desktop behind it,
    // which is already the new screen.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // Every pixel is painted from the two pictures, so Qt has no reason to
    // repaint what is underneath. Without this, each frame repaints the
    // viewport and every group window on the screen arriving, which is what
    // the slide is covering up anyway.
    setAttribute(Qt::WA_OpaquePaintEvent);
    hide();

    m_slide = new QVariantAnimation(this);
    m_slide->setDuration(kSlideMs);
    m_slide->setStartValue(0.0);
    m_slide->setEndValue(1.0);
    // Fast at first and settling at the end, so the new screen appears to come
    // to rest rather than to stop dead.
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                m_progress = value.toReal();
                update();
            });
    connect(m_slide, &QVariantAnimation::finished, this, [this]() {
        hide();
        // The pictures are a screenful each; there is no reason to hold two of
        // them between slides.
        m_outgoing = QPixmap();
        m_incoming = QPixmap();
    });
}

bool ScreenTransition::running() const {
    return m_slide->state() == QAbstractAnimation::Running;
}

void ScreenTransition::hold(const QPixmap &picture) {
    m_slide->stop();
    m_outgoing = picture;
    m_incoming = QPixmap();
    m_progress = 0.0;
    if (!parentWidget() || picture.isNull()) {
        hide();
        return;
    }
    setGeometry(parentWidget()->rect());
    show();
    raise();
    update();
}

void ScreenTransition::run(const QPixmap &outgoing, const QPixmap &incoming,
                           int direction) {
    m_slide->stop();

    m_outgoing = outgoing;
    m_incoming = incoming;
    m_direction = direction < 0 ? -1 : 1;
    m_progress = 0.0;

    if (!parentWidget() || m_incoming.isNull()) {
        hide();
        return;
    }

    setGeometry(parentWidget()->rect());
    show();
    raise();
    m_slide->start();
}

void ScreenTransition::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);

    // The widget paints every pixel of itself, so a picture that no longer
    // matches its size -- the viewport having narrowed for a scroll bar
    // between the grab and now -- gets a ground under it rather than
    // whatever the buffer held.
    if (m_outgoing.deviceIndependentSize().toSize() != size() ||
        (!m_incoming.isNull() && m_incoming.deviceIndependentSize().toSize() != size())) {
        painter.fillRect(rect(), palette().window());
    }

    // Held still over a screen being swapped: the one picture covers
    // everything, and nothing moves until run() is called.
    if (m_incoming.isNull()) {
        if (!m_outgoing.isNull()) painter.drawPixmap(QPoint(0, 0), m_outgoing);
        return;
    }

    const int span = width();
    // The pair of pictures travels as one strip: the screen arriving starts a
    // full width off to one side and ends at nought.
    const int shift = int(qRound(m_progress * span));
    const int incomingX = m_direction > 0 ? span - shift : shift - span;
    const int outgoingX = incomingX - m_direction * span;

    // Drawn at a point rather than into a rectangle: a rectangle rescales both
    // pictures on every frame, a point is a straight blit at the size they
    // were grabbed, which is this size already.
    if (!m_outgoing.isNull()) painter.drawPixmap(QPoint(outgoingX, 0), m_outgoing);
    painter.drawPixmap(QPoint(incomingX, 0), m_incoming);
}
