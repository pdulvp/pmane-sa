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

// Where one executable's Proton prefix lives. A prefix per executable, so
// two games cannot tread on each other's bottle.
QString protonPrefixFor(const QString &exePath);
