#include "ItemLauncher.h"

#include "CompatTools.h"
#include "ItemKind.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUrl>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("ItemLauncher", text);
}

// RetroArch does not pick a core from the content on its own, and ignores a
// bare core name, so the full path to one has to be passed. Rather than
// hard-coding where the cores live, read it back out of the retroarch alias
// and cache it for the session.
QString retroArchCoresDir() {
    static const QString dir = []() -> QString {
        QProcess probe;
        probe.start(QStringLiteral("bash"), {QStringLiteral("-ic"), QStringLiteral("alias retroarch")});
        if (!probe.waitForFinished(3000)) return {};

        const QString definition = QString::fromLocal8Bit(probe.readAllStandardOutput());
        static const QRegularExpression binary(QStringLiteral("(/[^\\s'\"]+)/retroarch\\b"));
        const QRegularExpressionMatch match = binary.match(definition);
        if (!match.hasMatch()) return {};

        const QString cores = match.captured(1) + QStringLiteral("/cores");
        return QDir(cores).exists() ? cores : QString();
    }();
    return dir;
}

// The libretro core for a ROM, by extension. Core file names are standard
// across libretro installs; only the directory is site-specific.
QString retroArchCoreFor(const QString &romPath) {
    static const QHash<QString, QString> coreByExtension = {
        {QStringLiteral("nes"), QStringLiteral("mesen_libretro.so")},
        {QStringLiteral("gba"), QStringLiteral("mgba_libretro.so")},
        {QStringLiteral("gb"), QStringLiteral("gearboy_libretro.so")},
        {QStringLiteral("sfc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("smc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("n64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("z64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("v64"), QStringLiteral("mupen64plus_next_libretro.so")},
    };

    const QString core = coreByExtension.value(QFileInfo(romPath).suffix().toLower());
    if (core.isEmpty()) return {};

    const QString dir = retroArchCoresDir();
    if (dir.isEmpty()) return {};

    const QString path = QDir(dir).filePath(core);
    return QFile::exists(path) ? path : QString();
}

// What it takes to start one item.
struct Invocation {
    QString program;
    QStringList args;
    // Left empty to inherit ours; only Proton needs anything added.
    QProcessEnvironment env;
    // What to tell the user if the process will not start.
    QString failureHint;
    // Set instead of all of the above when the item cannot be started at
    // all, so there is nothing to try.
    QString error;
};

Invocation protonInvocation(const ProgramItem &item, const QStringList &extraArgs) {
    Invocation call;

    const CompatTool *tool = compatToolById(item.runner);
    if (!tool) {
        call.error = tr("'%1' is set to run with '%2', which is no longer installed under "
                        "Steam's compatibilitytools.d.")
                         .arg(item.name, item.runner);
        return call;
    }

    // Proton keeps the wine prefix in STEAM_COMPAT_DATA_PATH and builds it
    // on first run; it refuses to start without one. The path is per
    // executable, so each program gets its own bottle.
    const QString prefix = protonPrefixFor(item.command);
    if (!QDir().mkpath(prefix)) {
        call.error = tr("Could not create the Proton prefix at '%1'.").arg(prefix);
        return call;
    }

    call.program = tool->protonPath;
    call.args = QStringList{QStringLiteral("run"), item.command} + extraArgs;
    call.failureHint = tr("Could not launch '%1' with %2.").arg(item.command, tool->displayName);
    call.env = QProcessEnvironment::systemEnvironment();
    call.env.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"), prefix);
    // Proton reads this to find the runtime beside the Steam client.
    call.env.insert(QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"), tool->steamPath);
    return call;
}

} // namespace

QString launchProgramItem(const ProgramItem &item) {
    if (isDocument(item.command)) {
        // Opened by whatever the desktop has registered for the type, which
        // is the one launcher here that is not a command of our own -- so
        // the item's arguments have nowhere to go and are not used.
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(item.command))) return {};
        return tr("Could not open '%1'. No application is registered for this file type.")
            .arg(item.command);
    }

    const QStringList extraArgs = QProcess::splitCommand(item.arguments);

    Invocation call;
    if (isWindowsExecutable(item.command) && !item.runner.isEmpty()) {
        call = protonInvocation(item, extraArgs);
    } else if (isDosBoxConfig(item.command)) {
        // `db` is a shell alias, so it exists only in an interactive bash.
        // The path goes through as a positional argument so names with
        // spaces survive and nothing is re-parsed by the shell.
        call.program = QStringLiteral("bash");
        call.args = QStringList{QStringLiteral("-ic"), QStringLiteral("db -conf \"$@\""),
                                QStringLiteral("_"), item.command} +
                    extraArgs;
        call.failureHint =
            tr("Could not launch '%1' with dosbox (the 'db' alias).").arg(item.command);
    } else if (isScummVmGame(item.command)) {
        // `scm` is a shell alias too, and it wants the folder holding the
        // game data rather than the file itself.
        call.program = QStringLiteral("bash");
        call.args = QStringList{QStringLiteral("-ic"), QStringLiteral("scm -p \"$1\" \"${@:2}\""),
                                QStringLiteral("_"), QFileInfo(item.command).absolutePath()} +
                    extraArgs;
        call.failureHint =
            tr("Could not launch '%1' with ScummVM (the 'scm' alias).").arg(item.command);
    } else if (isRetroArchRom(item.command)) {
        // Another shell alias, taking the ROM itself. The alias pins one
        // core, so pass the right one for this ROM when we can identify it;
        // a later -L overrides the alias's own --libretro.
        call.program = QStringLiteral("bash");
        const QString core = retroArchCoreFor(item.command);
        if (core.isEmpty()) {
            call.args = QStringList{QStringLiteral("-ic"), QStringLiteral("retroarch \"$@\""),
                                    QStringLiteral("_"), item.command};
        } else {
            call.args =
                QStringList{QStringLiteral("-ic"), QStringLiteral("retroarch -L \"$1\" \"$2\""),
                            QStringLiteral("_"), core, item.command};
        }
        call.args += extraArgs;
        call.failureHint =
            tr("Could not launch '%1' with RetroArch (the 'retroarch' alias).").arg(item.command);
    } else {
        call.program = QStringLiteral("wine");
        call.args = QStringList{item.command} + extraArgs;
        call.failureHint = tr("Could not launch '%1' with wine.").arg(item.command);
    }

    if (!call.error.isEmpty()) return call.error;

    QProcess process;
    process.setProgram(call.program);
    process.setArguments(call.args);
    process.setWorkingDirectory(item.workingDir.isEmpty()
                                    ? QFileInfo(item.command).absolutePath()
                                    : item.workingDir);
    if (!call.env.isEmpty()) process.setProcessEnvironment(call.env);

    return process.startDetached() ? QString() : call.failureHint;
}
