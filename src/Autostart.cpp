#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// The file's name is ours to choose, but matching the application's own
// .desktop file keeps a session from listing the Program Manager twice under
// two names.
const char *kFileName = "pmane-sansame.desktop";

QString autostartDir() {
    // ~/.config/autostart, wherever this session keeps its config.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/autostart");
}

QString autostartPath() { return autostartDir() + QLatin1Char('/') + QLatin1String(kFileName); }

// The binary that is actually running, rather than a name on the PATH: this
// may be an installed copy or one being worked on in a build directory, and
// the one that wrote the file is the one meant to start.
QString execLine() {
    const QString binary = QCoreApplication::applicationFilePath();
    // A path with a space in it is a command line with two words in it
    // unless it is quoted, which the desktop entry spec spells this way.
    if (!binary.contains(QLatin1Char(' '))) return binary;
    return QLatin1Char('"') + binary + QLatin1Char('"');
}

} // namespace

bool autostartEnabled() {
    const QString path = autostartPath();
    if (!QFileInfo::exists(path)) return false;

    // A session's tweak tool turns an entry off by leaving the file in place
    // and writing one of these into it rather than by deleting it.
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

    // Written out by hand rather than through QSettings: a desktop entry is
    // not an ini file -- its group name has a space in it and its values are
    // never quoted -- and the handful of lines are plainer this way.
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
    // What a GNOME session reads to mean "yes, this one too".
    out << "X-GNOME-Autostart-enabled=true\n";
    out.flush();
    file.close();

    return file.error() == QFile::NoError;
}
