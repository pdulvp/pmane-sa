#pragma once

#include <QWidget>

class QLabel;
class QTimer;

// The clock face that drops out of the time in the strip, beside the month that
// drops out of the date: a round dial with hour, minute and second hands, and
// the date written in full in the band above it.
//
// Nothing in it is clickable: it shows the time, as the month pane shows the
// month.
class ClockPane : public QWidget {
    Q_OBJECT
public:
    explicit ClockPane(QWidget *parent = nullptr);

    // Opens it under `anchor`, on the current time.
    void popUpAt(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;
    // The second hand moves only while the face is on screen: the strip's own
    // clock is what has to keep time, and a hand nobody is looking at does not.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    // The date in the band, in the system's long format.
    void refreshHeading();
    // The square the dial is drawn in, centred in the space below the band.
    QRectF faceRect() const;

    QLabel *m_heading;
    QTimer *m_tick;
};
