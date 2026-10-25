#pragma once

#include <QDate>
#include <QWidget>

class CalendarEvents;
class QLabel;
class QToolButton;

// The month grid that drops out of the clock: one month at a time, with the
// events from the desktop's calendars drawn across the days.
//
// An event covering several days is drawn as one bar spanning them rather than
// a mark on each, so its length shows how long it lasts.
//
// Days are not clickable: this displays the month, it is not a calendar
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
    // The day a cell stands for: six rows of seven, starting on the first day
    // of the week containing the start of the month.
    QDate dateAt(int row, int column) const;
    QRect cellRect(int row, int column) const;
    QRect gridRect() const;

    CalendarEvents *m_events;
    QDate m_month;   // the first day of the month shown
    QDate m_first;   // the day the top-left cell stands for
    QLabel *m_heading;
    QToolButton *m_previous;
    QToolButton *m_next;
};
