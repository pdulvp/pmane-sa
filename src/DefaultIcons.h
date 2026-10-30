#pragma once

#include <QIcon>
#include <QHash>
#include <QString>
#include <QStringList>

// The icon a file type gets when the file itself carries none. A ROM, a disc
// image or a DOSBox config holds no artwork, so without this every one of them
// ends up under the same generic icon; here one icon is chosen per file type
// and every item of that type takes it.
//
// An icon picked for an item, and any artwork the file itself carries, both
// win over this; see completeProgramItem() in ItemDetails.h for the order.
struct DefaultIcon {
    // The file the artwork comes from, as the "Change Icon" listing names it.
    // Empty means no default is set for the type.
    QString file;
    // Which of the icons that file holds, -1 for its first.
    int index = -1;
    // .ico and 16-bit artwork, which the XP style suits.
    bool legacy = false;

    bool isSet() const { return !file.isEmpty(); }
};

// The file types to offer: every extension the launchers claim, plus any that
// already has an icon set, so a type removed from a launcher does not take its
// setting with it. Lower case, without the dot, in alphabetical order.
QStringList everyFileType();

// The icons as set, by extension.
QHash<QString, DefaultIcon> defaultIcons();
// Replaces all of them and writes them to the settings.
void setDefaultIcons(const QHash<QString, DefaultIcon> &icons);

// The artwork one setting names, read from its file. For the dialog, which has
// to show an icon that has not been saved yet.
QIcon iconFromDefault(const DefaultIcon &icon, bool *legacy = nullptr);

// The default for one file, loaded from its file and kept for the session.
// Null when its type has no default, or when the file the default names is
// gone.
QIcon defaultIconForPath(const QString &path, bool *legacy = nullptr);
