#pragma once

#include <QDate>
#include <QWidget>

class CalendarEvents;
class QLabel;
class QToolButton;

// The month grid that drops out of the clock: one month at a time, with
// whatever the desktop's calendars have in it drawn across the days.
//
// An event covering several days is drawn as one bar running across them,
// not as a mark on each: a week away reads as a week away, and the days it
// covers are the shape of the bar rather than something to count.
//
// Days are not clickable. It is a thing to look at, not another calendar
// application.
class CalendarPane : public QWidget {
    Q_OBJECT
public:
    explicit CalendarPane(QWidget *parent = nullptr);

    // Opens it under `anchor`, on the month containing today.
    void popUpAt(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void showMonth(const QDate &anyDayInMonth);
    void refreshHeading();
    // The day a cell stands for: six rows of seven, starting on the first
    // day of the week before the month begins.
    QDate dateAt(int row, int column) const;
    QRect cellRect(int row, int column) const;
    QRect gridRect() const;

    CalendarEvents *m_events;
    QDate m_month;   // the first of the month on show
    QDate m_first;   // the day the top-left cell stands for
    QLabel *m_heading;
    QToolButton *m_previous;
    QToolButton *m_next;
};
