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

// An event the server reports as starting and ending at midnight covers whole
// days, which is how a holiday or a birthday is recorded.
bool looksAllDay(const QDateTime &start, const QDateTime &end) {
    return start.time() == QTime(0, 0) && end.time() == QTime(0, 0) && start != end;
}

} // namespace

CalendarEvents::CalendarEvents(QObject *parent) : QObject(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    // The signals carry the data; the method only states which dates are
    // wanted. Taken as raw messages because the payload is an array of
    // structures, which is easier to read out of the argument than to declare.
    // Matched on path and interface rather than sender: the server broadcasts
    // these from whatever unique name it has, so a rule naming the well-known
    // name does not match them.
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

    // Whole days, with an exclusive end, so an event starting on the last day
    // is included.
    const qint64 since = QDateTime(from, QTime(0, 0)).toSecsSinceEpoch();
    const qint64 until = QDateTime(to.addDays(1), QTime(0, 0)).toSecsSinceEpoch();

    QDBusInterface server(QLatin1String(kService), QLatin1String(kPath),
                          QLatin1String(kInterface), QDBusConnection::sessionBus());
    // Forced until the server has replied once. The shell's own calendar sets a
    // range too, and when it is the same range wanted here, the six weeks
    // around today, an unforced request changes nothing and so reports nothing,
    // leaving the pane empty until the month is changed.
    const bool force = !m_answered;
    // The reply arrives as a signal, so nothing is waited for here.
    server.asyncCall(QStringLiteral("SetTimeRange"), since, until, force);
}

void CalendarEvents::onEventsAddedOrUpdated(const QDBusMessage &message) {
    // Any reply, even an empty list, means the server is responding.
    m_answered = true;
    if (message.arguments().isEmpty()) return;

    // Must be const: QDBusArgument's beginArray() and friends come in reading
    // and writing pairs, and a non-const argument selects the writing half,
    // which aborts on an argument received off the wire.
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

        // An updated event replaces the previous version of itself.
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
        // An event spans its first day to its last; an all-day one ends at
        // midnight on the following day, which it does not cover.
        const QDate last = event.allDay ? event.end.date().addDays(-1) : event.end.date();
        if (date >= event.start.date() && date <= last) found.append(event);
    }
    return found;
}

bool CalendarEvents::hasEventsOn(const QDate &date) const {
    return !eventsOn(date).isEmpty();
}
