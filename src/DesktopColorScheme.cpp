#include "DesktopColorScheme.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace {

const char *kService = "org.freedesktop.portal.Desktop";
const char *kPath = "/org/freedesktop/portal/desktop";
const char *kInterface = "org.freedesktop.portal.Settings";

// What the portal calls the appearance settings, and the one key here.
const char *kNamespace = "org.freedesktop.appearance";
const char *kKey = "color-scheme";

// The portal's own numbering: 1 asks for dark, 2 asks for light, and 0 is
// "no preference" -- which is light, that being what a desktop looks like
// when nobody has asked for anything else.
//
// Not a nicety: GNOME's own Appearance panel writes 'prefer-dark' for dark
// and plain 'default' for light, so 0 is what arrives every time someone
// switches back. Treating it as "say nothing" means dark is followed and
// light never is.
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
    // ReadOne where the portal has it, and the older Read where it does not:
    // the newer name arrived with the version that deprecated the older one.
    QDBusReply<QDBusVariant> answer = settings.call(
        QStringLiteral("ReadOne"), QLatin1String(kNamespace), QLatin1String(kKey));
    if (!answer.isValid()) {
        answer = settings.call(QStringLiteral("Read"), QLatin1String(kNamespace),
                               QLatin1String(kKey));
    }
    // Unknown is kept for the one case it means something: there was no
    // portal to ask, so nothing is known and nothing should be changed.
    if (!answer.isValid()) return DesktopScheme::Unknown;

    // Read hands back a variant holding a variant; ReadOne hands back the
    // value itself. Unwrapped either way.
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
