#pragma once

#include <QWidget>

class CalendarPane;
class QLabel;
class QMenuBar;
class QSlider;
class QTimer;
class QToolButton;

// The menu bar and the title bar in one strip, the way current desktop
// applications draw them: the menus on the left, the window's title across
// the middle, and the clock and the minimize/maximize/close buttons on the
// right. The
// window itself is frameless, so this strip is also where it is dragged
// from and where its top edge is resized.
//
// It is a plain widget holding a QMenuBar rather than being one, which also
// keeps QMdiArea from moving a maximized group's buttons up into it: a
// maximized group keeps its own caption inside the desktop area.
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);

    // The menus live here; the window builds them as it always did.
    QMenuBar *menuBar() const { return m_menuBar; }

signals:
    // Asked for by the button beside the volume.
    void darkModeToggled(bool dark);

public:

    // Colours the whole strip. A platform theme can hand QMenuBar and the
    // buttons palettes of their own, which outrank the application's, so
    // each part is told the colour scheme rather than left to inherit it.
    void applyPalette(const QPalette &palette);

protected:
    void paintEvent(QPaintEvent *event) override;
    // Where the watch on the window is set up, the window being final by
    // then and not when this is built.
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    // Watches the window for the title and the maximized state, neither of
    // which is announced by a signal.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void toggleMaximized();
    // The time and date, written the way this system writes them.
    void updateClock();
    void updateMaximizeButton();
    // Reads the mixer and puts what it says on the slider and the button.
    // The volume is the desktop's, not ours: the keyboard's volume keys and
    // the shell's own slider move it too.
    void refreshVolume();
    // The speaker glyph for a level, and what the button says it will do.
    void updateVolumeIcon(int value, bool muted);
    // The sun or the moon, by which half of the scheme is on.
    void updateDarkIcon();
    // Where the title is drawn: the space left between the menus and the
    // window buttons.
    QRect titleArea() const;

    QMenuBar *m_menuBar;
    // Hands the search over to the desktop shell: a button rather than a
    // box, because nothing typed here could reach the shell's own search
    // anyway -- see DesktopShell.h. Only there on a desktop that has a
    // search to hand over to.
    QToolButton *m_searchButton = nullptr;
    // The machine's output volume, where there is a mixer to ask. The
    // button mutes and shows how loud it is; the slider sets it.
    // Turns the scheme's dark half on and off. The window applies it: the
    // strip only says that it was asked for.
    QToolButton *m_darkButton = nullptr;
    QToolButton *m_volumeButton = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QTimer *m_volumeWatch = nullptr;

    QLabel *m_clock;
    // Drops out of the clock: the month, with what the desktop's calendars
    // have in it. Built the first time it is asked for.
    CalendarPane *m_calendar = nullptr;
    QToolButton *m_minimizeButton;
    QToolButton *m_maximizeButton;
    QToolButton *m_closeButton;
    bool m_watchingWindow = false;
};
