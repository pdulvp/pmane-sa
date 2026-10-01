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

// Where one executable's Proton prefix lives, under `baseDir` -- the folder
// the launcher profile keeps its prefixes in, or this application's own data
// directory when that is empty. A prefix per executable, so two games cannot
// tread on each other's bottle.
QString protonPrefixFor(const QString &exePath, const QString &baseDir = QString());

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
