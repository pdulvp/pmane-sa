#include "TitleBar.h"

#include "WindowEdges.h"

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace {

// Caption buttons are square and a little taller than the menu text, the
// way a window manager would have drawn them.
constexpr int kButtonWidth = 34;
constexpr int kButtonHeight = 24;
// Keeps the title clear of the menus and the clock on either side.
constexpr int kTitleMargin = 12;
// Room around the clock, so it belongs to neither the title nor the buttons.
constexpr int kClockMargin = 10;
// The short time format has no seconds in it, but the minute still has to
// turn over promptly, and the system clock can be set while we watch.
constexpr int kClockInterval = 1000;

QToolButton *captionButton(QWidget *parent, QStyle::StandardPixmap glyph) {
    auto *button = new QToolButton(parent);
    button->setIcon(parent->style()->standardIcon(glyph, nullptr, parent));
    button->setFixedSize(kButtonWidth, kButtonHeight);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
    m_menuBar = new QMenuBar(this);
    // Only as wide as the menus themselves: the rest of the strip is this
    // widget's own surface, which is what makes it draggable.
    m_menuBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_clock = new QLabel(this);
    m_clock->setContentsMargins(kClockMargin, 0, kClockMargin, 0);
    m_clock->setAlignment(Qt::AlignCenter);
    updateClock();

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &TitleBar::updateClock);
    clockTimer->start(kClockInterval);

    m_minimizeButton = captionButton(this, QStyle::SP_TitleBarMinButton);
    m_maximizeButton = captionButton(this, QStyle::SP_TitleBarMaxButton);
    m_closeButton = captionButton(this, QStyle::SP_TitleBarCloseButton);

    m_minimizeButton->setToolTip(tr("Minimize"));
    m_closeButton->setToolTip(tr("Close"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_menuBar);
    layout->addStretch();
    layout->addWidget(m_clock);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    connect(m_minimizeButton, &QToolButton::clicked, this,
            [this] { window()->showMinimized(); });
    connect(m_maximizeButton, &QToolButton::clicked, this, &TitleBar::toggleMaximized);
    connect(m_closeButton, &QToolButton::clicked, this, [this] { window()->close(); });

    setMouseTracking(true);
}

void TitleBar::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!m_watchingWindow && window()) {
        window()->installEventFilter(this);
        m_watchingWindow = true;
        updateMaximizeButton();
    }
}

void TitleBar::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);

    const QString title = window() ? window()->windowTitle() : QString();
    if (title.isEmpty()) return;

    QRect area = titleArea();
    if (area.width() <= 0) return;

    const QString shown = fontMetrics().elidedText(title, Qt::ElideRight, area.width());
    const int shownWidth = fontMetrics().horizontalAdvance(shown);

    // Centred on the window, the way a caption sits, as long as it clears
    // the menus and the buttons; centred in the room left over when not.
    const int centred = (width() - shownWidth) / 2;
    if (centred >= area.left() && centred + shownWidth <= area.right()) {
        area = QRect(centred, 0, shownWidth, height());
    }

    QPainter painter(this);
    painter.setPen(palette().color(isActiveWindow() ? QPalette::Active : QPalette::Inactive,
                                   QPalette::WindowText));
    painter.drawText(area, Qt::AlignCenter, shown);
}

QRect TitleBar::titleArea() const {
    const int left = m_menuBar->geometry().right() + kTitleMargin;
    const int right = m_clock->geometry().left() - kTitleMargin;
    return QRect(left, 0, right - left, height());
}

void TitleBar::updateClock() {
    const QLocale locale = QLocale::system();
    const QDateTime now = QDateTime::currentDateTime();
    // Whatever this system's idea of a short time and a short date is; the
    // label ignores a text it is already showing, so the minutes in between
    // cost nothing.
    m_clock->setText(QStringLiteral("%1   %2")
                         .arg(locale.toString(now.time(), QLocale::ShortFormat),
                              locale.toString(now.date(), QLocale::ShortFormat)));
    m_clock->setToolTip(locale.toString(now.date(), QLocale::LongFormat));
}

void TitleBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // The window's own top edge runs along the top of this strip.
    const Qt::Edges edges = windowEdgesAt(window(), event->globalPosition().toPoint());
    if (edges && startWindowResize(window(), edges)) {
        event->accept();
        return;
    }

    // Anywhere else on the bare strip drags the window, which the window
    // manager carries out so that snapping and the pointer grab behave --
    // unless the window fills the screen, which is not something to be
    // dragged out of by accident.
    if (windowCanBeMoved(window())) {
        if (QWindow *handle = window()->windowHandle()) {
            if (handle->startSystemMove()) {
                event->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        toggleMaximized();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent *event) {
    const Qt::Edges edges = windowEdgesAt(window(), event->globalPosition().toPoint());
    setCursor(cursorForWindowEdges(edges));
    QWidget::mouseMoveEvent(event);
}

void TitleBar::toggleMaximized() {
    QWidget *win = window();
    if (!win) return;
    if (win->isMaximized()) {
        win->showNormal();
    } else {
        win->showMaximized();
    }
}

void TitleBar::updateMaximizeButton() {
    const bool maximized = window() && window()->isMaximized();
    m_maximizeButton->setIcon(style()->standardIcon(
        maximized ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton, nullptr,
        this));
    m_maximizeButton->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == window()) {
        switch (event->type()) {
        case QEvent::WindowTitleChange:
        case QEvent::ActivationChange:
            update();
            break;
        case QEvent::WindowStateChange:
            updateMaximizeButton();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
