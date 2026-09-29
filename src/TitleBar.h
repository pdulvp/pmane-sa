#pragma once

#include <QWidget>

class QLabel;
class QMenuBar;
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
    // Where the title is drawn: the space left between the menus and the
    // window buttons.
    QRect titleArea() const;

    QMenuBar *m_menuBar;
    QLabel *m_clock;
    QToolButton *m_minimizeButton;
    QToolButton *m_maximizeButton;
    QToolButton *m_closeButton;
    bool m_watchingWindow = false;
};
