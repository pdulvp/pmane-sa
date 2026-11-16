#pragma once

#include <QVector>
#include <QWidget>

#include <QtGlobal>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class QElapsedTimer;

// Formats an elapsed time as H:MM:SS.cc, hours unpadded so a long run reads
// 12:03:45.67 rather than 012:03:45.67. Exposed so the formatting can be
// tested without a window.
QString formatElapsed(qint64 ms);

// The chronometer pane that drops out of the strip, beside the calculator:
// a large readout of the time elapsed, with the basic options a chronometer
// has — start/stop, lap, reset — and the laps taken listed underneath. The
// time keeps running while the pane is closed, since it is measured from when
// it was started, not displayed.
class ChronometerPane : public QWidget {
    Q_OBJECT
public:
    explicit ChronometerPane(QWidget *parent = nullptr);

    // Opens it under `anchor`.
    void popUpAt(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;
    // The readout is refreshed only while the pane is on screen; the elapsed
    // time itself comes from the watch and is correct however long it was
    // hidden.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    // Space starts or stops, L takes a lap, R resets.
    void keyPressEvent(QKeyEvent *event) override;

private:
    // The elapsed time so far: the settled part plus, while running, what the
    // watch has counted since it started.
    qint64 elapsedMs() const;
    // What the readout shows: the time left when a target is set (a reverse
    // chronometer), else the time elapsed.
    qint64 shownMs() const;
    void toggleStart();
    void takeLap();
    void reset();
    // Steps the countdown target by `deltaMs`, clamped at zero — zero being no
    // target, the plain chronometer. Only while stopped: changing the target
    // re-arms the watch.
    void adjustTarget(qint64 deltaMs);
    // The readout and the buttons' enabled state and labels, from the state.
    void refresh();
    // The readout takes the accent colour while the watch runs or a target is
    // set, so a countdown reads differently from a plain chronometer.
    void updateReadoutColour();
    // The list and the buttons paint their own surfaces, and a popup inherits
    // no palette from the dialog chrome, so the scheme is applied explicitly.
    void wearTheScheme();

    QLabel *m_time;
    QPushButton *m_startStop;
    QPushButton *m_lap;
    QPushButton *m_reset;
    // The step buttons that set the countdown target: −1:00, −0:30, +0:30,
    // +1:00.
    QVector<QPushButton *> m_steps;
    QListWidget *m_laps;
    // The watch and the time banked while it is stopped.
    QElapsedTimer *m_watch;
    qint64 m_settled = 0;
    bool m_running = false;
    // The countdown target in ms; zero means no target, count up as a plain
    // chronometer does.
    qint64 m_target = 0;
    // The last lap's total, so a lap shows its own length as well as the total.
    qint64 m_lastLap = 0;
    int m_lapCount = 0;
    // Refreshes the readout while the pane is visible.
    QTimer *m_display;
};
