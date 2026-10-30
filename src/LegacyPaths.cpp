#include "LegacyPaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// Where the folders were before the organisation was renamed: the organisation
// and the application were both "pmane-sa", which QStandardPaths spells as
// `<root>/pmane-sa/pmane-sa`.
const char *kLegacyName = "pmane-sa";

// Whether a folder holds a file, anywhere inside it. Folders alone do not
// count: running the new program once makes itself an empty `prefixes` and an
// empty `launches`, and those must not be what stops the real ones from being
// moved in.
bool holdsFiles(const QString &path) {
    if (!QDir(path).exists()) return false;
    QDirIterator walk(path, QDir::Files | QDir::Hidden | QDir::System,
                      QDirIterator::Subdirectories);
    return walk.hasNext();
}

// Moves one old folder onto its new place. The destination is expected to hold
// no file: it is removed first, since a rename cannot land on a folder that
// exists, and what is removed can only be empty folders.
//
// `sayIfInUse` is false for the cache, which is logs of past launches: a
// destination already in use is the ordinary case there and would say so at
// every startup for as long as the old folder sat around.
void moveFolder(const QString &from, const QString &to, bool sayIfInUse) {
    if (!holdsFiles(from)) return;
    if (QFileInfo(from) == QFileInfo(to)) return;

    if (holdsFiles(to)) {
        // Two of them, with files in both. Merging a set of wine prefixes into
        // another is not something to do behind someone's back.
        if (sayIfInUse) {
            qWarning().noquote() << "Left as it is:" << from << "--" << to << "is in use";
        }
        return;
    }

    QDir().mkpath(QFileInfo(to).absolutePath());
    QDir(to).removeRecursively();
    if (QDir().rename(from, to)) {
        qInfo().noquote() << "Moved" << from << "to" << to;
    } else {
        // Left where it is, which the program can still be pointed at, and the
        // next run will try again. A rename fails when the two are on
        // different filesystems, which copying gigabytes of prefixes behind
        // someone's back is no answer to.
        qWarning().noquote() << "Could not move" << from << "to" << to;
    }
}

// The old folder of one root, and the parent it sits in.
QString legacyFolder(QStandardPaths::StandardLocation root, QString *parent) {
    const QString base = QStandardPaths::writableLocation(root);
    if (base.isEmpty()) return {};
    *parent = base + QLatin1Char('/') + QLatin1String(kLegacyName);
    return *parent + QLatin1Char('/') + QCoreApplication::applicationName();
}

} // namespace

void migrateLegacyFolders() {
    // Config, data and cache: the settings, the wine prefixes and the launch
    // logs. The generic root is the one the old name sat in; the application
    // root is where the same thing is now.
    const QVector<QPair<QStandardPaths::StandardLocation, QStandardPaths::StandardLocation>>
        roots = {{QStandardPaths::ConfigLocation, QStandardPaths::AppConfigLocation},
                 {QStandardPaths::GenericDataLocation, QStandardPaths::AppDataLocation},
                 {QStandardPaths::GenericCacheLocation, QStandardPaths::CacheLocation}};

    for (const auto &root : roots) {
        QString parent;
        const QString from = legacyFolder(root.first, &parent);
        if (from.isEmpty()) continue;

        moveFolder(from, QStandardPaths::writableLocation(root.second),
                   root.first != QStandardPaths::GenericCacheLocation);

        // The settings file sits beside the folder rather than in it.
        if (root.first == QStandardPaths::ConfigLocation) {
            const QString conf = QCoreApplication::applicationName() + QStringLiteral(".conf");
            const QString legacyConf = parent + QLatin1Char('/') + conf;
            const QString current = QStandardPaths::writableLocation(root.first) +
                                    QLatin1Char('/') + QCoreApplication::organizationName() +
                                    QLatin1Char('/') + conf;
            if (QFile::exists(legacyConf) && !QFile::exists(current)) {
                QDir().mkpath(QFileInfo(current).absolutePath());
                if (!QFile::rename(legacyConf, current)) {
                    qWarning().noquote() << "Could not move" << legacyConf << "to" << current;
                }
            }
        }

        // Only when the move emptied it. Anything else in there was put there
        // by something else and is not ours to remove.
        QDir().rmdir(parent);
    }
}
