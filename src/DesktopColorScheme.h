#pragma once

#include <QDBusVariant>
#include <QObject>

// Whether the desktop as a whole is set light or dark.
//
// Read through the desktop portal, which every current desktop answers and
// which says so in one place whatever is underneath -- GNOME's own setting,
// KDE's, or a sandbox's idea of either. Qt has a hint of its own for this,
// but it depends on the platform theme in use and answers "unknown" under
// the one on this machine, which is no use for following anything.
enum class DesktopScheme { Unknown, Light, Dark };

// What it is now. Unknown where there is no portal to ask, in which case
// nothing should be changed on its account.
DesktopScheme desktopColorScheme();

// Says so again whenever the desktop is switched over, so an application
// can follow it rather than being told twice.
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
