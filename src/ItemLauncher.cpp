#include "ItemLauncher.h"

#include "WindowScale.h"

#include "CompatTools.h"
#include "DesktopEntry.h"
#include "ItemDetails.h"
#include "ItemKind.h"
#include "LaunchNotices.h"
#include "Launchers.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("ItemLauncher", text);
}

// What it takes to start one item.
struct Invocation {
    QString program;
    QStringList args;
    // Left empty to inherit ours; only Proton needs anything added.
    QProcessEnvironment env;
    // The message to show if the process will not start.
    QString failureHint;
    // Set instead of all of the above when the item cannot be started at all,
    // so there is nothing to attempt.
    QString error;
};

// Wine's own scaling: the program is told it is on a higher-resolution screen
// and draws itself larger. The DPI recorded in the prefix is the only thing
// that controls this; WINE_DPI_SCALE, often cited as the way to do it, is read
// by no part of wine.
//
// Run for every wine launch, not only a scaled one: the DPI is stored in the
// prefix and persists, so turning scaling off has to reset it. Left alone when
// it is already correct, which is the usual case and cheap to check.
void applyWineDpi(Invocation &call, const ProgramItem &item, const Launcher &launcher,
                  const LauncherRunner &runner) {
    if (!launcher.usesDpiScale) return;

    const bool scaling = item.windowScaleMode == WindowScaleMode::WineDpi &&
                         item.windowScalePercent > 100;
    const int wanted = scaling ? wineLogPixels(item.windowScalePercent) : kDefaultLogPixels;

    setPrefixDpi(runner.command,
                 itemPrefixFor(item.command, launcher.prefixesDir, item.prefix, item.id),
                 wanted);
}

// Gamescope's scaling: the program is wrapped rather than told anything, and
// draws at its own size while gamescope scales the result. The program's
// command line is unchanged, gamescope taking it whole after a "--". Wine's own
// scaling is handled by applyWineDpi(), which runs whether or not anything is
// being scaled.
void applyWindowScale(Invocation &call, const ProgramItem &item, const Launcher &launcher) {
    if (!launcher.usesWindowScale) return;
    if (item.windowScaleMode != WindowScaleMode::Gamescope) return;
    // A shader needs gamescope just as a scale does.
    if (item.windowScalePercent <= 100 && item.shader.isEmpty()) return;

    const QString scale = item.windowScalePercent > 100
                              ? windowScaleLabel(item.windowScalePercent)
                              : tr("its own size with a shader");
    if (!gamescopeAvailable()) {
        call.error = tr("'%1' is set to run at %2 its own size, which needs gamescope. It "
                        "is not installed.")
                         .arg(item.name, scale);
        return;
    }
    if (!item.nativeSize.isValid() || item.nativeSize.isEmpty()) {
        call.error = tr("'%1' is set to run at %2 its own size, but that size is not known "
                        "yet. Its properties has a Measure button, which runs it once to "
                        "find out.")
                         .arg(item.name, scale);
        return;
    }

    const QStringList wrapped = gamescopeCommand(item.nativeSize, item.windowScalePercent,
                                                item.shader, call.program, call.args);
    if (wrapped.isEmpty()) return;
    call.program = QStringLiteral("gamescope");
    call.args = wrapped;
}

Invocation protonInvocation(const ProgramItem &item, const Launcher &launcher,
                            const LauncherRunner &runner, const QStringList &extraArgs) {
    Invocation call;

    const CompatTool *tool = compatToolById(runner.compatToolId);
    if (!tool) {
        call.error = tr("'%1' is set to run with the Proton build '%2', which is no longer "
                        "installed under Steam's compatibilitytools.d.")
                         .arg(item.name, runner.compatToolId);
        return call;
    }

    // Proton takes the wine prefix from STEAM_COMPAT_DATA_PATH and creates it
    // on first run; it refuses to start without one. The path is per
    // executable, so each program gets its own prefix under whichever folder
    // the launcher uses.
    const QString prefix =
        itemPrefixFor(item.command, launcher.prefixesDir, item.prefix, item.id);
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
    applyWindowScale(call, item, launcher);
    return call;
}

// One wine prefix per program, as Proton gets: two games cannot interfere with
// each other's C: drive. Left to wine's own default when the launcher names no
// folder.
void applyWinePrefix(Invocation &call, const ProgramItem &item, const Launcher &launcher) {
    if (!launcher.usesPrefixes) return;
    const QString prefix =
        itemPrefixFor(item.command, launcher.prefixesDir, item.prefix, item.id);
    if (!QDir().mkpath(prefix)) {
        call.error = tr("Could not create the wine prefix at '%1'.").arg(prefix);
        return;
    }
    if (call.env.isEmpty()) call.env = QProcessEnvironment::systemEnvironment();
    call.env.insert(QStringLiteral("WINEPREFIX"), prefix);
}

// A .desktop file describes how to start an application rather than being one.
// gio follows one correctly, running a Terminal=true program in a terminal and
// reaching a D-Bus activated one over the bus, so it is used wherever it is
// installed, which is almost everywhere .desktop files are used.
//
// The item's own arguments are passed through: gio hands them to the
// application the entry describes, which is how an item opens a particular
// file or starts in a particular mode.
Invocation desktopEntryInvocation(const ProgramItem &item, const QStringList &extraArgs) {
    Invocation call;

    const DesktopEntry entry = readDesktopEntry(item.command);
    if (!entry.isValid()) {
        call.error = tr("'%1' names no program to run.").arg(item.command);
        return call;
    }

    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!gio.isEmpty()) {
        call.program = gio;
        call.args = QStringList{QStringLiteral("launch"), item.command} + extraArgs;
        call.failureHint = tr("Could not launch '%1'.").arg(item.name);
        return call;
    }

    // No gio: run the Exec line directly. A program asking for a terminal does
    // not get one this way, but it does start.
    QStringList command = desktopEntryCommand(entry);
    if (command.isEmpty()) {
        call.error = tr("'%1' names no program to run.").arg(item.command);
        return call;
    }
    call.program = command.takeFirst();
    call.args = command + extraArgs;
    call.failureHint = tr("Could not launch '%1'.").arg(item.name);
    return call;
}

} // namespace

QString launchProgramItem(const ProgramItem &item) {
    if (isFolder(item.command)) {
        // Handed to the file manager the desktop has registered, as a document
        // is handed to its own application.
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(item.command))) return {};
        return tr("Could not open '%1'. No file manager is registered for folders.")
            .arg(item.command);
    }

    if (isDocument(item.command)) {
        // Opened by whatever the desktop has registered for the type. This is
        // the one path with no command line of ours, so the item's arguments
        // have nowhere to go and are not used.
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(item.command))) return {};
        return tr("Could not open '%1'. No application is registered for this file type.")
            .arg(item.command);
    }

    Invocation call;
    const Launcher *launcher = nullptr;
    const LauncherRunner *runner = nullptr;
    launcherForItem(item.command, item.launcher, &launcher, &runner);

    // The launchers whose arguments reach the program: wine and Proton, which
    // pass them to the Windows program, and a .desktop file, whose application
    // receives them from gio. The others are each given a file or an id they
    // determine themselves, where anything appended would be meaningless,
    // which is why the properties dialog hides the field for them.
    const bool takesArguments =
        isDesktopEntry(item.command) || (launcher && launcher->usesArguments);
    const QStringList extraArgs =
        takesArguments ? QProcess::splitCommand(item.arguments) : QStringList();

    if (isDesktopEntry(item.command)) {
        call = desktopEntryInvocation(item, extraArgs);
    } else if (!launcher || !runner) {
        call.error = tr("Nothing is set up to run '%1'. Options > Launchers says which file "
                        "types each launcher takes.")
                         .arg(item.command);
    } else if (runner->isCompatTool()) {
        // A Proton build is selected like any other runner but started
        // differently: it needs a prefix of its own and the environment
        // pointing at it, so its command line is not what runs.
        call = protonInvocation(item, *launcher, *runner, extraArgs);
    } else if (runner->command.isEmpty()) {
        call.error = tr("'%1' names no program to run '%2' with. Options > Launchers is "
                        "where the programs it can use are listed.")
                         .arg(launcher->name, item.command);
    } else {
        // The runner's program, the launcher's arguments with the item's path
        // substituted in, and an interactive shell around it when the program
        // is an alias.
        const LauncherCommand command =
            launcherCommandFor(*launcher, *runner, item.command, extraArgs);
        call.program = command.program;
        call.args = command.args;
        // Names the program actually started, which is what would have to be
        // fixed.
        call.failureHint =
            tr("Could not launch '%1' with %2.").arg(item.command, runner->name);
        applyWinePrefix(call, item, *launcher);
        applyWineDpi(call, item, *launcher, *runner);
        applyWindowScale(call, item, *launcher);
    }

    if (!call.error.isEmpty()) return call.error;

    QProcess process;
    process.setProgram(call.program);
    process.setArguments(call.args);
    // The working directory, which is the item file's own folder for nearly
    // everything and is not configurable.
    process.setWorkingDirectory(workingDirFor(item));
    if (!call.env.isEmpty()) process.setProcessEnvironment(call.env);

    // The program's output, captured while it runs. Being started detached, it
    // used to leave no trace at all when it failed in its first second, which
    // is the hardest kind of failure to diagnose. The file is per item and
    // truncated at each launch, so the last run can be inspected and nothing
    // accumulates.
    //
    // Only where the process is the program itself: gio hands a .desktop file
    // to the application and exits, as does the handler for Steam's url, so
    // their exit says nothing.
    const bool ourProcess = launcher && !launcher->usesSteamLibraries;
    const QString log = ourProcess ? launchLogPath(item.name, item.id) : QString();
    if (!log.isEmpty()) {
        process.setStandardOutputFile(log, QIODevice::Truncate);
        process.setStandardErrorFile(log, QIODevice::Append);
    }

    qint64 pid = 0;
    if (!process.startDetached(&pid)) return call.failureHint;

    LaunchNotices::instance()->watch(item.name, pid, log);
    return {};
}
