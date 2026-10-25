#include "TitleBar.h"

#include "CalculatorPane.h"
#include "CalendarPane.h"
#include "DesktopShell.h"
#include "Kiosk.h"
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

// Caption buttons are square and slightly taller than the menu text, as a
// window manager would draw them.
constexpr int kButtonWidth = 34;
constexpr int kButtonHeight = 24;
// Keeps the title clear of the menus and the clock on either side.
constexpr int kTitleMargin = 12;
// Space around the clock, so it reads as separate from the title and the
// buttons.
constexpr int kClockMargin = 10;

// The volume slider: long enough to be usable, short enough to leave the title
// the middle of the strip.
constexpr int kVolumeWidth = 135;
constexpr int kVolumeMargin = 6;



// The short time format has no seconds, but the minute still has to change
// promptly, and the system clock can be set at any time.
constexpr int kClockInterval = 1000;

// A slider that jumps to where it is clicked. By default a click on the groove
// moves the handle one page towards the pointer, which suits a scrollbar but
// not a volume control, where the pointer is already on the wanted level.
//
// Done through the style rather than by intercepting the click, so the handle,
// the drag that follows and the keyboard keep working unchanged.
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
    // The strip is one colour throughout. A menu bar paints its own panel,
    // which most styles shade slightly darker than the window behind it,
    // leaving a faint band behind the menus; without the panel the menus sit
    // directly on this widget's background.
    m_menuBar->setStyleSheet(QStringLiteral("QMenuBar { background: transparent; }"));

    // Beside the volume: the two machine settings the strip can change.
    m_darkButton = captionButton(this, QStyle::SP_DialogResetButton);
    m_darkButton->setToolTip(tr("Dark colours"));

    // Only when a mixer is available.
    if (systemVolumeAvailable()) {
        m_volumeButton = captionButton(this, QStyle::SP_MediaVolume);
        m_volumeButton->setToolTip(tr("Mute"));

        m_volumeSlider = new QSlider(Qt::Horizontal, this);
        m_volumeSlider->setRange(0, 100);
        m_volumeSlider->setFixedWidth(kVolumeWidth);
        // The strip is used for dragging the window, so the slider is kept out
        // of the tab order.
        m_volumeSlider->setFocusPolicy(Qt::NoFocus);
        m_volumeSlider->setToolTip(tr("Output volume"));
        // Parented to the slider so their lifetimes match; the proxy uses the
        // application style as its base without owning it.
        auto *clickToSet = new ClickToSetSlider;
        clickToSet->setParent(m_volumeSlider);
        m_volumeSlider->setStyle(clickToSet);

        m_volumeWatch = new SystemVolumeWatch(this);
        connect(m_volumeWatch, &SystemVolumeWatch::changed, this, [this] {
            if (!m_movingVolume) refreshVolume();
        });
        refreshVolume();
    }

    // Beside the date, both being quick references rather than applications.
    m_calculatorButton = captionButton(this, QStyle::SP_FileDialogDetailedView);
    m_calculatorButton->setToolTip(tr("Calculator"));

    m_clock = new QLabel(this);
    m_clock->setContentsMargins(kClockMargin, 0, kClockMargin, 0);
    m_clock->setAlignment(Qt::AlignCenter);
    // Clicking the date opens the month below it, as the desktop's own clock
    // does. Since the strip is used for dragging, the clock needs a pointing
    // cursor to show that it is clickable.
    m_clock->setCursor(Qt::PointingHandCursor);
    m_clock->setToolTip(tr("Show the month"));
    m_clock->installEventFilter(this);
    updateClock();

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &TitleBar::updateClock);
    clockTimer->start(kClockInterval);

    // Only when there is a shell to hand the search to; a button that could do
    // nothing would be worse than none.
    if (desktopSearchAvailable()) {
        m_searchButton = captionButton(this, QStyle::SP_FileDialogContentsView);
        m_searchButton->setToolTip(tr("Search the desktop"));
        // The icon is set in applyPalette(), which is where the strip's text
        // colour is known.
    }

    m_minimizeButton = captionButton(this, QStyle::SP_TitleBarMinButton);
    m_maximizeButton = captionButton(this, QStyle::SP_TitleBarMaxButton);
    m_closeButton = captionButton(this, QStyle::SP_TitleBarCloseButton);

    m_minimizeButton->setToolTip(tr("Minimize"));
    m_closeButton->setToolTip(tr("Close"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // Before the menus, as a file manager puts its search at the start of the
    // bar rather than among the window buttons.
    if (m_searchButton) layout->addWidget(m_searchButton);
    layout->addWidget(m_menuBar);
    layout->addStretch();
    layout->addWidget(m_darkButton);
    if (m_volumeButton) {
        layout->addWidget(m_volumeButton);
        layout->addWidget(m_volumeSlider);
        layout->addSpacing(kVolumeMargin);
    }
    layout->addWidget(m_calculatorButton);
    layout->addWidget(m_clock);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    if (m_searchButton) {
        connect(m_searchButton, &QToolButton::clicked, this, [] { openDesktopSearch(); });
    }

    connect(m_calculatorButton, &QToolButton::clicked, this, [this] {
        if (!m_calculator) m_calculator = new CalculatorPane(this);
        if (m_calculator->isVisible()) {
            m_calculator->hide();
        } else {
            m_calculator->popUpAt(m_calculatorButton);
        }
    });

    connect(m_darkButton, &QToolButton::clicked, this, [this] {
        // Reported rather than applied: the window applies the scheme, which
        // affects every palette in the application.
        emit darkModeToggled(!darkMode());
    });

    if (m_volumeSlider) {
        // The mixer is set as the slider moves, so the volume follows the drag.
        // The watch is suppressed meanwhile, since reading back mid-drag would
        // fight the drag.
        connect(m_volumeSlider, &QSlider::sliderPressed, this,
                [this] { m_movingVolume = true; });
        connect(m_volumeSlider, &QSlider::sliderReleased, this, [this] {
            m_movingVolume = false;
            refreshVolume();
        });
        connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
            setSystemVolume(value / 100.0);
            // Moving the slider implies unmuting, mute meaning "not now" rather
            // than "zero". Set unconditionally rather than queried first, since
            // querying would block and setting it again is harmless.
            if (value > 0) setSystemVolumeMuted(false);
            updateVolumeIcon(value, false);
        });
        connect(m_volumeButton, &QToolButton::clicked, this, [this] {
            setSystemVolumeMuted(!m_volumeMuted);
            m_volumeMuted = !m_volumeMuted;
            updateVolumeIcon(m_volumeSlider->value(), m_volumeMuted);
        });
    }

    connect(m_minimizeButton, &QToolButton::clicked, this,
            [this] { window()->showMinimized(); });
    connect(m_maximizeButton, &QToolButton::clicked, this, &TitleBar::toggleMaximized);
    connect(m_closeButton, &QToolButton::clicked, this, [this] { window()->close(); });

    setMouseTracking(true);
    // A plain widget paints no background, so the window behind it would show
    // through in the ordinary window colour; the strip has a colour of its own
    // and therefore has to fill.
    setAutoFillBackground(true);

    applyKioskSettings();
}

void TitleBar::applyKioskSettings() {
    const KioskSettings &kiosk = kioskSettings();

    if (m_searchButton) m_searchButton->setVisible(kiosk.showSearch);
    if (m_volumeButton) {
        m_volumeButton->setVisible(kiosk.showVolume);
        m_volumeSlider->setVisible(kiosk.showVolume);
    }
    if (m_calculatorButton) {
        m_calculatorButton->setVisible(kiosk.showCalculator);
        // Hidden with the button that opens it, being positioned relative to
        // that button.
        if (!kiosk.showCalculator && m_calculator) m_calculator->hide();
    }
    m_clock->setVisible(kiosk.showCalendar);
    if (!kiosk.showCalendar && m_calendar) m_calendar->hide();

    // The buttons follow too: the lock is precisely about whether the window
    // can leave full screen.
    updateMaximizeButton();
}

void TitleBar::applyPalette(const QPalette &palette) {
    setPalette(palette);
    m_menuBar->setPalette(palette);
    m_clock->setPalette(palette);
    // The strip's text colour, black or white according to how dark the desktop
    // colour is; its icons are tinted to match.
    const QColor ink = palette.color(QPalette::WindowText);
    if (m_searchButton) {
        m_searchButton->setPalette(palette);
        const QIcon glass = tintedIcon({QStringLiteral("system-search-symbolic"),
                                        QStringLiteral("edit-find-symbolic"),
                                        QStringLiteral("system-search")},
                                       ink);
        if (!glass.isNull()) m_searchButton->setIcon(glass);
    }
    if (m_darkButton) {
        m_darkButton->setPalette(palette);
        updateDarkIcon();
    }
    if (m_calculatorButton) {
        m_calculatorButton->setPalette(palette);
        const QIcon sums = tintedIcon({QStringLiteral("accessories-calculator-symbolic"),
                                       QStringLiteral("accessories-calculator")},
                                      ink);
        if (!sums.isNull()) m_calculatorButton->setIcon(sums);
    }
    if (m_volumeButton) {
        m_volumeButton->setPalette(palette);
        m_volumeSlider->setPalette(palette);
        // Repainted in the new colours, the glyph having been drawn by the
        // style against the palette in force when it was created.
        updateVolumeIcon(m_volumeSlider->value(), m_volumeMuted);
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

    // Centred on the window, as a caption is, as long as it clears the menus
    // and the buttons; otherwise centred in the space left.
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
    // After the menus, which are the rightmost item on the left; the search
    // button sits before them rather than between them and the title.
    const int left = m_menuBar->geometry().right() + kTitleMargin;

    // Up to whichever widget actually starts the right-hand end. Using one
    // named widget would be unreliable: the kiosk settings remove them one by
    // one, and a hidden widget keeps its last geometry.
    int right = width();
    for (const QWidget *widget : {static_cast<const QWidget *>(m_darkButton),
                                  static_cast<const QWidget *>(m_volumeButton),
                                  static_cast<const QWidget *>(m_calculatorButton),
                                  static_cast<const QWidget *>(m_clock),
                                  static_cast<const QWidget *>(m_minimizeButton),
                                  static_cast<const QWidget *>(m_maximizeButton),
                                  static_cast<const QWidget *>(m_closeButton)}) {
        if (widget && widget->isVisible()) right = qMin(right, widget->geometry().left());
    }
    right -= kTitleMargin;
    return QRect(left, 0, right - left, height());
}

void TitleBar::refreshVolume() {
    if (!m_volumeSlider) return;

    // Queried asynchronously: the answer arrives later and nothing here blocks.
    readSystemVolume(this, [this](double volume, bool muted) {
        if (!m_volumeSlider || m_movingVolume) return;
        m_volumeMuted = muted;
        // Set without writing back to the mixer: this update came from the
        // mixer.
        const QSignalBlocker blocker(m_volumeSlider);
        m_volumeSlider->setValue(qRound(volume * 100.0));
        updateVolumeIcon(m_volumeSlider->value(), muted);
    });
}

void TitleBar::updateDarkIcon() {
    if (!m_darkButton) return;

    // The moon to switch to dark and the sun to switch back: the icon shows
    // what the button will do, not the current state.
    const QColor ink = palette().color(QPalette::WindowText);
    const QStringList names =
        darkMode() ? QStringList{QStringLiteral("weather-clear-symbolic"),
                                 QStringLiteral("display-brightness-symbolic")}
                   : QStringList{QStringLiteral("weather-clear-night-symbolic"),
                                 QStringLiteral("night-light-symbolic")};
    const QIcon icon = tintedIcon(names, ink);
    if (!icon.isNull()) m_darkButton->setIcon(icon);
    m_darkButton->setToolTip(darkMode() ? tr("Light colours") : tr("Dark colours"));
}

void TitleBar::updateVolumeIcon(int value, bool muted) {
    if (!m_volumeButton) return;

    // The level shown with the desktop's own speaker icons: muted, low, medium
    // and high.
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
        // None of those icon names is installed, so fall back to the style's own
        // speaker icon.
        m_volumeButton->setIcon(style()->standardIcon(
            muted || value == 0 ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume,
            nullptr, this));
    }
    m_volumeButton->setToolTip(muted ? tr("Unmute") : tr("Mute"));
}

void TitleBar::updateClock() {
    const QLocale locale = QLocale::system();
    const QDateTime now = QDateTime::currentDateTime();
    // The system's short time and date formats. The label ignores text it is
    // already showing, so the updates in between cost nothing.
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

    // The window's top edge runs along the top of this strip.
    const Qt::Edges edges = windowEdgesAt(window(), event->globalPosition().toPoint());
    if (edges && startWindowResize(window(), edges)) {
        event->accept();
        return;
    }

    // Anywhere else on the bare strip drags the window. The window manager
    // performs the drag, so snapping and the pointer grab behave correctly. A
    // maximized window is not draggable, to avoid leaving full screen by
    // accident.
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
    // Under a kiosk lock a double click maximizes the window but never
    // restores it: with the window filling the screen the caption is a long
    // empty strip, and a stray double click there should not shrink it.
    // Unlocked, it toggles both ways as a caption normally does.
    const bool canShrink = !windowFillsScreen(window()) || !kioskSettings().locked;
    if (event->button() == Qt::LeftButton && canShrink) {
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

    // Locked full screen: the window has no caption buttons and cannot be
    // dragged from the strip, which is left to the menus, the title and the
    // clock. Unlocked, under Options > Settings, Kiosk, it behaves like any
    // other window and keeps its buttons, so there is a way back from full
    // screen.
    const bool showButtons = !windowFillsScreen(window()) || !kioskSettings().locked;
    m_minimizeButton->setVisible(showButtons);
    m_maximizeButton->setVisible(showButtons);
    m_closeButton->setVisible(showButtons);
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event) {
    // Clicking the clock opens the month below it. Built on first click rather
    // than with the strip, since most sessions never open it and building it
    // queries the calendar service.
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
