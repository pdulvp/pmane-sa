#include "DefaultIcons.h"

#include "Launchers.h"
#include "PeIconExtractor.h"

#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QSettings>

namespace {

const char *kGroup = "defaultIcons";

// Read once and kept, as the launchers are: this is asked for every item of
// every group as a desktop opens.
QHash<QString, DefaultIcon> *loaded() {
    static QHash<QString, DefaultIcon> icons;
    return &icons;
}

bool *loadedYet() {
    static bool done = false;
    return &done;
}

// The artwork itself, kept by extension so that a hundred ROMs of one type
// read their icon file once, with what the file turned out to be: whether it
// is the flat, low-colour kind is known only once it has been read.
struct Artwork {
    QIcon icon;
    bool legacy = false;
};

QHash<QString, Artwork> *cache() {
    static QHash<QString, Artwork> icons;
    return &icons;
}

void load() {
    if (*loadedYet()) return;
    *loadedYet() = true;

    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    for (const QString &extension : settings.childGroups()) {
        settings.beginGroup(extension);
        DefaultIcon icon;
        icon.file = settings.value(QStringLiteral("file")).toString();
        icon.index = settings.value(QStringLiteral("index"), -1).toInt();
        icon.legacy = settings.value(QStringLiteral("legacy"), false).toBool();
        settings.endGroup();
        if (icon.isSet()) loaded()->insert(extension.toLower(), icon);
    }
    settings.endGroup();
}

} // namespace

// One of the icons a file holds, or a plain image file: the same two sources
// the properties dialog offers.
QIcon iconFromDefault(const DefaultIcon &icon, bool *legacy) {
    PeIconExtractor::Source source = PeIconExtractor::Source::None;
    const QIcon artwork = PeIconExtractor::extractIconAt(icon.file, icon.index, &source);
    if (!artwork.isNull()) {
        if (legacy) {
            *legacy = source == PeIconExtractor::Source::Ne ||
                      source == PeIconExtractor::Source::Ico;
        }
        return artwork;
    }

    const QImage image(icon.file);
    if (image.isNull()) return {};
    if (legacy) *legacy = icon.legacy;
    return QIcon(QPixmap::fromImage(image));
}

QStringList everyFileType() {
    load();

    QStringList types;
    for (const Launcher &launcher : launchers()) {
        for (const QString &extension : launcher.extensions) {
            const QString type = extension.trimmed().toLower();
            if (!type.isEmpty() && !types.contains(type)) types << type;
        }
    }
    // A type no launcher claims any more, but which someone chose an icon for.
    for (auto it = loaded()->constBegin(); it != loaded()->constEnd(); ++it) {
        if (!types.contains(it.key())) types << it.key();
    }

    types.sort();
    return types;
}

QHash<QString, DefaultIcon> defaultIcons() {
    load();
    return *loaded();
}

void setDefaultIcons(const QHash<QString, DefaultIcon> &icons) {
    load();
    *loaded() = icons;
    cache()->clear();

    QSettings settings;
    // Written whole: a type whose icon was cleared has to leave the file, and
    // removing the group is the only way to be sure of it.
    settings.remove(QLatin1String(kGroup));
    settings.beginGroup(QLatin1String(kGroup));
    for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
        if (!it.value().isSet()) continue;
        settings.beginGroup(it.key().toLower());
        settings.setValue(QStringLiteral("file"), it.value().file);
        settings.setValue(QStringLiteral("index"), it.value().index);
        settings.setValue(QStringLiteral("legacy"), it.value().legacy);
        settings.endGroup();
    }
    settings.endGroup();
}

QIcon defaultIconForPath(const QString &path, bool *legacy) {
    load();
    if (legacy) *legacy = false;

    const QString type = QFileInfo(path).suffix().toLower();
    if (type.isEmpty()) return {};

    const auto cached = cache()->constFind(type);
    if (cached != cache()->constEnd()) {
        if (legacy) *legacy = cached->legacy;
        return cached->icon;
    }

    const auto found = loaded()->constFind(type);
    if (found == loaded()->constEnd()) return {};

    Artwork artwork;
    artwork.icon = iconFromDefault(*found, &artwork.legacy);
    // Kept even when nothing could be read, so a missing file is not looked
    // for again for every item of its type.
    cache()->insert(type, artwork);
    if (legacy) *legacy = artwork.legacy;
    return artwork.icon;
}
