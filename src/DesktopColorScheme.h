#pragma once

#include <QDBusVariant>
#include <QObject>

// Whether the desktop as a whole is set light or dark.
//
// Read through the desktop portal, which every current desktop implements and
// which reports it in one place whatever is underneath: GNOME's own setting,
// KDE's, or a sandbox's view of either. Qt has a hint for this, but it depends
// on the platform theme in use and returns "unknown" with the one on this
// machine.
enum class DesktopScheme { Unknown, Light, Dark };

// The current setting. Unknown when there is no portal to query, in which case
// nothing should be changed because of it.
DesktopScheme desktopColorScheme();

// Emits the setting again whenever the desktop switches, so an application can
// follow it.
class DesktopColorSchemeWatch : public QObject {
    Q_OBJECT
public:
    explicit DesktopColorSchemeWatch(QObject *parent = nullptr);

signals:
    void schemeChanged(DesktopScheme scheme);

private slots:
    void onSettingChanged(const QString &nameSpace, const QString &key,
                          const QDBusVariant &value);
};
