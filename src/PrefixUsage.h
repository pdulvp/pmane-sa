#pragma once

#include <QString>
#include <QVector>

#include "ProgramItem.h"

// What is in a launcher's prefixes folder and whether anything still uses it. A
// prefix is a Windows installation, saved games included, so nothing is
// discarded on a guess: a prefix is only reported unused when no item on any
// screen points at it.
struct PrefixUse {
    // The name an item stores for a named prefix; the folder's own name for one
    // created for a program.
    QString name;
    QString path;

    enum Kind {
        Named,          // created by hand on the launcher, for items to share
        PerApplication, // created for the one program its marker names
        Foreign,        // a folder this application did not create
    };
    Kind kind = Named;

    // The program a per-application prefix was created for, as its marker
    // records. Empty when the marker is missing, as for a prefix created before
    // the markers existed.
    QString program;

    // The programs that run in it: one entry per item on any screen pointing
    // here, as the path that item names. The count says whether the prefix is
    // still in use; the list says what for.
    QStringList programs;
    bool onDisk = false;

    int items() const { return int(programs.size()); }
};

// The current desktop rather than the last saved one: a screen is only saved
// when it is closed, so a newly added item is on screen but not yet in its
// file. Treating it as absent would mark the prefix it just created as unused.
struct OpenScreen {
    QString name;
    QVector<ProgramItem> items;
};

// Everything in `baseDir`, with the items pointing at each one counted: the
// named prefixes first, in the order `names` gives, whether or not they exist
// yet, then whatever else the folder holds.
QVector<PrefixUse> prefixesIn(const QString &baseDir, const QStringList &names,
                              const OpenScreen &open);

// The size of a folder in bytes. It walks the whole tree, so it is called on
// request rather than up front: a prefix is a Windows installation and there
// may be twenty of them.
qint64 folderSize(const QString &path);
