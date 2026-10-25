#include "DesktopShell.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QVariant>

namespace {

const char *kService = "org.gnome.Shell";
const char *kPath = "/org/gnome/Shell";

} // namespace

bool desktopSearchAvailable() {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    QDBusConnectionInterface *iface = bus.interface();
    return iface && iface->isServiceRegistered(QLatin1String(kService)).value();
}

bool openDesktopSearch() {
    if (!desktopSearchAvailable()) return false;

    // The property rather than FocusSearch(): the methods on this interface are
    // refused to untrusted callers, but the overview property can be set by
    // anyone, and the overview opens with its search focused, which is all that
    // is needed.
    QDBusInterface properties(QLatin1String(kService), QLatin1String(kPath),
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              QDBusConnection::sessionBus());
    const QDBusMessage reply =
        properties.call(QStringLiteral("Set"), QLatin1String(kService),
                        QStringLiteral("OverviewActive"),
                        QVariant::fromValue(QDBusVariant(true)));
    return reply.type() != QDBusMessage::ErrorMessage;
}
