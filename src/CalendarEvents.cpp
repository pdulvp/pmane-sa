#include "CalendarEvents.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>

#include <algorithm>

namespace {

const char *kService = "org.gnome.Shell.CalendarServer";
const char *kPath = "/org/gnome/Shell/CalendarServer";
const char *kInterface = "org.gnome.Shell.CalendarServer";

// An event the server says starts and ends at midnight covers whole days:
// that is how a holiday or a birthday is written down.
bool looksAllDay(const QDateTime &start, const QDateTime &end) {
    return start.time() == QTime(0, 0) && end.time() == QTime(0, 0) && start != end;
}

} // namespace

CalendarEvents::CalendarEvents(QObject *parent) : QObject(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    // The signals carry everything; the method only says which dates are
    // wanted. Taken as raw messages because the payload is an array of
    // structures, which is easier read out of the argument than declared.
    // Matched on the path and interface rather than on the sender: the
    // server announces these to nobody in particular, from the unique name
    // it happens to have, and a rule naming the well-known one does not
    // catch them.
    m_connected =
        bus.connect(QString(), QLatin1String(kPath), QLatin1String(kInterface),
                    QStringLiteral("EventsAddedOrUpdated"), this,
                    SLOT(onEventsAddedOrUpdated(QDBusMessage))) &&
        bus.connect(QString(), QLatin1String(kPath), QLatin1String(kInterface),
                    QStringLiteral("EventsRemoved"), this, SLOT(onEventsRemoved(QStringList)));
}

bool CalendarEvents::available() const {
    if (!m_connected) return false;
    QDBusInterface server(QLatin1String(kService), QLatin1String(kPath),
                          QStringLiteral("org.freedesktop.DBus.Properties"),
                          QDBusConnection::sessionBus());
    const QDBusReply<QVariant> has =
        server.call(QStringLiteral("Get"), QLatin1String(kInterface),
                    QStringLiteral("HasCalendars"));
    return has.isValid() && has.value().toBool();
}

void CalendarEvents::setRange(const QDate &from, const QDate &to) {
    if (!m_connected) return;

    // Whole days, and the end exclusive: an event starting on the last day
    // belongs to the range.
    const qint64 since = QDateTime(from, QTime(0, 0)).toSecsSinceEpoch();
    const qint64 until = QDateTime(to.addDays(1), QTime(0, 0)).toSecsSinceEpoch();

    QDBusInterface server(QLatin1String(kService), QLatin1String(kPath),
                          QLatin1String(kInterface), QDBusConnection::sessionBus());
    // The answer comes as a signal, so nothing is waited for here.
    server.asyncCall(QStringLiteral("SetTimeRange"), since, until, false);
}

void CalendarEvents::onEventsAddedOrUpdated(const QDBusMessage &message) {
    if (message.arguments().isEmpty()) return;

    // Const, and it has to be: QDBusArgument's beginArray() and the rest
    // come in a reading and a writing pair, and a non-const one picks the
    // writing half -- which aborts on an argument that came off the wire.
    const QDBusArgument events = message.arguments().first().value<QDBusArgument>();
    events.beginArray();
    while (!events.atEnd()) {
        CalendarEvent event;
        qint64 start = 0;
        qint64 end = 0;
        QVariantMap extras;

        events.beginStructure();
        events >> event.id >> event.summary >> start >> end >> extras;
        events.endStructure();

        event.start = QDateTime::fromSecsSinceEpoch(start);
        event.end = QDateTime::fromSecsSinceEpoch(end);
        event.allDay = looksAllDay(event.start, event.end);

        // An event that has changed replaces the one it changed from.
        auto existing = std::find_if(m_events.begin(), m_events.end(),
                                     [&event](const CalendarEvent &known) {
                                         return known.id == event.id;
                                     });
        if (existing != m_events.end()) {
            *existing = event;
        } else {
            m_events.append(event);
        }
    }
    events.endArray();

    std::sort(m_events.begin(), m_events.end(),
              [](const CalendarEvent &a, const CalendarEvent &b) { return a.start < b.start; });
    emit changed();
}

void CalendarEvents::onEventsRemoved(const QStringList &ids) {
    const auto gone = std::remove_if(m_events.begin(), m_events.end(),
                                     [&ids](const CalendarEvent &event) {
                                         return ids.contains(event.id);
                                     });
    if (gone == m_events.end()) return;
    m_events.erase(gone, m_events.end());
    emit changed();
}

QVector<CalendarEvent> CalendarEvents::eventsOn(const QDate &date) const {
    QVector<CalendarEvent> found;
    for (const CalendarEvent &event : m_events) {
        // An event spans from its first day to its last; an all-day one ends
        // at midnight on the day after, which is not a day it covers.
        const QDate last = event.allDay ? event.end.date().addDays(-1) : event.end.date();
        if (date >= event.start.date() && date <= last) found.append(event);
    }
    return found;
}

bool CalendarEvents::hasEventsOn(const QDate &date) const {
    return !eventsOn(date).isEmpty();
}
