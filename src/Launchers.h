#pragma once

#include "LibretroCores.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// One program that can act as a launcher: an installed wine, a Proton build,
// DOSBox, a ScummVM outside the PATH. An item picks one of these. It holds no
// command line: the arguments a launcher needs belong to the launcher and are
// not editable.
struct LauncherRunner {
    QString id;      // unique within its launcher, and what an item stores
    QString name;    // shown in the dialogs, e.g. "GE-Proton10-33"
    QString command; // a name on the PATH, an absolute path, or an alias

    // Start it through an interactive shell, so an alias or shell function is
    // found. Otherwise the program is run directly.
    bool useShell = false;

    // Set for a Steam Proton build, to the id of its directory under
    // compatibilitytools.d. Such a runner needs a wine prefix of its own and
    // the environment pointing at it, so `command` is not what starts it.
    QString compatToolId;
    bool isCompatTool() const { return !compatToolId.isEmpty(); }

    // Detected on the machine rather than configured here, as every wine and
    // Proton build is. Neither saved nor editable, since they appear and
    // disappear with what is installed.
    bool discovered = false;
};

// A family of launchers: the file types it claims, the programs that can open
// them, and the settings shared by the whole family.
struct Launcher {
    QString id;
    QString name;
    QString description;

    // The file types this launcher claims, lower case and without the dot.
    // Several launchers may claim the same type, wine and Proton both taking
    // .exe, in which case an item picks between them.
    QStringList extensions;

    // What follows a runner's command, split as a shell would split it.
    // Hardcoded rather than configurable: how a launcher is handed a file is
    // its own business. The placeholders are:
    //   {file}   the item's own path
    //   {dir}    the folder the item sits in
    //   {core}   the emulator core chosen for the file
    //   {appid}  the Steam app id the file's manifest names
    //
    // A placeholder with no value removes its argument from the line, along
    // with the flag in front of it: "-L {core} {file}" becomes just the file
    // when no core is known, so no second form is needed.
    QString arguments;

    // What this family needs to be configured with, which differs by family.
    bool usesPrefixes = false;   // wine and Proton, which keep a prefix per program
    // Whether its programs can be scaled. A Windows program draws the window
    // size it was written for, which can be very small on a modern screen; an
    // emulator does its own scaling and needs none of this.
    bool usesWindowScale = false;
    // Whether wine's own scaling is available. Wine reads WINE_DPI_SCALE and
    // the prefix DPI; a Proton build scales its own way and must not be given
    // these.
    bool usesDpiScale = false;
    bool usesCores = false;      // an emulator that has to be told the core
    // Whether an item run by it can have arguments of its own. Wine and Proton
    // pass them to the Windows program they start, which is the only place
    // they mean anything; an emulator, DOSBox and Steam are each given a file
    // or an id and have nowhere to put them.
    bool usesArguments = false;
    // Steam finds the game itself, so this launcher is configured with the
    // library locations rather than with a program.
    bool usesSteamLibraries = false;

    // Where the prefixes its runners create are kept, one per program, each
    // holding its C: drive, registry and saved games. Empty means this
    // application's own data directory.
    QString prefixesDir;

    // Named prefixes created by hand, kept in that same folder. An item names
    // one of these when several programs have to share a prefix, such as a
    // game and its editor. The per-item prefix every other item gets is not
    // listed here: it has no name and is not removable from the dialog.
    QStringList prefixes;

    // Emulator cores by file type, as a version that had no core per item
    // wrote them: which core plays a .nes, which a .gba, and the folder they
    // are in. Read and never written now -- an item names its own core, out of
    // what the install has -- and kept only so that an item carrying no core
    // runs the core its file type used to give it.
    QString coresDir;
    QVector<QPair<QString, QString>> cores;

    // Steam library folders added by hand, for libraries Steam does not list
    // itself. The ones it lists are detected each session and not stored here.
    QStringList libraryFolders;

    // The programs that can act as this launcher.
    QVector<LauncherRunner> runners;

    bool handles(const QString &path) const;
    const LauncherRunner *runner(const QString &runnerId) const;
    // The runner an item gets when it names none: the first one.
    const LauncherRunner *defaultRunner() const;
};

// Every launcher as last saved, with missing fields filled in from the
// defaults and the installed wines and Proton builds added. Read once and
// cached for the session.
const QVector<Launcher> &launchers();
// Replaces all of them and writes them to the settings.
void setLaunchers(const QVector<Launcher> &launchers);
// The launchers as they are before any editing.
QVector<Launcher> defaultLaunchers();

const Launcher *launcherById(const QString &id);
// Every launcher claiming this file's extension, in list order, so wine comes
// before Proton for a .exe.
QVector<const Launcher *> launchersForPath(const QString &path);

// The command line behind a runner: the program it names when that can be
// found, else the definition of the alias it is, which carries arguments of
// its own. Resolved once and kept for the session.
QString resolvedRunnerCommand(const LauncherRunner &runner);
// Just the program of it.
QString resolvedRunnerBinary(const LauncherRunner &runner);

// The folder an emulator loads its cores from: the one configured for the
// family, else the `cores` directory beside this runner's own program, since a
// runner is an install. Empty when neither is found, and meaningless for a
// launcher that uses no cores.
QString coresFolderFor(const Launcher &launcher, const LauncherRunner &runner);

// The id an item stores for what runs it, naming both the launcher and the
// runner so that a saved desktop is readable and unambiguous.
QString launcherId(const Launcher &launcher, const LauncherRunner &runner);

// One entry in an item's "Run with" list: the id to store and the label to
// show.
struct LauncherChoice {
    QString id;
    QString label;
    // Whether the runner it names keeps a wine prefix of its own.
    bool usesPrefixes = false;
    // The named prefixes of the launcher behind it, which an item can pick
    // instead of its own.
    QStringList prefixes;
    // Whether an item run by it can be scaled, and whether wine's own scaling
    // is one of the available methods.
    bool usesWindowScale = false;
    bool usesDpiScale = false;
    // Whether the item's own arguments reach the program it starts.
    bool usesArguments = false;
    // Whether it is an emulator that has to be told a core, and the cores of
    // that install which play the file, by display name. Discovered from the
    // install rather than configured; see LibretroCores.h.
    bool usesCores = false;
    QVector<LibretroCore> cores;
};
// Everything that can open this file: every runner of every launcher claiming
// it, so an item can be pointed at one of them.
QVector<LauncherChoice> launchersFor(const QString &path);

// Resolves the launcher and runner an item named by id. When it named none,
// or named one that no longer exists, falls back to the first runner of the
// first launcher claiming the file type. `launcher` is left null when nothing
// claims the file.
void launcherForItem(const QString &path, const QString &launcherId, const Launcher **launcher,
                     const LauncherRunner **runner);

// What it takes to start `path`: a program and its arguments, with the
// placeholders filled in, wrapped in a shell if the runner needs one, and the
// item's own arguments appended.
struct LauncherCommand {
    QString program;
    QStringList args;
};
// `chosenCore` is the core the item named, empty for an item that named none,
// which then gets the one the launcher's table gives its file type.
LauncherCommand launcherCommandFor(const Launcher &launcher, const LauncherRunner &runner,
                                   const QString &path, const QStringList &extraArgs,
                                   const QString &chosenCore = QString());
