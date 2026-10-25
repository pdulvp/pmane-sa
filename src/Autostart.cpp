#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// The file name is arbitrary, but matching the application's own .desktop file
// stops a session listing the Program Manager twice under two names.
const char *kFileName = "pmane-sa.desktop";

QString autostartDir() {
    // ~/.config/autostart, wherever this session keeps its configuration.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/autostart");
}

QString autostartPath() { return autostartDir() + QLatin1Char('/') + QLatin1String(kFileName); }

// The binary actually running rather than a name on the PATH: this may be an
// installed copy or one in a build directory, and the one that wrote the file
// is the one meant to start.
QString execLine() {
    const QString binary = QCoreApplication::applicationFilePath();
    // A path containing a space parses as two arguments unless quoted, which
    // the desktop entry specification spells this way.
    if (!binary.contains(QLatin1Char(' '))) return binary;
    return QLatin1Char('"') + binary + QLatin1Char('"');
}

} // namespace

bool autostartEnabled() {
    const QString path = autostartPath();
    if (!QFileInfo::exists(path)) return false;

    // A session tweak tool disables an entry by leaving the file in place and
    // writing one of these keys into it rather than deleting it.
    QSettings entry(path, QSettings::IniFormat);
    entry.beginGroup(QStringLiteral("Desktop Entry"));
    if (entry.value(QStringLiteral("Hidden")).toString().compare(
            QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    const QString gnome =
        entry.value(QStringLiteral("X-GNOME-Autostart-enabled")).toString();
    return gnome.isEmpty() || gnome.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0;
}

bool setAutostartEnabled(bool enabled) {
    const QString path = autostartPath();

    if (!enabled) {
        if (!QFileInfo::exists(path)) return true;
        return QFile::remove(path);
    }

    if (!QDir().mkpath(autostartDir())) return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;

    // Written directly rather than through QSettings: a desktop entry is not an
    // ini file, its group name containing a space and its values never being
    // quoted, and these few lines are clearer this way.
    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=" << QCoreApplication::translate("Autostart", "Program Manager") << "\n";
    out << "Comment="
        << QCoreApplication::translate("Autostart",
                                       "Starts the Program Manager with this session")
        << "\n";
    out << "Exec=" << execLine() << "\n";
    out << "Icon=user-desktop\n";
    out << "Terminal=false\n";
    // The key a GNOME session reads to enable the entry.
    out << "X-GNOME-Autostart-enabled=true\n";
    out.flush();
    file.close();

    return file.error() == QFile::NoError;
}
