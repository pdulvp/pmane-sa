#include "LegacyConfig.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// Where the settings were before the organisation was renamed: the
// organisation and the application were both "pmane-sa", which
// QStandardPaths spells as `<config>/pmane-sa/pmane-sa`.
const char *kLegacyFolder = "pmane-sa";

// Whether a folder holds anything at all. The old installed program, run once
// after the rename, makes itself an empty desktop in the new place; moving the
// real one in over that is what an upgrade should do, so a folder with nothing
// in it does not count as taken.
bool holdsSomething(const QString &path) {
    const QDir dir(path);
    if (!dir.exists()) return false;
    return !dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
}

} // namespace

void migrateLegacyConfigFolder() {
    const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (config.isEmpty()) return;

    const QString legacyParent = config + QLatin1Char('/') + QLatin1String(kLegacyFolder);
    const QString legacyFolder =
        legacyParent + QLatin1Char('/') + QCoreApplication::applicationName();
    const QString legacyConf = legacyParent + QLatin1Char('/') +
                               QCoreApplication::applicationName() +
                               QStringLiteral(".conf");

    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString conf =
        config + QLatin1Char('/') + QCoreApplication::organizationName() + QLatin1Char('/') +
        QCoreApplication::applicationName() + QStringLiteral(".conf");

    // The rename of the organisation is what this is for; without one there is
    // no old place, and the two paths are the same folder.
    if (QFileInfo(legacyFolder) == QFileInfo(folder)) return;

    if (holdsSomething(legacyFolder) && !holdsSomething(folder)) {
        // The parent of the new folder, which is the renamed organisation and
        // may not exist yet.
        QDir().mkpath(QFileInfo(folder).absolutePath());
        // An empty folder in the way would stop the rename.
        QDir(folder).rmdir(folder);
        if (QDir().rename(legacyFolder, folder)) {
            qInfo().noquote() << "Settings moved from" << legacyFolder << "to" << folder;
        } else {
            // Left where it is: a desktop that cannot be moved is still a
            // desktop, and the next run will try again.
            qWarning().noquote() << "Could not move the settings from" << legacyFolder << "to"
                                 << folder;
        }
    }

    // The launchers, the theme and the rest, which sit beside the folder
    // rather than in it.
    if (QFile::exists(legacyConf) && !QFile::exists(conf)) {
        QDir().mkpath(QFileInfo(conf).absolutePath());
        if (!QFile::rename(legacyConf, conf)) {
            qWarning().noquote() << "Could not move" << legacyConf << "to" << conf;
        }
    }

    // Only when the move emptied it. Anything else in there was put there by
    // something else and is not ours to remove.
    QDir().rmdir(legacyParent);
}
