#pragma once

#include <QWidget>

class CalculatorPane;
class CalendarPane;
class QLabel;
class QMenuBar;
class QSlider;
class SystemVolumeWatch;
class QTimer;
class QToolButton;

// The menu bar and the title bar in one strip, as current desktop applications
// draw them: the menus on the left, the window title in the middle, and the
// clock and the minimize/maximize/close buttons on the right. The window is
// frameless, so this strip is also where it is dragged from and where its top
// edge is resized.
//
// It is a plain widget holding a QMenuBar rather than being one, which also
// stops QMdiArea moving a maximized group's buttons into it: a maximized group
// keeps its own caption inside the desktop area.
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);

    // The menus live here; the window builds them as usual.
    QMenuBar *menuBar() const { return m_menuBar; }

signals:
    // Emitted by the button beside the volume control.
    void darkModeToggled(bool dark);

public:

    // Colours the whole strip. A platform theme can give QMenuBar and the
    // buttons palettes of their own, which outrank the application's, so each
    // part is set explicitly rather than left to inherit.
    void applyPalette(const QPalette &palette);

    // Applies the kiosk settings: what the strip shows, and whether the window
    // can leave full screen. Called at startup and whenever they change in
    // Options > Settings.
    void applyKioskSettings();

protected:
    void paintEvent(QPaintEvent *event) override;
    // Installs the event filter on the window, which is only known by this
    // point and not at construction.
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    // Watches the window for title and maximized-state changes, neither of
    // which has a signal.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void toggleMaximized();
    // The time and date, in the system's own format.
    void updateClock();
    void updateMaximizeButton();
    // Reads the mixer onto the slider and the button. The volume belongs to the
    // session, not to this application: the keyboard's volume keys and the
    // shell's own slider change it too.
    void refreshVolume();
    // The speaker glyph for a level, and the button's tooltip.
    void updateVolumeIcon(int value, bool muted);
    // The sun or the moon, according to the current light/dark scheme.
    void updateDarkIcon();
    // Where the title is drawn: the space between the menus and the window
    // buttons.
    QRect titleArea() const;

    QMenuBar *m_menuBar;
    // Hands the search over to the desktop shell. A button rather than a text
    // box, since text typed here could not reach the shell's own search; see
    // DesktopShell.h. Only present on a desktop that has one.
    QToolButton *m_searchButton = nullptr;
    // Toggles the dark scheme. The window applies it; the strip only reports
    // the request.
    QToolButton *m_darkButton = nullptr;
    // The machine's output volume, where a mixer is available. The button mutes
    // and shows the level; the slider sets it.
    QToolButton *m_volumeButton = nullptr;
    QSlider *m_volumeSlider = nullptr;
    // Notifies on mixer changes rather than being polled.
    SystemVolumeWatch *m_volumeWatch = nullptr;
    // Set while the slider is being dragged: the mixer is being changed from
    // here, and reading it back mid-drag would fight the drag.
    bool m_movingVolume = false;
    // The mixer's last reported mute state, so the button does not have to
    // query it.
    bool m_volumeMuted = false;

    // A calculator, beside the calendar: the same kind of pane from the same
    // strip.
    QToolButton *m_calculatorButton = nullptr;
    CalculatorPane *m_calculator = nullptr;

    QLabel *m_clock;
    // Drops out of the clock: the month, with the events from the desktop's
    // calendars. Built on first use.
    CalendarPane *m_calendar = nullptr;
    QToolButton *m_minimizeButton;
    QToolButton *m_maximizeButton;
    QToolButton *m_closeButton;
    bool m_watchingWindow = false;
};
