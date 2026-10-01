#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// One program that can act as a launcher: a wine on this machine, a Proton
// build, a DOSBox, the ScummVM sitting outside the PATH. An item picks one of
// these, and nothing about it is a command line: the arguments a launcher
// needs belong to the launcher and are not anyone's to edit.
struct LauncherRunner {
    QString id;      // unique within its launcher, and what an item stores
    QString name;    // what the dialogs call it: "GE-Proton10-33"
    QString command; // a name on the PATH, an absolute path, or an alias

    // Start it from an interactive shell, so an alias or a shell function is
    // found. Off runs the program directly.
    bool useShell = false;

    // Set on a runner that is a Steam Proton build, by the id of its
    // directory under compatibilitytools.d. Such a runner is not simply a
    // program to run: it wants a wine prefix of its own and the environment
    // that points at it, so `command` is not what starts it.
    QString compatToolId;
    bool isCompatTool() const { return !compatToolId.isEmpty(); }

    // Read off the machine rather than set up here -- every wine and every
    // Proton build is -- and so neither saved nor edited: they come and go as
    // things are installed.
    bool discovered = false;
};

// What a kind of file is opened with: which file types it claims, the
// programs that can open them, and whatever the family as a whole knows.
struct Launcher {
    QString id;
    QString name;
    QString description;

    // The file types this launcher claims, without their dots, lower case.
    // More than one launcher may claim the same type -- wine and Proton both
    // take .exe -- and an item then picks between them.
    QStringList extensions;

    // What follows a runner's command, split the way a shell would split it.
    // Hardcoded: how a launcher is handed a file is the launcher's business,
    // not a line for anyone to maintain. The placeholders filled in on the
    // way past are:
    //   {file}   the item's own path
    //   {dir}    the folder the item sits in
    //   {core}   the emulator core chosen for the file
    //
    // A placeholder with nothing to put in it takes its argument out of the
    // line, and the flag in front of it with it: "-L {core} {file}" is just
    // the file when no core is known, and needs no second spelling.
    QString arguments;

    // What this family is asked for, which differs by family.
    bool usesPrefixes = false;   // wine and Proton, which keep a prefix per program
    // Whether its programs can be scaled: a Windows program draws the window
    // it was written for, which on a modern screen may be very small. An
    // emulator does its own scaling and wants none of this.
    bool usesWindowScale = false;
    bool usesCores = false;      // an emulator that has to be told the core

    // Where the prefixes its runners build are kept -- one to a program,
    // holding its C: drive, its registry and its saved games. Empty means
    // this application's own data directory.
    QString prefixesDir;

    // Emulator cores by file type: which core plays a .nes, which a .gba.
    // `coresDir` empty means: work it out from where the program lives.
    QString coresDir;
    QVector<QPair<QString, QString>> cores;

    // The programs that can act as this launcher.
    QVector<LauncherRunner> runners;

    bool handles(const QString &path) const;
    const LauncherRunner *runner(const QString &runnerId) const;
    // The one an item gets when it has not asked for another: the first.
    const LauncherRunner *defaultRunner() const;
};

// Every launcher as last saved, with anything missing filled in from the
// defaults and the installed wines and Proton builds added. Read once and
// kept for the session.
const QVector<Launcher> &launchers();
// Replaces the lot and writes them to the settings.
void setLaunchers(const QVector<Launcher> &launchers);
// What the launchers are before anything is edited.
QVector<Launcher> defaultLaunchers();

const Launcher *launcherById(const QString &id);
// Every launcher that claims this file's extension, in the order they are
// listed -- so wine comes before Proton for a .exe.
QVector<const Launcher *> launchersForPath(const QString &path);

// How an item names what runs it: the launcher and the runner, so that both
// are readable in a saved desktop and neither is ambiguous.
QString launcherId(const Launcher &launcher, const LauncherRunner &runner);

// One entry in the "Run with" list on an item: what to store, and what to
// show.
struct LauncherChoice {
    QString id;
    QString label;
    // Whether what it names keeps a wine prefix of its own.
    bool usesPrefixes = false;
    // Whether an item run by it can be scaled up.
    bool usesWindowScale = false;
};
// Everything that can open this file: every runner of every launcher that
// claims it, so an item can be pointed at one of them.
QVector<LauncherChoice> launchersFor(const QString &path);

// What an item asked for by id, or what its file leads to when it asked for
// nothing (or for something no longer there): the first runner of the first
// launcher claiming the type. `launcher` comes back null when nothing claims
// the file at all.
void launcherForItem(const QString &path, const QString &launcherId, const Launcher **launcher,
                     const LauncherRunner **runner);

// What it takes to start `path`: a program and its arguments, with the
// placeholders filled in, the shell wrapped round it if the runner needs one,
// and the item's own arguments on the end.
struct LauncherCommand {
    QString program;
    QStringList args;
};
LauncherCommand launcherCommandFor(const Launcher &launcher, const LauncherRunner &runner,
                                   const QString &path, const QStringList &extraArgs);
