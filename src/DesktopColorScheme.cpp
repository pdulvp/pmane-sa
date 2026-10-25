#include "DesktopColorScheme.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace {

const char *kService = "org.freedesktop.portal.Desktop";
const char *kPath = "/org/freedesktop/portal/desktop";
const char *kInterface = "org.freedesktop.portal.Settings";

// The portal's appearance settings namespace, and the one key read here.
const char *kNamespace = "org.freedesktop.appearance";
const char *kKey = "color-scheme";

// The portal's numbering: 1 means dark, 2 means light, and 0 means "no
// preference", which is treated as light, that being how a desktop looks when
// nothing else has been set.
//
// This matters: GNOME's Appearance panel writes 'prefer-dark' for dark and
// plain 'default' for light, so 0 arrives every time the user switches back.
// Treating it as "unknown" would follow dark but never light.
DesktopScheme fromPortal(uint value) {
    switch (value) {
    case 1:
        return DesktopScheme::Dark;
    case 2:
        return DesktopScheme::Light;
    default:
        return DesktopScheme::Light;
    }
}

} // namespace

DesktopScheme desktopColorScheme() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return DesktopScheme::Unknown;

    QDBusInterface settings(QLatin1String(kService), QLatin1String(kPath),
                            QLatin1String(kInterface), bus);
    // ReadOne where the portal provides it, and the older Read where it does
    // not: the newer name arrived in the version that deprecated the older one.
    QDBusReply<QDBusVariant> answer = settings.call(
        QStringLiteral("ReadOne"), QLatin1String(kNamespace), QLatin1String(kKey));
    if (!answer.isValid()) {
        answer = settings.call(QStringLiteral("Read"), QLatin1String(kNamespace),
                               QLatin1String(kKey));
    }
    // Unknown is reserved for the case where it means something: there was no
    // portal to query, so nothing is known and nothing should change.
    if (!answer.isValid()) return DesktopScheme::Unknown;

    // Read returns a variant holding a variant; ReadOne returns the value
    // itself. Both are unwrapped here.
    QVariant held = answer.value().variant();
    if (held.canConvert<QDBusVariant>()) held = held.value<QDBusVariant>().variant();
    return fromPortal(held.toUInt());
}

DesktopColorSchemeWatch::DesktopColorSchemeWatch(QObject *parent) : QObject(parent) {
    QDBusConnection::sessionBus().connect(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
        QStringLiteral("SettingChanged"), this,
        SLOT(onSettingChanged(QString, QString, QDBusVariant)));
}

void DesktopColorSchemeWatch::onSettingChanged(const QString &nameSpace, const QString &key,
                                               const QDBusVariant &value) {
    if (nameSpace != QLatin1String(kNamespace) || key != QLatin1String(kKey)) return;
    emit schemeChanged(fromPortal(value.variant().toUInt()));
}
