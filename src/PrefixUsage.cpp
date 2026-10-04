#include "PrefixUsage.h"

#include "CompatTools.h"
#include "Launchers.h"
#include "ScreenManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSettings>

namespace {

// The marker a per-application prefix carries, naming the program it was
// built for and the item that claimed it. It is what tells one of ours from
// a folder someone else put in there.
const char *kMarkerFile = ".program";

// The fields of a saved item a prefix scan needs: what it runs and where it
// runs it. The rest of a ProgramItem -- its icon, its scaling -- costs work
// to read and says nothing about prefixes.
struct SavedItem {
    QString id;
    QString command;
    QString launcher;
    QString prefix;
};

// Every item on every screen. The screen that is open is taken from what is
// on it, since its file is only written when it is saved.
QVector<SavedItem> allItems(const OpenScreen &open) {
    QVector<SavedItem> items;

    for (const ProgramItem &item : open.items) {
        items.append({item.id, item.command, item.launcher, item.prefix});
    }

    ScreenManager screens;
    for (const QString &name : screens.names()) {
        if (name.compare(open.name, Qt::CaseInsensitive) == 0) continue;
        const QString path = screens.filePath(name);
        if (path.isEmpty() || !QFile::exists(path)) continue;

        QSettings settings(path, QSettings::IniFormat);
        const int groups = settings.beginReadArray(QStringLiteral("Groups"));
        for (int g = 0; g < groups; ++g) {
            settings.setArrayIndex(g);
            const int count = settings.beginReadArray(QStringLiteral("Items"));
            for (int i = 0; i < count; ++i) {
                settings.setArrayIndex(i);
                SavedItem item;
                item.command = settings.value(QStringLiteral("command")).toString();
                if (item.command.isEmpty()) continue;
                item.id = settings.value(QStringLiteral("id")).toString();
                item.launcher = settings.value(QStringLiteral("launcher")).toString();
                item.prefix = settings.value(QStringLiteral("prefix")).toString();
                items.append(item);
            }
            settings.endArray();
        }
        settings.endArray();
    }
    return items;
}

// Where every item that runs in a prefix at all would run, counted by path.
// An item is counted under its own launcher's prefixes folder, which need not
// be the one being looked at: what makes a folder wanted is that something
// leads to it, wherever that something is set up.
QHash<QString, int> usageByPath(const OpenScreen &open) {
    QHash<QString, int> counts;
    for (const SavedItem &item : allItems(open)) {
        const Launcher *launcher = nullptr;
        const LauncherRunner *runner = nullptr;
        launcherForItem(item.command, item.launcher, &launcher, &runner);
        if (!launcher || !launcher->usesPrefixes) continue;
        const QString path =
            itemPrefixPath(item.command, launcher->prefixesDir, item.prefix, item.id);
        if (!path.isEmpty()) counts[QDir::cleanPath(path)] += 1;
    }
    return counts;
}

// Whether a folder looks like a wine prefix rather than something else that
// happens to sit in there: a prefix holds a C: drive and, once anything has
// run in it, a registry.
bool looksLikePrefix(const QString &path) {
    const QDir dir(path);
    return dir.exists(QStringLiteral("pfx")) ||          // Proton keeps one inside
           dir.exists(QStringLiteral("drive_c")) ||
           dir.exists(QStringLiteral("system.reg")) ||
           dir.exists(QLatin1String(kMarkerFile));
}

} // namespace

QVector<PrefixUse> prefixesIn(const QString &baseDir, const QStringList &names,
                              const OpenScreen &open) {
    QVector<PrefixUse> result;

    // Resolved the way a launch resolves it, so that an empty prefixes folder
    // means this application's own directory here too.
    const QString base = QDir::cleanPath(prefixesFolder(baseDir));
    const QHash<QString, int> counts = usageByPath(open);

    QStringList listed; // the paths the named ones took, so they are not listed twice
    for (const QString &name : names) {
        const QString folder = prefixFolderName(name);
        if (folder.isEmpty()) continue;
        const QString path = QDir::cleanPath(base + QLatin1Char('/') + folder);

        PrefixUse use;
        use.name = folder;
        use.path = path;
        use.kind = PrefixUse::Named;
        use.items = counts.value(path);
        // A prefix set up but not yet run in has nothing on disk, which is
        // not the same as being unused: an item may well be pointed at it.
        use.onDisk = QFileInfo::exists(path);
        result.append(use);
        listed << path;
    }

    const QDir dir(base);
    if (!dir.exists()) return result;

    // Whatever else is in there: the per-application prefixes, and anything
    // that is not one of ours at all.
    const QStringList folders =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : folders) {
        const QString path = QDir::cleanPath(dir.filePath(folder));
        if (listed.contains(path)) continue;

        PrefixUse use;
        use.name = folder;
        use.path = path;
        use.onDisk = true;
        use.items = counts.value(path);
        use.program = prefixOwner(path).program;
        // One of ours if it carries a marker, or if it has no marker but
        // holds a prefix and something still runs in it -- which is how the
        // ones built before the markers existed read.
        use.kind = (!use.program.isEmpty() || use.items > 0 || looksLikePrefix(path))
                       ? PrefixUse::PerApplication
                       : PrefixUse::Foreign;
        result.append(use);
    }

    // The prefixes items lead to that are not there yet -- a program added
    // but not yet run -- so that a count of what is in use is a count of the
    // items and not only of the folders already built.
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (QFileInfo::exists(it.key())) continue;
        if (QFileInfo(it.key()).absolutePath() != base) continue;
        if (listed.contains(it.key())) continue;

        PrefixUse use;
        use.name = QFileInfo(it.key()).fileName();
        use.path = it.key();
        use.kind = PrefixUse::PerApplication;
        use.items = it.value();
        use.onDisk = false;
        result.append(use);
    }
    return result;
}

qint64 folderSize(const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return 0;

    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
