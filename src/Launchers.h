#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// One way of starting a file with a launcher. The program is the launcher's --
// there is one DOSBox on a machine -- so a profile is what differs about a
// run: the Proton build behind it and the prefixes it keeps, a base
// configuration, a pinned emulator core, an argument or two.
//
// A launcher ships with one of these and a desktop can keep as many more as it
// has ways of playing things.
struct LauncherProfile {
    QString id;   // unique within its launcher, and what an item stores
    QString name; // what the dialogs call it

    // Added to the launcher's own arguments, for a way of running that differs
    // only by what it is told: a window rather than a full screen, a log
    // turned on. The same placeholders apply.
    QString extraArguments;

    // Wine: the Steam compatibility tool this profile runs through, by the
    // id of its directory. Empty means the launcher's own command, which is
    // the system wine.
    QString compatToolId;
    // Wine: where the prefixes this profile builds are kept -- one to a
    // program, holding its C: drive, its registry and its saved games.
    // Empty means the application's own data directory.
    QString prefixesDir;

    // DOSBox: a configuration applied before the item's own, for the
    // settings a machine shares across its games. Reached as {config}.
    QString baseConfig;

    // RetroArch: the core this profile always plays with. Empty leaves it to
    // the launcher's table of cores by file type.
    QString core;
};

// What a kind of file is opened with: which file types it claims, whatever
// the family as a whole knows -- RetroArch's cores, the wine builds
// installed -- and the profiles underneath it.
struct Launcher {
    QString id;
    QString name;
    QString description;

    // The file types this launcher claims, without their dots, lower case.
    QStringList extensions;

    // The program every profile of this launcher starts, which may be a name
    // on the PATH, an absolute path, or -- with `useShell` -- a shell alias
    // or function.
    QString command;
    // What follows it, split the way a shell would split it. The
    // placeholders filled in on the way past are:
    //   {file}   the item's own path
    //   {dir}    the folder the item sits in
    //   {core}   the emulator core chosen for the file
    //   {config} the profile's base configuration file
    //
    // A placeholder with nothing to put in it takes its argument out of the
    // line, and the flag in front of it with it: "-L {core} {file}" is just
    // the file when no core is known, and needs no second spelling.
    QString arguments;
    // Start it from an interactive shell, so an alias or a shell function is
    // found. Off runs the program directly.
    bool useShell = false;

    // What its profiles are asked for, which differs by family.
    bool usesCompatTools = false; // wine and the Proton builds beside it
    bool usesCores = false;       // an emulator that has to be told the core
    bool usesBaseConfig = false;  // a machine-wide configuration file

    // Emulator cores by file type, shared by every profile that has not
    // pinned one of its own. `coresDir` empty means: work it out from where
    // the program lives.
    QString coresDir;
    QVector<QPair<QString, QString>> cores;

    QVector<LauncherProfile> profiles;

    bool handles(const QString &path) const;
    // The profile an item gets when it has not asked for one: the first.
    const LauncherProfile *defaultProfile() const;
    const LauncherProfile *profile(const QString &profileId) const;
};

// Every launcher as last saved, with anything missing filled in from the
// defaults. Read once and kept for the session.
const QVector<Launcher> &launchers();
// Replaces the lot and writes them to the settings.
void setLaunchers(const QVector<Launcher> &launchers);
// What the launchers are before anything is edited.
QVector<Launcher> defaultLaunchers();

const Launcher *launcherById(const QString &id);
// The launcher that claims this file's extension, or nullptr for a file no
// launcher wants.
const Launcher *launcherForPath(const QString &path);

// How an item names the profile it is run with: the launcher and the profile,
// so that the two are readable in a saved desktop and neither is ambiguous.
QString launcherId(const Launcher &launcher, const LauncherProfile &profile);

// One entry in the "Run with" list on an item: what to store, and what to
// show.
struct LauncherChoice {
    QString id;
    QString label;
};
// Every profile of every launcher that claims this file, so an item can be
// pointed at one of them.
QVector<LauncherChoice> launchersFor(const QString &path);

// The launcher and profile an item asked for by id, or the ones its file
// leads to when it asked for nothing (or for something no longer there).
// `launcher` comes back null when nothing claims the file at all.
void launcherForItem(const QString &path, const QString &launcherId, const Launcher **launcher,
                     const LauncherProfile **profile);

// What it takes to start `path`: a program and its arguments, with the
// placeholders filled in, the shell wrapped round it if the profile asked
// for one, and the item's own arguments on the end.
struct LauncherCommand {
    QString program;
    QStringList args;
};
LauncherCommand launcherCommandFor(const Launcher &launcher, const LauncherProfile &profile,
                               const QString &path, const QStringList &extraArgs);
