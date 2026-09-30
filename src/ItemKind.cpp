#include "ItemKind.h"

#include "Runners.h"

#include <QFileInfo>
#include <QStringList>

namespace {
// Which file types belong to a kind is the runner's business now: it is set
// in Options > Runners, so a machine that plays its ROMs from a different
// extension says so there rather than here.
bool claimedBy(const char *id, const QString &path) {
    const Runner *runner = runnerById(QLatin1String(id));
    return runner && runner->handles(path);
}
} // namespace

bool isWindowsExecutable(const QString &path) {
    return claimedBy("wine", path);
}

bool isDosBoxConfig(const QString &path) {
    return claimedBy("dosbox", path);
}

bool isScummVmGame(const QString &path) {
    return claimedBy("scummvm", path);
}

bool isRetroArchRom(const QString &path) {
    return claimedBy("retroarch", path);
}

bool isFolder(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) return info.isDir();
    return info.suffix().isEmpty();
}

bool isDesktopEntry(const QString &path) {
    return path.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive);
}

bool isDocument(const QString &path) {
    static const QStringList extensions = {
        QStringLiteral(".pdf"),
        QStringLiteral(".cbz"),
    };
    for (const QString &extension : extensions) {
        if (path.endsWith(extension, Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool isLaunchable(const QString &path) {
    return runnerForPath(path) != nullptr || isDesktopEntry(path) || isDocument(path) ||
           isFolder(path);
}
