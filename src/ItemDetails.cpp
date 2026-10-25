#include "ItemDetails.h"

#include "SteamLibrary.h"

#include "DesktopEntry.h"
#include "Ids.h"
#include "ItemKind.h"
#include "PeIconExtractor.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QStyle>
#include <QTextStream>

namespace {

// A .trs installer script may name its game, as a resource definition
// followed by the string on the next line:
//
//   #define TRS_BANNER_TITLE            225
//   //^f02Full Throttle
//
// The leading ^fNN is a font code, not part of the title. These are DOS-era
// files: CRLF line endings and 8-bit Latin-1 text.
QString readTrsBannerTitle(const QString &trsPath) {
    QFile file(trsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);

    bool titleIsNext = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (titleIsNext) {
            if (!line.startsWith(QLatin1String("//"))) return {};
            QString value = line.mid(2).trimmed();
            static const QRegularExpression fontCode(QStringLiteral("^\\^[A-Za-z]\\d*"));
            value.remove(fontCode);
            return value.trimmed();
        }
        if (line.startsWith(QLatin1String("#define")) &&
            line.contains(QLatin1String("TRS_BANNER_TITLE"))) {
            titleIsNext = true;
        }
    }
    return {};
}

// The optional [ui] section a dosbox conf may carry, naming the entry and
// pointing at an icon relative to the conf itself.
struct DosBoxUi {
    QString title;
    QString iconPath;
};

DosBoxUi readDosBoxUi(const QString &confPath) {
    DosBoxUi ui;
    QFile file(confPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return ui;

    QTextStream in(&file);
    bool inUiSection = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        if (line.startsWith(QLatin1Char('['))) {
            inUiSection = line.compare(QLatin1String("[ui]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inUiSection) continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed().toLower();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("title")) {
            ui.title = value;
        } else if (key == QLatin1String("icon")) {
            ui.iconPath = value;
        }
    }
    return ui;
}

// The icon the desktop theme gives a file of this kind, used for documents,
// which carry no artwork of their own.
QIcon mimeIcon(const QString &path) {
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    for (const QString &name : {mime.iconName(), mime.genericIconName()}) {
        if (name.isEmpty()) continue;
        QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull()) return icon;
    }
    return {};
}

// One of the icons a file holds: the one at `index`, or the first for -1.
QIcon loadIconFile(const QString &path, int index, bool *legacy) {
    if (legacy) *legacy = false;

    PeIconExtractor::Source source = PeIconExtractor::Source::None;
    QIcon icon = PeIconExtractor::extractIconAt(path, index, &source);
    if (!icon.isNull()) {
        if (legacy) {
            *legacy = source == PeIconExtractor::Source::Ne ||
                      source == PeIconExtractor::Source::Ico;
        }
        return icon;
    }

    // Neither an executable nor an .ico: a plain image file, which a dosbox
    // [ui] icon may name.
    QImage image(path);
    return image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image));
}

// The icon a .desktop file names: an icon theme name most of the time, but
// the specification allows an absolute path to an image as well.
QIcon desktopEntryIcon(const QString &icon, bool *legacy) {
    if (legacy) *legacy = false;
    if (icon.isEmpty()) return {};
    if (icon.contains(QLatin1Char('/'))) return loadIconFile(icon, -1, legacy);
    return QIcon::fromTheme(icon);
}

// Whether a name was derived here, and so may be improved on, rather than
// chosen by the user. The customName flag is trusted when set; otherwise, as
// for a file dropped on a group or an item saved before the flag existed, the
// test is whether the name matches the default derived from the file name.
bool nameIsOurs(const ProgramItem &item) {
    if (item.customName) return false;
    return item.name.isEmpty() || item.name == QFileInfo(item.command).completeBaseName();
}

} // namespace

void completeProgramItem(ProgramItem &it) {
    // Every item on the desktop passes through here once, so this is where an
    // item gets its id. An item from a desktop saved before ids existed is
    // given one now and keeps it from the next save on.
    if (it.id.isEmpty()) it.id = newId();

    // An icon picked in the properties dialog takes precedence over anything
    // found here. If its file is gone, the item falls back to the discovery
    // below rather than to no icon at all.
    if (it.icon.isNull() && it.iconIndex >= 0) {
        it.icon = loadIconFile(iconSourceFile(it), it.iconIndex, &it.legacyIcon);
    }

    if (isDosBoxConfig(it.command)) {
        const DosBoxUi ui = readDosBoxUi(it.command);
        // A [ui] title names the entry, but never overrides a name the user
        // typed in the properties dialog.
        if (!ui.title.isEmpty() && nameIsOurs(it)) it.name = ui.title;
        if (it.icon.isNull() && !ui.iconPath.isEmpty()) {
            const QString resolved = QFileInfo(it.command).absoluteDir().filePath(ui.iconPath);
            it.icon = loadIconFile(resolved, -1, &it.legacyIcon);
            // Stored so the properties dialog can offer the other icons in that
            // file, the conf naming only the file.
            if (!it.icon.isNull()) it.iconSource = resolved;
        }
    } else if (isSteamManifest(it.command)) {
        // The manifest names the game and Steam has cached its icon; the file
        // name ("appmanifest_19680.acf") is useless as a label.
        const SteamGame game = readSteamManifest(it.command);
        if (!game.name.isEmpty() && nameIsOurs(it)) it.name = game.name;
        if (it.icon.isNull() && !game.iconPath.isEmpty()) {
            it.icon = QIcon(game.iconPath);
            // Stored so the properties dialog opens its icon listing on the
            // artwork Steam cached rather than on the manifest.
            if (!it.icon.isNull()) it.iconSource = game.iconPath;
        }
    } else if (isScummVmGame(it.command)) {
        // Prefer the title the .trs declares, falling back to the name of the
        // folder holding it ("The Dig", not "DIG"). Never overrides a name the
        // user typed in the properties dialog.
        if (nameIsOurs(it)) {
            const QString banner = readTrsBannerTitle(it.command);
            it.name = banner.isEmpty() ? QFileInfo(it.command).absoluteDir().dirName() : banner;
        }
    } else if (isDesktopEntry(it.command)) {
        const DesktopEntry entry = readDesktopEntry(it.command);
        // The launcher names the application and points at its artwork; the
        // file name ("org.kde.okular") is useless as a label.
        if (!entry.name.isEmpty() && nameIsOurs(it)) it.name = entry.name;
        if (it.icon.isNull()) it.icon = desktopEntryIcon(entry.icon, &it.legacyIcon);
    } else if (isFolder(it.command)) {
        // The folder's own name: the default derivation would give "photos" for
        // a folder called "photos.2024".
        if (nameIsOurs(it)) it.name = QDir(it.command).dirName();
        if (it.icon.isNull()) it.icon = QIcon::fromTheme(QStringLiteral("folder"));
    } else if (isDocument(it.command)) {
        // A document holds no artwork to extract, so it takes the icon the
        // desktop gives its file type.
        if (it.icon.isNull()) it.icon = mimeIcon(it.command);
    } else if (it.icon.isNull()) {
        it.icon = loadIconFile(it.command, -1, &it.legacyIcon);
        if (!it.icon.isNull()) it.iconSource = it.command;
    }

    // Failing that, the launcher that runs it may have an icon in the desktop
    // theme.
    if (it.icon.isNull() && isDosBoxConfig(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("dosbox"));
    }
    if (it.icon.isNull() && isScummVmGame(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("scummvm"));
    }
    if (it.icon.isNull() && isSteamManifest(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("steam"));
    }
    if (it.icon.isNull() && isRetroArchRom(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("retroarch"));
    }
    if (it.icon.isNull() && isDesktopEntry(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }
    if (it.icon.isNull()) {
        QStyle::StandardPixmap fallback = QStyle::SP_ComputerIcon;
        if (isDosBoxConfig(it.command)) fallback = QStyle::SP_DriveHDIcon;
        if (isScummVmGame(it.command)) fallback = QStyle::SP_DirIcon;
        if (isRetroArchRom(it.command)) fallback = QStyle::SP_DriveFDIcon;
        if (isDocument(it.command)) fallback = QStyle::SP_FileIcon;
        if (isDesktopEntry(it.command)) fallback = QStyle::SP_FileIcon;
        if (isFolder(it.command)) fallback = QStyle::SP_DirIcon;
        it.icon = QApplication::style()->standardIcon(fallback);
    }
}

QString workingDirFor(const ProgramItem &item) {
    // A .desktop file lives in /usr/share/applications, which is no place to
    // start an application from: use its Path line, or the home directory when
    // it has none.
    if (isDesktopEntry(item.command)) {
        const QString path = readDesktopEntry(item.command).path;
        return path.isEmpty() ? QDir::homePath() : path;
    }
    // Steam starts the game from wherever it installed it; the manifest's own
    // directory is irrelevant.
    if (isSteamManifest(item.command)) return {};
    if (isFolder(item.command)) return item.command;
    // Everything else runs from the folder it sits in, which is what a Windows
    // program expects of its own install.
    return QFileInfo(item.command).absolutePath();
}

QString iconSourceFile(const ProgramItem &item) {
    return item.iconSource.isEmpty() ? item.command : item.iconSource;
}
