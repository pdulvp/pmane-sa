#pragma once

#include <QString>
#include <QVector>

// A Steam "compatibility tool": a Proton build installed in Steam's
// compatibilitytools.d, which can run a Windows executable in place of the
// system wine.
struct CompatTool {
    QString id;          // the install directory's name, stored with the item
    QString displayName; // what the tool's own manifest calls it
    QString protonPath;  // the proton script to invoke
    QString steamPath;   // the Steam install it was found under
};

// Every tool installed under a Steam compatibilitytools.d, ordered by display
// name. Scanned once per session.
const QVector<CompatTool> &installedCompatTools();

// The tool with this id, or nullptr when it is no longer installed.
const CompatTool *compatToolById(const QString &id);

// The folder a launcher keeps its prefixes in: the one it names, or this
// application's data directory when it names none.
QString prefixesFolder(const QString &baseDir);

// Where one executable's Proton prefix lives, under `baseDir`: the folder the
// launcher profile keeps its prefixes in, or this application's data directory
// when that is empty. One prefix per executable, so two games cannot interfere
// with each other.
QString protonPrefixFor(const QString &exePath, const QString &baseDir = QString());

// The contents of the file marking a prefix as created by this application: the
// program it was created for and, once an item has claimed it, which item.
// Either may be empty, a prefix created before the markers recorded an item
// holding only the program.
struct PrefixOwner {
    QString program;
    QString itemId;
};
PrefixOwner prefixOwner(const QString &prefixPath);

// The prefix an item actually runs in: the one it names, kept in the same
// folder as the per-application ones, or its own per-application prefix when it
// names none. A named prefix is shared deliberately, by a game and its editor
// or a program and the runtime installed with it, so it carries none of the
// per-program marker the automatic ones do.
//
// `itemId` is what distinguishes two icons for the same executable: each gets a
// prefix of its own, so one can be the demo and the other the full game. Empty
// asks for the prefix the executable alone would have had, which is what a
// desktop written before items had ids needs.
QString itemPrefixFor(const QString &exePath, const QString &baseDir,
                      const QString &prefixName, const QString &itemId = QString());

// The same path, computed but not created: itemPrefixFor() creates the folder
// for a program that has none yet, which is right at launch but wrong when only
// the location is wanted, since counting prefixes in use must not create them.
QString itemPrefixPath(const QString &exePath, const QString &baseDir,
                       const QString &prefixName, const QString &itemId = QString());

// The folder name of a named prefix: the name as typed, with anything invalid
// in a folder name removed. Empty when nothing is left.
QString prefixFolderName(const QString &prefixName);

// One wine installed on the machine: the system wine on the PATH, or a build
// under /opt or kept by Lutris. Wine runs a Windows program directly, whereas a
// Proton build wraps it in Steam's runtime, so the two are listed separately.
struct WineBuild {
    QString id;          // stable across sessions, and stored with the item
    QString displayName; // "Wine (system)", "wine-9.0-staging"
    QString command;     // the wine binary to run
};

// Every wine found on this machine, the system one first. Scanned once per
// session, like the Proton builds.
const QVector<WineBuild> &installedWineBuilds();
