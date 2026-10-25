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
    // Whether it covers whole days rather than a time of day, as a holiday or
    // a birthday does.
    bool allDay = false;
};

// The events in the desktop's calendars over a range of dates.
//
// Read through org.gnome.Shell.CalendarServer, the same service the shell's own
// calendar uses, so whatever Evolution, GNOME Calendar or an online account has
// configured appears here without this code knowing where it came from.
//
// Events arrive asynchronously: a range is requested, and the server reports
// what is in it and reports again when something changes. The pane therefore
// shows what it has and fills in the rest a moment later.
class CalendarEvents : public QObject {
    Q_OBJECT
public:
    explicit CalendarEvents(QObject *parent = nullptr);

    // Whether a calendar service with any events in it is available.
    bool available() const;

    // Requests everything between these dates, inclusive. changed() is emitted
    // whenever the server reports something for the range.
    void setRange(const QDate &from, const QDate &to);

    // The known events for a day, in chronological order.
    QVector<CalendarEvent> eventsOn(const QDate &date) const;
    // Whether a day has any events, which is what the month grid marks.
    bool hasEventsOn(const QDate &date) const;
    // Everything in the requested range, in date order. Returned by reference:
    // the grid calls this once per week while painting.
    const QVector<CalendarEvent> &events() const { return m_events; }

signals:
    void changed();

private slots:
    void onEventsAddedOrUpdated(const QDBusMessage &message);
    void onEventsRemoved(const QStringList &ids);

private:

    QVector<CalendarEvent> m_events;
    bool m_connected = false;
    // Whether the server has ever reported anything. Until it has, the range is
    // requested repeatedly: the server only reports a range that differs from
    // the one it already holds, and the desktop's own calendar may have
    // requested exactly the weeks wanted here, in which case a single request
    // gets no reply.
    bool m_answered = false;
};
