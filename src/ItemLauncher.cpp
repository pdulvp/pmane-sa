#include "ItemLauncher.h"

#include "WindowScale.h"

#include "CompatTools.h"
#include "DesktopEntry.h"
#include "ItemKind.h"
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
    // What to tell the user if the process will not start.
    QString failureHint;
    // Set instead of all of the above when the item cannot be started at
    // all, so there is nothing to try.
    QString error;
};

// Runs the program inside gamescope, which draws it at its own size and
// scales the result. Nothing about the program's own command line changes:
// gamescope takes it whole, after a "--".
// Wine's own scaling: the program is told it is on a higher-resolution
// screen and draws itself bigger. Two things say so -- the variable wine
// reads at startup, and the DPI recorded in the prefix, which is what the
// fonts follow.
//
// Run for every wine launch, not only a scaled one: the DPI lives in the
// prefix and stays there, so turning scaling off has to put it back. Left
// alone when it is already right, which is the ordinary case and costs
// nothing to establish.
void applyWineDpi(Invocation &call, const ProgramItem &item, const Launcher &launcher,
                  const LauncherRunner &runner) {
    if (!launcher.usesDpiScale) return;

    const bool scaling = item.windowScaleMode == WindowScaleMode::WineDpi &&
                         item.windowScalePercent > 100;
    const int wanted = scaling ? wineLogPixels(item.windowScalePercent) : kDefaultLogPixels;

    if (scaling) {
        if (call.env.isEmpty()) call.env = QProcessEnvironment::systemEnvironment();
        call.env.insert(QStringLiteral("WINE_DPI_SCALE"),
                        wineDpiScaleValue(item.windowScalePercent));
    }

    setPrefixDpi(runner.command, protonPrefixFor(item.command, launcher.prefixesDir), wanted);
}

// Gamescope's scaling, which wraps the program rather than telling it
// anything. Wine's own scaling is handled by applyWineDpi(), which runs
// whether or not anything is being scaled.
void applyWindowScale(Invocation &call, const ProgramItem &item, const Launcher &launcher) {
    if (item.windowScalePercent <= 100 || !launcher.usesWindowScale) return;
    if (item.windowScaleMode != WindowScaleMode::Gamescope) return;

    const QString scale = windowScaleLabel(item.windowScalePercent);
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

    const QStringList wrapped =
        gamescopeCommand(item.nativeSize, item.windowScalePercent, call.program, call.args);
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

    // Proton keeps the wine prefix in STEAM_COMPAT_DATA_PATH and builds it
    // on first run; it refuses to start without one. The path is per
    // executable, so each program gets its own bottle -- under whichever
    // folder the launcher keeps them in.
    const QString prefix = protonPrefixFor(item.command, launcher.prefixesDir);
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

// A wine prefix per program, the way Proton gets one: two games cannot tread
// on each other's C: drive, and a prefix built for one is not disturbed by
// the next. Left to wine's own default when the launcher names no folder.
void applyWinePrefix(Invocation &call, const ProgramItem &item, const Launcher &launcher) {
    if (!launcher.usesPrefixes) return;
    const QString prefix = protonPrefixFor(item.command, launcher.prefixesDir);
    if (!QDir().mkpath(prefix)) {
        call.error = tr("Could not create the wine prefix at '%1'.").arg(prefix);
        return;
    }
    if (call.env.isEmpty()) call.env = QProcessEnvironment::systemEnvironment();
    call.env.insert(QStringLiteral("WINEPREFIX"), prefix);
}

// A .desktop file is a recipe for starting an application rather than an
// application itself. gio knows how to follow one properly -- it puts a
// Terminal=true program in a terminal, and reaches a D-Bus activated one
// over the bus -- so it does the work wherever it is installed, which on a
// desktop with .desktop files is very nearly everywhere.
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

    // No gio: run the Exec line ourselves. A program that wanted a terminal
    // of its own does not get one this way, but it does start.
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
        // Handed to the file manager the desktop has registered, the same
        // way a document goes to its own application.
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(item.command))) return {};
        return tr("Could not open '%1'. No file manager is registered for folders.")
            .arg(item.command);
    }

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
    const Launcher *launcher = nullptr;
    const LauncherRunner *runner = nullptr;
    launcherForItem(item.command, item.launcher, &launcher, &runner);

    if (isDesktopEntry(item.command)) {
        call = desktopEntryInvocation(item, extraArgs);
    } else if (!launcher || !runner) {
        call.error = tr("Nothing is set up to run '%1'. Options > Launchers says which file "
                        "types each launcher takes.")
                         .arg(item.command);
    } else if (runner->isCompatTool()) {
        // A Proton build is a runner like any other to pick, but not to
        // start: it wants a prefix of its own and the environment that
        // points at it, so its command line is not what runs.
        call = protonInvocation(item, *launcher, *runner, extraArgs);
    } else if (runner->command.isEmpty()) {
        call.error = tr("'%1' names no program to run '%2' with. Options > Launchers is "
                        "where the programs it can use are listed.")
                         .arg(launcher->name, item.command);
    } else {
        // The runner's program, the launcher's arguments with the item's path
        // filled in, and the interactive shell around it when the program is
        // an alias.
        const LauncherCommand command =
            launcherCommandFor(*launcher, *runner, item.command, extraArgs);
        call.program = command.program;
        call.args = command.args;
        // Named by the program actually started, which is what would have to
        // be put right.
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
    // A .desktop file lives in /usr/share/applications, which is no place to
    // start an application: it runs from home unless the launcher named a
    // directory of its own, which completeProgramItem() will have picked up.
    const QString fallbackDir = isDesktopEntry(item.command)
                                    ? QDir::homePath()
                                    : QFileInfo(item.command).absolutePath();
    process.setWorkingDirectory(item.workingDir.isEmpty() ? fallbackDir : item.workingDir);
    if (!call.env.isEmpty()) process.setProcessEnvironment(call.env);

    return process.startDetached() ? QString() : call.failureHint;
}
