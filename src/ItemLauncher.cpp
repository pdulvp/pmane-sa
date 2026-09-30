#include "ItemLauncher.h"

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

Invocation protonInvocation(const ProgramItem &item, const LauncherProfile &profile,
                            const QStringList &extraArgs) {
    Invocation call;

    const CompatTool *tool = compatToolById(profile.compatToolId);
    if (!tool) {
        call.error = tr("'%1' is set to run with '%2', whose Proton build ('%3') is no longer "
                        "installed under Steam's compatibilitytools.d.")
                         .arg(item.name, profile.name, profile.compatToolId);
        return call;
    }

    // Proton keeps the wine prefix in STEAM_COMPAT_DATA_PATH and builds it
    // on first run; it refuses to start without one. The path is per
    // executable, so each program gets its own bottle -- under whichever
    // folder the profile keeps them in.
    const QString prefix = protonPrefixFor(item.command, profile.prefixesDir);
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
    const LauncherProfile *profile = nullptr;
    launcherForItem(item.command, item.launcher, &launcher, &profile);

    if (isDesktopEntry(item.command)) {
        call = desktopEntryInvocation(item, extraArgs);
    } else if (!launcher || !profile) {
        call.error = tr("Nothing is set up to run '%1'. Options > Launchers says which file "
                        "types each launcher takes.")
                         .arg(item.command);
    } else if (launcher->usesCompatTools && !profile->compatToolId.isEmpty()) {
        // A Proton build is not simply another program to run: it wants a
        // prefix of its own and the environment that points at it.
        call = protonInvocation(item, *profile, extraArgs);
    } else {
        // The profile's own program, its arguments with the item's path
        // filled in, and the interactive shell around it when the program is
        // an alias.
        const LauncherCommand command =
            launcherCommandFor(*launcher, *profile, item.command, extraArgs);
        call.program = command.program;
        call.args = command.args;
        call.failureHint = tr("Could not launch '%1' with %2 ('%3').")
                               .arg(item.command, profile->name, launcher->command);
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
