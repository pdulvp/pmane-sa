#pragma once

#include <QString>
#include <QVector>

#include "ProgramItem.h"

// What is in a launcher's prefixes folder and whether anything still wants
// it. A prefix is a Windows install -- saved games and all -- so nothing is
// thrown away on a guess: a prefix is only called unused when no item on any
// screen leads to it.
struct PrefixUse {
    // The name an item stores for a named prefix; the folder's own name for
    // one built for a program.
    QString name;
    QString path;

    enum Kind {
        Named,          // set up by hand on the launcher, for items to share
        PerApplication, // built for the one program its marker names
        Foreign,        // a folder in there that this application did not make
    };
    Kind kind = Named;

    // What a per-application prefix was built for, as its marker records it.
    // Empty when the marker is missing, which is how a prefix made before the
    // markers existed reads.
    QString program;

    // How many saved items run in it, counted across every screen.
    int items = 0;
    bool onDisk = false;
};

// The desktop as it stands rather than as it was last written: a screen is
// saved when it is closed, so an item added a moment ago is on screen and not
// yet in its file. Counting it as absent would mark the prefix it just had
// built as unused.
struct OpenScreen {
    QString name;
    QVector<ProgramItem> items;
};

// Everything in `baseDir`, with the items pointing at it counted: the named
// prefixes first, in the order `names` gives them -- whether or not they have
// been built yet -- then whatever else the folder holds.
QVector<PrefixUse> prefixesIn(const QString &baseDir, const QStringList &names,
                              const OpenScreen &open);

// What a folder holds, in bytes. Walks the whole tree, so it is asked for
// rather than worked out up front: a prefix is a Windows installation and
// there may be twenty of them.
qint64 folderSize(const QString &path);
