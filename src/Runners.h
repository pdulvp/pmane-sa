#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// How one kind of file is started: the program to run, what to hand it, and
// which file types it claims.
//
// These used to be written into the launcher -- a `db` alias for DOSBox, an
// `scm` alias for ScummVM, a `retroarch` one for ROMs -- which meant the
// machine had to be set up exactly the way the author's was. They are
// settings now, edited in Options > Runners and kept with the rest of them.
struct Runner {
    QString id;    // never shown, and what an old setting is found again by
    QString name;  // what the dialog calls it
    // A line of explanation in the dialog, so a runner says what it is for.
    QString description;

    // The program, which may be a name on the PATH, an absolute path, or --
    // with `useShell` -- a shell alias or function.
    QString command;
    // What follows it, split the way a shell would split it. Three
    // placeholders are filled in on the way past:
    //   {file}  the item's own path
    //   {dir}   the folder the item sits in, for a runner pointed at a
    //           directory rather than at a file
    //   {core}  the emulator core chosen for this file, in `coreArguments`
    QString arguments;
    // Used instead of `arguments` when a core has been found for the file.
    // Empty on a runner that does not use cores.
    QString coreArguments;

    // Start it from an interactive shell, so that an alias or a shell
    // function is found. Off means the program is run directly, which is
    // faster and does not depend on a shell profile.
    bool useShell = false;

    // The file types this runner claims, without their dots, lower case.
    QStringList extensions;

    // Where the wine prefixes are built -- one to a program, holding its C:
    // drive, its registry and its saved games. Empty means the application's
    // own data directory.
    QString prefixesDir;

    // Emulator cores, for a runner that needs to be told which one to use.
    // `coresDir` empty means: work it out from where the program lives.
    bool usesCores = false;
    QString coresDir;
    QVector<QPair<QString, QString>> cores; // extension -> core file name

    bool handles(const QString &path) const;
};

// Every runner, as last saved, with anything missing filled in from the
// defaults. Read once and kept for the session.
const QVector<Runner> &runners();

// Replaces the lot and writes them to the settings.
void setRunners(const QVector<Runner> &runners);

// What the runners are without anything having been edited: the aliases and
// the file types this application shipped with.
QVector<Runner> defaultRunners();

const Runner *runnerById(const QString &id);
// The runner that claims this file's extension, or nullptr for a file no
// runner wants.
const Runner *runnerForPath(const QString &path);

// What it takes to start `path` with `runner`: a program and its arguments,
// with the placeholders filled in, the shell wrapped round it if it asked
// for one, and `extraArgs` -- the item's own -- on the end.
struct RunnerCommand {
    QString program;
    QStringList args;
};
RunnerCommand runnerCommandFor(const Runner &runner, const QString &path,
                               const QStringList &extraArgs);
