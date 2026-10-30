#include "LibretroCores.h"

#include "Launchers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QTextStream>

#include <algorithm>

namespace {

// The `key = "value"` lines of a retroarch.cfg or a core's .info file, which
// are the same format. Comment lines and anything without a `=` are skipped,
// and the quotes around a value are not part of it.
QHash<QString, QString> readKeyValues(const QString &path) {
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return values;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int split = line.indexOf(QLatin1Char('='));
        if (split <= 0) continue;

        const QString key = line.left(split).trimmed();
        QString value = line.mid(split + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
            value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        values.insert(key, value);
    }
    return values;
}

// A folder as a retroarch.cfg writes it: ":" is the folder the cfg itself sits
// in, which is how a portable install refers to its own directories, and "~"
// is the home. An empty value stays empty rather than becoming the home.
QString resolveConfigPath(const QString &value, const QString &configDir) {
    if (value.isEmpty()) return {};
    if (value.startsWith(QLatin1Char(':'))) {
        return QDir::cleanPath(configDir + QLatin1Char('/') + value.mid(1));
    }
    if (value.startsWith(QLatin1Char('~'))) {
        return QDir::cleanPath(QDir::homePath() + value.mid(1));
    }
    return QDir::cleanPath(value);
}

// The retroarch.cfg of one install, which is what knows where its cores are.
// Looked for where RetroArch itself would have it: the `--config` it was
// started with, which is how a Lutris or a flatpak build is pointed at its own
// one, then the user's, then one beside the program for a portable install.
QString configFileFor(const LauncherRunner &runner) {
    const QStringList command = QProcess::splitCommand(resolvedRunnerCommand(runner));
    for (int i = 0; i < command.size(); ++i) {
        const QString &argument = command.at(i);
        QString path;
        if (argument.startsWith(QLatin1String("--config="))) {
            path = argument.mid(9);
        } else if ((argument == QLatin1String("--config") || argument == QLatin1String("-c")) &&
                   i + 1 < command.size()) {
            path = command.at(i + 1);
        }
        if (!path.isEmpty()) {
            path = resolveConfigPath(path, QFileInfo(command.first()).absolutePath());
            if (QFile::exists(path)) return path;
        }
    }

    const QString own = QDir::homePath() + QStringLiteral("/.config/retroarch/retroarch.cfg");
    if (QFile::exists(own)) return own;

    const QString binary = resolvedRunnerBinary(runner);
    if (binary.isEmpty()) return {};
    const QString beside =
        QFileInfo(binary).absolutePath() + QStringLiteral("/retroarch.cfg");
    return QFile::exists(beside) ? beside : QString();
}

// Where to look for the cores of an install and for their .info files. The cfg
// names both; without one, the cores folder is the configured or derived one
// and the info files are looked for beside it, which is how every libretro
// install lays them out.
void coreFolders(const Launcher &launcher, const LauncherRunner &runner, QString *coresDir,
                 QString *infoDir) {
    const QString config = configFileFor(runner);
    if (!config.isEmpty()) {
        const QString configDir = QFileInfo(config).absolutePath();
        const QHash<QString, QString> values = readKeyValues(config);
        *coresDir =
            resolveConfigPath(values.value(QStringLiteral("libretro_directory")), configDir);
        *infoDir =
            resolveConfigPath(values.value(QStringLiteral("libretro_info_path")), configDir);
    }

    if (coresDir->isEmpty()) *coresDir = coresFolderFor(launcher, runner);
    if (infoDir->isEmpty() && !coresDir->isEmpty()) {
        // Beside the cores, which is where RetroArch puts them when the cfg
        // says nothing: `<install>/cores` and `<install>/info`.
        const QString beside =
            QFileInfo(*coresDir).absolutePath() + QStringLiteral("/info");
        if (QDir(beside).exists()) *infoDir = beside;
    }
}

QVector<LibretroCore> scanCores(const Launcher &launcher, const LauncherRunner &runner) {
    QString coresDir;
    QString infoDir;
    coreFolders(launcher, runner, &coresDir, &infoDir);
    if (coresDir.isEmpty()) return {};

    QVector<LibretroCore> cores;
    const QFileInfoList files =
        QDir(coresDir).entryInfoList({QStringLiteral("*_libretro.so"), QStringLiteral("*.so")},
                                     QDir::Files, QDir::Name);
    QStringList seen;
    for (const QFileInfo &file : files) {
        if (seen.contains(file.fileName())) continue;
        seen << file.fileName();

        LibretroCore core;
        core.file = file.fileName();
        // The .info is named after the core, so one install's descriptions
        // cannot be read for another's cores.
        const QHash<QString, QString> info = infoDir.isEmpty()
            ? QHash<QString, QString>()
            : readKeyValues(infoDir + QLatin1Char('/') + file.completeBaseName() +
                            QStringLiteral(".info"));

        core.displayName = info.value(QStringLiteral("display_name"));
        core.systemName = info.value(QStringLiteral("systemname"));
        for (const QString &extension :
             info.value(QStringLiteral("supported_extensions")).split(QLatin1Char('|'),
                                                                     Qt::SkipEmptyParts)) {
            core.extensions << extension.trimmed().toLower();
        }

        // A core whose .info is missing is still listed, under its file name:
        // it can be chosen by hand even though nothing says what it plays.
        if (core.displayName.isEmpty()) core.displayName = file.completeBaseName();
        cores.append(core);
    }

    std::sort(cores.begin(), cores.end(), [](const LibretroCore &a, const LibretroCore &b) {
        return a.displayName.localeAwareCompare(b.displayName) < 0;
    });
    return cores;
}

} // namespace

bool LibretroCore::handles(const QString &extension) const {
    return extensions.contains(extension.toLower());
}

QVector<LibretroCore> libretroCores(const Launcher &launcher, const LauncherRunner &runner) {
    if (!launcher.usesCores) return {};

    // Keyed on the runner and the program behind it, so editing a runner's
    // command in the Launchers dialog is noticed while the cached scan of an
    // untouched one is kept.
    static QHash<QString, QVector<LibretroCore>> cache;
    const QString key = launcher.id + QLatin1Char('/') + runner.id + QLatin1Char('|') +
                        runner.command;
    const auto found = cache.constFind(key);
    if (found != cache.constEnd()) return *found;

    const QVector<LibretroCore> cores = scanCores(launcher, runner);
    cache.insert(key, cores);
    return cores;
}

QVector<LibretroCore> libretroCoresFor(const Launcher &launcher, const LauncherRunner &runner,
                                       const QString &path) {
    const QString extension = QFileInfo(path).suffix().toLower();
    if (extension.isEmpty()) return {};

    QVector<LibretroCore> playing;
    for (const LibretroCore &core : libretroCores(launcher, runner)) {
        if (core.handles(extension)) playing.append(core);
    }
    return playing;
}
