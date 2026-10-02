#pragma once

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

class QDBusMessage;

// One entry from the desktop's calendars.
struct CalendarEvent {
    QString id;
    QString summary;
    QDateTime start;
    QDateTime end;
    // Whether it covers whole days rather than a time of day, which is what
    // a holiday or a birthday looks like.
    bool allDay = false;
};

// What the desktop's calendars hold, over a range of dates.
//
// Read through org.gnome.Shell.CalendarServer, which is the same service the
// shell's own calendar gets its events from -- so whatever Evolution, GNOME
// Calendar or an online account has set up turns up here without this having
// to know anything about where it came from.
//
// Events arrive rather than being returned: a range is asked for, and the
// server announces what is in it, and announces again when something
// changes. So the pane shows what it has and fills in a moment later.
class CalendarEvents : public QObject {
    Q_OBJECT
public:
    explicit CalendarEvents(QObject *parent = nullptr);

    // Whether there is a calendar service with anything in it.
    bool available() const;

    // Asks for everything between these dates, inclusive. Answered by
    // changed() whenever the server has something to say about it.
    void setRange(const QDate &from, const QDate &to);

    // What is known about a day, in the order the day runs.
    QVector<CalendarEvent> eventsOn(const QDate &date) const;
    // Whether a day has anything at all, which is what a month grid marks.
    bool hasEventsOn(const QDate &date) const;
    // Everything in the range asked for, in the order the days run. By
    // reference: the grid asks for this once per week while painting.
    const QVector<CalendarEvent> &events() const { return m_events; }

signals:
    void changed();

private slots:
    void onEventsAddedOrUpdated(const QDBusMessage &message);
    void onEventsRemoved(const QStringList &ids);

private:

    QVector<CalendarEvent> m_events;
    bool m_connected = false;
};
