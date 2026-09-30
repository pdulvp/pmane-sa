#include "Runners.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Runners", text);
}

Runner makeRunner(const QString &id, const QString &name, const QString &description,
                  const QString &command, const QString &arguments, bool useShell,
                  const QStringList &extensions) {
    Runner runner;
    runner.id = id;
    runner.name = name;
    runner.description = description;
    runner.command = command;
    runner.arguments = arguments;
    runner.useShell = useShell;
    runner.extensions = extensions;
    return runner;
}

// Where RetroArch keeps its cores, worked out from the program itself when
// no folder has been given: a libretro install puts them in a `cores`
// directory beside the binary. An alias is asked what it stands for, which
// is the one thing only the shell knows.
QString coresDirFor(const Runner &runner) {
    if (!runner.coresDir.isEmpty()) return runner.coresDir;

    const QString found = QStandardPaths::findExecutable(runner.command);
    const QString binary =
        !found.isEmpty()
            ? found
            : [&runner]() -> QString {
                  if (!runner.useShell) return {};
                  QProcess probe;
                  probe.start(QStringLiteral("bash"),
                              {QStringLiteral("-ic"),
                               QStringLiteral("alias %1").arg(runner.command)});
                  if (!probe.waitForFinished(3000)) return {};
                  const QString definition =
                      QString::fromLocal8Bit(probe.readAllStandardOutput());
                  static const QRegularExpression path(QStringLiteral("(/[^\\s'\"]+)"));
                  const QRegularExpressionMatch match = path.match(definition);
                  return match.hasMatch() ? match.captured(1) : QString();
              }();
    if (binary.isEmpty()) return {};

    const QString cores = QFileInfo(binary).absolutePath() + QStringLiteral("/cores");
    return QDir(cores).exists() ? cores : QString();
}

// The core for this file, as a full path: the cores directory plus whatever
// the runner's table says this extension is played by. Empty when either is
// unknown, which leaves the runner to start without one.
QString coreFor(const Runner &runner, const QString &path) {
    if (!runner.usesCores) return {};

    const QString suffix = QFileInfo(path).suffix().toLower();
    QString core;
    for (const auto &entry : runner.cores) {
        if (entry.first.compare(suffix, Qt::CaseInsensitive) == 0) {
            core = entry.second;
            break;
        }
    }
    if (core.isEmpty()) return {};

    // A core given as a path of its own is taken as it stands.
    if (core.startsWith(QLatin1Char('/'))) return QFile::exists(core) ? core : QString();

    const QString dir = coresDirFor(runner);
    if (dir.isEmpty()) return {};
    const QString full = QDir(dir).filePath(core);
    return QFile::exists(full) ? full : QString();
}

QStringList expand(const QString &arguments, const QString &file, const QString &core) {
    QStringList out;
    // Split first and substitute after, so that a path with spaces in it
    // stays one argument however the template was written.
    for (QString piece : QProcess::splitCommand(arguments)) {
        piece.replace(QLatin1String("{file}"), file);
        piece.replace(QLatin1String("{dir}"), QFileInfo(file).absolutePath());
        piece.replace(QLatin1String("{core}"), core);
        out << piece;
    }
    return out;
}

QVector<Runner> *loaded() {
    static QVector<Runner> runners;
    return &runners;
}

QString settingsKey(const Runner &runner, const char *field) {
    return QStringLiteral("runners/%1/%2").arg(runner.id, QLatin1String(field));
}

void load() {
    QVector<Runner> result = defaultRunners();
    QSettings settings;
    for (Runner &runner : result) {
        const QString marker = settingsKey(runner, "command");
        if (!settings.contains(marker)) continue; // never edited: keep the default

        runner.command = settings.value(marker).toString();
        runner.arguments = settings.value(settingsKey(runner, "arguments")).toString();
        runner.useShell = settings.value(settingsKey(runner, "useShell")).toBool();
        runner.extensions = settings.value(settingsKey(runner, "extensions")).toStringList();
        runner.prefixesDir = settings.value(settingsKey(runner, "prefixesDir")).toString();
        if (runner.usesCores) {
            runner.coreArguments =
                settings.value(settingsKey(runner, "coreArguments")).toString();
            runner.coresDir = settings.value(settingsKey(runner, "coresDir")).toString();
            const QStringList pairs =
                settings.value(settingsKey(runner, "cores")).toStringList();
            QVector<QPair<QString, QString>> cores;
            for (const QString &pair : pairs) {
                const int split = pair.indexOf(QLatin1Char('='));
                if (split <= 0) continue;
                cores.append({pair.left(split), pair.mid(split + 1)});
            }
            if (!cores.isEmpty()) runner.cores = cores;
        }
    }
    *loaded() = result;
}

} // namespace

bool Runner::handles(const QString &path) const {
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.isEmpty()) return false;
    for (const QString &extension : extensions) {
        if (extension.compare(suffix, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QVector<Runner> defaultRunners() {
    QVector<Runner> result;

    result << makeRunner(QStringLiteral("wine"), tr("Wine"),
                         tr("Windows programs, unless the item names a Proton build of its "
                            "own on its properties page."),
                         QStringLiteral("wine"), QStringLiteral("{file}"), false,
                         {QStringLiteral("exe")});

    result << makeRunner(QStringLiteral("dosbox"), tr("DOSBox"),
                         tr("DOS games, started from the .conf file that sets one up."),
                         QStringLiteral("db"), QStringLiteral("-conf {file}"), true,
                         {QStringLiteral("conf")});

    result << makeRunner(QStringLiteral("scummvm"), tr("ScummVM"),
                         tr("Point-and-click adventures. ScummVM is handed the folder the "
                            "game data sits in rather than the file itself."),
                         QStringLiteral("scm"), QStringLiteral("-p {dir}"), true,
                         {QStringLiteral("trs")});

    Runner retroarch = makeRunner(
        QStringLiteral("retroarch"), tr("RetroArch"),
        tr("Console ROMs and disc images. RetroArch will not pick a core from the file on "
           "its own, so one is named for it."),
        QStringLiteral("retroarch"), QStringLiteral("{file}"), true,
        {QStringLiteral("nes"), QStringLiteral("z64"), QStringLiteral("n64"),
         QStringLiteral("sfc"), QStringLiteral("smc"), QStringLiteral("gb"),
         QStringLiteral("gba"), QStringLiteral("gcm"), QStringLiteral("iso")});
    retroarch.usesCores = true;
    retroarch.coreArguments = QStringLiteral("-L {core} {file}");
    retroarch.cores = {
        {QStringLiteral("nes"), QStringLiteral("mesen_libretro.so")},
        {QStringLiteral("gba"), QStringLiteral("mgba_libretro.so")},
        {QStringLiteral("gb"), QStringLiteral("gearboy_libretro.so")},
        {QStringLiteral("sfc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("smc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("n64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("z64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("v64"), QStringLiteral("mupen64plus_next_libretro.so")},
    };
    result << retroarch;

    return result;
}

const QVector<Runner> &runners() {
    if (loaded()->isEmpty()) load();
    return *loaded();
}

void setRunners(const QVector<Runner> &updated) {
    QSettings settings;
    for (const Runner &runner : updated) {
        settings.setValue(settingsKey(runner, "command"), runner.command);
        settings.setValue(settingsKey(runner, "arguments"), runner.arguments);
        settings.setValue(settingsKey(runner, "useShell"), runner.useShell);
        settings.setValue(settingsKey(runner, "extensions"), runner.extensions);
        settings.setValue(settingsKey(runner, "prefixesDir"), runner.prefixesDir);
        if (!runner.usesCores) continue;

        settings.setValue(settingsKey(runner, "coreArguments"), runner.coreArguments);
        settings.setValue(settingsKey(runner, "coresDir"), runner.coresDir);
        QStringList pairs;
        for (const auto &entry : runner.cores) {
            pairs << entry.first + QLatin1Char('=') + entry.second;
        }
        settings.setValue(settingsKey(runner, "cores"), pairs);
    }
    *loaded() = updated;
}

const Runner *runnerById(const QString &id) {
    for (const Runner &runner : runners()) {
        if (runner.id == id) return &runner;
    }
    return nullptr;
}

const Runner *runnerForPath(const QString &path) {
    for (const Runner &runner : runners()) {
        if (runner.handles(path)) return &runner;
    }
    return nullptr;
}

RunnerCommand runnerCommandFor(const Runner &runner, const QString &path,
                               const QStringList &extraArgs) {
    const QString core = coreFor(runner, path);
    const QString &templateArgs =
        (!core.isEmpty() && !runner.coreArguments.isEmpty()) ? runner.coreArguments
                                                            : runner.arguments;
    QStringList args = expand(templateArgs, path, core) + extraArgs;

    RunnerCommand call;
    if (!runner.useShell) {
        call.program = runner.command;
        call.args = args;
        return call;
    }

    // An alias or a shell function exists only inside an interactive shell,
    // so one is started to run it. The arguments go through as positional
    // parameters rather than being written into the command line, so that
    // nothing in them is read as shell syntax.
    call.program = QStringLiteral("bash");
    call.args = QStringList{QStringLiteral("-ic"),
                            runner.command + QStringLiteral(" \"$@\""), QStringLiteral("_")} +
                args;
    return call;
}
