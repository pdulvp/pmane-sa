#pragma once

#include <QString>
#include <QVector>

// A Steam "compatibility tool" -- a Proton build dropped into Steam's
// compatibilitytools.d -- that can run a Windows executable in place of
// the system wine.
struct CompatTool {
    QString id;          // the install directory's name, stored with the item
    QString displayName; // what the tool's own manifest calls it
    QString protonPath;  // the proton script to invoke
    QString steamPath;   // the Steam install it was found under
};

// Every tool installed under a Steam compatibilitytools.d, ordered by
// display name. Scanned once per session.
const QVector<CompatTool> &installedCompatTools();

// The tool with this id, or nullptr if it is not installed (any more).
const CompatTool *compatToolById(const QString &id);

// The folder a launcher keeps its prefixes in: the one it names, or this
// application's own data directory when it names none.
QString prefixesFolder(const QString &baseDir);

// Where one executable's Proton prefix lives, under `baseDir` -- the folder
// the launcher profile keeps its prefixes in, or this application's own data
// directory when that is empty. A prefix per executable, so two games cannot
// tread on each other's bottle.
QString protonPrefixFor(const QString &exePath, const QString &baseDir = QString());

// The prefix one item actually runs in: the one it names, kept under the same
// folder beside the per-application ones, or its own per-application prefix
// when it names none. A named prefix is shared on purpose -- a game and its
// editor, a program and the runtime installed with it -- so it carries none
// of the per-program marker the automatic ones do.
QString itemPrefixFor(const QString &exePath, const QString &baseDir,
                      const QString &prefixName);

// The same path, worked out and not built: itemPrefixFor() creates the folder
// for a program that has none yet, which is right at launch and wrong when
// all that is wanted is to know where an item would run -- counting up what
// is still in use must not conjure the prefixes it is counting.
QString itemPrefixPath(const QString &exePath, const QString &baseDir,
                       const QString &prefixName);

// What a named prefix is called on disk: the name as typed, with whatever
// cannot be a folder name taken out of it. Empty when nothing is left.
QString prefixFolderName(const QString &prefixName);

// One wine installed on the machine: the system wine on the PATH, or a build
// dropped under /opt or kept by Lutris. Wine runs a Windows program directly,
// where a Proton build wraps it in Steam's runtime, so the two are listed
// apart.
struct WineBuild {
    QString id;          // stable across sessions, and stored with the item
    QString displayName; // "Wine (system)", "wine-9.0-staging"
    QString command;     // the wine binary to run
};

// Every wine found on this machine, the system one first. Scanned once per
// session, like the Proton builds beside it.
const QVector<WineBuild> &installedWineBuilds();
