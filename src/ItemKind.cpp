#include "ItemKind.h"

#include <QStringList>

bool isWindowsExecutable(const QString &path) {
    return path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
}

bool isDosBoxConfig(const QString &path) {
    return path.endsWith(QLatin1String(".conf"), Qt::CaseInsensitive);
}

bool isScummVmGame(const QString &path) {
    return path.endsWith(QLatin1String(".trs"), Qt::CaseInsensitive);
}

bool isRetroArchRom(const QString &path) {
    static const QStringList extensions = {
        QStringLiteral(".nes"), QStringLiteral(".z64"), QStringLiteral(".n64"),
        QStringLiteral(".sfc"), QStringLiteral(".gb"),  QStringLiteral(".gba"),
        QStringLiteral(".gcm"), QStringLiteral(".iso"),
    };
    for (const QString &extension : extensions) {
        if (path.endsWith(extension, Qt::CaseInsensitive)) return true;
    }
    return false;
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
    return isWindowsExecutable(path) || isDosBoxConfig(path) || isScummVmGame(path) ||
           isRetroArchRom(path) || isDesktopEntry(path) || isDocument(path);
}
