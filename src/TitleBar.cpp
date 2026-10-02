#include "TitleBar.h"

#include "CalendarPane.h"
#include "DesktopShell.h"
#include "SystemVolume.h"
#include "Theme.h"

#include "WindowEdges.h"

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QProxyStyle>
#include <QSlider>
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

// The volume slider: long enough to aim with, short enough to leave the
// title the middle of the strip.
constexpr int kVolumeWidth = 135;
constexpr int kVolumeMargin = 6;
// How often the mixer is asked, for the volume keys and the shell's own
// slider moving it behind our back.
constexpr int kVolumeInterval = 3000;


// The short time format has no seconds in it, but the minute still has to
// turn over promptly, and the system clock can be set while we watch.
constexpr int kClockInterval = 1000;

// A slider that goes where it is clicked. Left alone, a click on the groove
// nudges the handle one page towards the pointer, which is right for a
// scrollbar and wrong for a volume: the pointer is already on the loudness
// that was wanted.
//
// Said through the style rather than by catching the click, so the handle,
// the drag that follows and the keyboard all keep working as they are.
class ClickToSetSlider : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override {
        if (hint == SH_Slider_AbsoluteSetButtons) return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

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
    // The strip is one colour throughout. A menu bar paints a panel of its
    // own, which most styles shade a little darker than the window behind
    // it, and that left a faint band behind the menus; without the panel the
    // menus sit straight on this widget's background.
    m_menuBar->setStyleSheet(QStringLiteral("QMenuBar { background: transparent; }"));

    // Only where there is a mixer to talk to.
    if (systemVolumeAvailable()) {
        m_volumeButton = captionButton(this, QStyle::SP_MediaVolume);
        m_volumeButton->setToolTip(tr("Mute"));

        m_volumeSlider = new QSlider(Qt::Horizontal, this);
        m_volumeSlider->setRange(0, 100);
        m_volumeSlider->setFixedWidth(kVolumeWidth);
        // The strip is dragged from, so a slider on it must not be
        // something the keyboard wanders into.
        m_volumeSlider->setFocusPolicy(Qt::NoFocus);
        m_volumeSlider->setToolTip(tr("Output volume"));
        // Parented to the slider so it lives as long as it does; the proxy
        // takes the application style as its base without owning it.
        auto *clickToSet = new ClickToSetSlider;
        clickToSet->setParent(m_volumeSlider);
        m_volumeSlider->setStyle(clickToSet);

        m_volumeWatch = new QTimer(this);
        m_volumeWatch->setInterval(kVolumeInterval);
        connect(m_volumeWatch, &QTimer::timeout, this, &TitleBar::refreshVolume);
        m_volumeWatch->start();
        refreshVolume();
    }

    m_clock = new QLabel(this);
    m_clock->setContentsMargins(kClockMargin, 0, kClockMargin, 0);
    m_clock->setAlignment(Qt::AlignCenter);
    // Clicking the date opens the month under it, the way the desktop's own
    // clock does. The strip is dragged from, so the clock has to say that it
    // is something to click.
    m_clock->setCursor(Qt::PointingHandCursor);
    m_clock->setToolTip(tr("Show the month"));
    m_clock->installEventFilter(this);
    updateClock();

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &TitleBar::updateClock);
    clockTimer->start(kClockInterval);

    // Only where there is a shell to hand the search to: a button that
    // could do nothing would be worse than no button.
    if (desktopSearchAvailable()) {
        m_searchButton = captionButton(this, QStyle::SP_FileDialogContentsView);
        m_searchButton->setToolTip(tr("Search the desktop"));
        // The icon itself is set in applyPalette(), which is where the ink
        // the strip is readable in is known.
    }

    m_minimizeButton = captionButton(this, QStyle::SP_TitleBarMinButton);
    m_maximizeButton = captionButton(this, QStyle::SP_TitleBarMaxButton);
    m_closeButton = captionButton(this, QStyle::SP_TitleBarCloseButton);

    m_minimizeButton->setToolTip(tr("Minimize"));
    m_closeButton->setToolTip(tr("Close"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // Before the menus, the way a file manager puts its search at the start
    // of the bar rather than among the window's own buttons.
    if (m_searchButton) layout->addWidget(m_searchButton);
    layout->addWidget(m_menuBar);
    layout->addStretch();
    if (m_volumeButton) {
        layout->addWidget(m_volumeButton);
        layout->addWidget(m_volumeSlider);
        layout->addSpacing(kVolumeMargin);
    }
    layout->addWidget(m_clock);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    if (m_searchButton) {
        connect(m_searchButton, &QToolButton::clicked, this, [] { openDesktopSearch(); });
    }

    if (m_volumeSlider) {
        // While a slider is being dragged the mixer is told as it moves, so
        // what is heard follows the hand. The watch is held off meanwhile:
        // reading back mid-drag would fight whoever is dragging.
        connect(m_volumeSlider, &QSlider::sliderPressed, this,
                [this] { m_volumeWatch->stop(); });
        connect(m_volumeSlider, &QSlider::sliderReleased, this,
                [this] { m_volumeWatch->start(); });
        connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
            setSystemVolume(value / 100.0);
            // Unmuting is what moving the slider means, mute being a way of
            // saying "not now" rather than "zero".
            if (value > 0 && systemVolumeMuted()) setSystemVolumeMuted(false);
            updateVolumeIcon(value, false);
        });
        connect(m_volumeButton, &QToolButton::clicked, this, [this] {
            setSystemVolumeMuted(!systemVolumeMuted());
            refreshVolume();
        });
    }

    connect(m_minimizeButton, &QToolButton::clicked, this,
            [this] { window()->showMinimized(); });
    connect(m_maximizeButton, &QToolButton::clicked, this, &TitleBar::toggleMaximized);
    connect(m_closeButton, &QToolButton::clicked, this, [this] { window()->close(); });

    setMouseTracking(true);
    // A plain widget paints no background of its own, and the window behind
    // it would show through in the ordinary window colour: the strip carries
    // a colour of its own, so it has to fill.
    setAutoFillBackground(true);
}

void TitleBar::applyPalette(const QPalette &palette) {
    setPalette(palette);
    m_menuBar->setPalette(palette);
    m_clock->setPalette(palette);
    // The strip's own ink, which is black or white depending on how dark the
    // desktop colour is: the icons on it have to be drawn in the same.
    const QColor ink = palette.color(QPalette::WindowText);
    if (m_searchButton) {
        m_searchButton->setPalette(palette);
        const QIcon glass = tintedIcon({QStringLiteral("system-search-symbolic"),
                                        QStringLiteral("edit-find-symbolic"),
                                        QStringLiteral("system-search")},
                                       ink);
        if (!glass.isNull()) m_searchButton->setIcon(glass);
    }
    if (m_volumeButton) {
        m_volumeButton->setPalette(palette);
        m_volumeSlider->setPalette(palette);
        // Repainted in the new colours, the glyph being drawn by the style
        // against the palette it had when it was made.
        updateVolumeIcon(m_volumeSlider->value(), systemVolumeMuted());
    }
    for (QToolButton *button : {m_minimizeButton, m_maximizeButton, m_closeButton}) {
        button->setPalette(palette);
    }
    update();
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
    // After the menus, which are the rightmost thing on the left: the
    // search button sits before them rather than between them and the title.
    const int left = m_menuBar->geometry().right() + kTitleMargin;
    const int right = m_clock->geometry().left() - kTitleMargin;
    return QRect(left, 0, right - left, height());
}

void TitleBar::refreshVolume() {
    if (!m_volumeSlider) return;

    const double volume = systemVolume();
    if (volume < 0.0) return; // the mixer went away; leave what is shown
    const bool muted = systemVolumeMuted();

    // Set without telling the mixer again: this is the mixer talking to us.
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setValue(qRound(volume * 100.0));
    updateVolumeIcon(m_volumeSlider->value(), muted);
}

void TitleBar::updateVolumeIcon(int value, bool muted) {
    if (!m_volumeButton) return;

    // How loud it is, at a glance, in the desktop's own speaker icons:
    // muted, then quiet, middling and loud.
    QString name = QStringLiteral("audio-volume-high");
    if (muted || value == 0) {
        name = QStringLiteral("audio-volume-muted");
    } else if (value < 34) {
        name = QStringLiteral("audio-volume-low");
    } else if (value < 67) {
        name = QStringLiteral("audio-volume-medium");
    }

    const QColor ink = palette().color(QPalette::WindowText);
    const QIcon icon = tintedIcon({name + QStringLiteral("-symbolic"), name}, ink);
    if (!icon.isNull()) {
        m_volumeButton->setIcon(icon);
    } else {
        // Nothing installed under those names: the style's own, which at
        // least says speaker.
        m_volumeButton->setIcon(style()->standardIcon(
            muted || value == 0 ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume,
            nullptr, this));
    }
    m_volumeButton->setToolTip(muted ? tr("Unmute") : tr("Mute"));
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
    // A double click maximizes the window, but never brings it back out
    // again: with the window filling the screen the caption is a long strip
    // of nothing to click on, and a stray double click there should not
    // shrink it. The restore button is the way back.
    if (event->button() == Qt::LeftButton && !windowFillsScreen(window())) {
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

    // A window filling the screen carries no caption buttons at all: there is
    // nothing to drag it by up there either, so the strip is left to the
    // menus, the title and the clock. Window > Restore brings it back, and
    // File > Exit closes it.
    const bool showButtons = !windowFillsScreen(window());
    m_minimizeButton->setVisible(showButtons);
    m_maximizeButton->setVisible(showButtons);
    m_closeButton->setVisible(showButtons);
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event) {
    // Clicking the clock drops the month out of it. Built on the first
    // click rather than with the strip: most windows never ask for it, and
    // it talks to the calendar service when it does.
    if (watched == m_clock && event->type() == QEvent::MouseButtonPress) {
        if (!m_calendar) m_calendar = new CalendarPane(this);
        if (m_calendar->isVisible()) {
            m_calendar->hide();
        } else {
            m_calendar->popUpAt(m_clock);
        }
        return true;
    }



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
