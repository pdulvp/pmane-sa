#include "ItemDetails.h"

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

// The icon the desktop's theme gives a file of this kind -- for documents,
// which carry no artwork of their own to extract.
QIcon mimeIcon(const QString &path) {
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    for (const QString &name : {mime.iconName(), mime.genericIconName()}) {
        if (name.isEmpty()) continue;
        QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull()) return icon;
    }
    return {};
}

QIcon loadIconFile(const QString &path, bool *legacy) {
    if (legacy) *legacy = false;

    if (path.endsWith(QLatin1String(".ico"), Qt::CaseInsensitive)) {
        QIcon icon = PeIconExtractor::extractIcoFile(path);
        if (!icon.isNull()) {
            if (legacy) *legacy = true;
            return icon;
        }
    }
    if (path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
        PeIconExtractor::Source source = PeIconExtractor::Source::None;
        QIcon icon = PeIconExtractor::extractIcon(path, &source);
        if (legacy) *legacy = source == PeIconExtractor::Source::Ne;
        return icon;
    }
    QImage image(path);
    return image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image));
}

// Whether a name is one the user chose, as opposed to the file name we
// would have defaulted to.
bool nameIsOurs(const ProgramItem &item) {
    return item.name.isEmpty() || item.name == QFileInfo(item.command).completeBaseName();
}

} // namespace

void completeProgramItem(ProgramItem &it) {
    if (isDosBoxConfig(it.command)) {
        const DosBoxUi ui = readDosBoxUi(it.command);
        // A [ui] title names the entry, but never overrides a name the user
        // typed in the properties dialog.
        if (!ui.title.isEmpty() && nameIsOurs(it)) it.name = ui.title;
        if (it.icon.isNull() && !ui.iconPath.isEmpty()) {
            const QString resolved = QFileInfo(it.command).absoluteDir().filePath(ui.iconPath);
            it.icon = loadIconFile(resolved, &it.legacyIcon);
        }
    } else if (isScummVmGame(it.command)) {
        // Prefer the title the .trs names itself, falling back to the folder
        // holding it ("The Dig", not "DIG"). Never override a name the user
        // typed in the properties dialog.
        if (nameIsOurs(it)) {
            const QString banner = readTrsBannerTitle(it.command);
            it.name = banner.isEmpty() ? QFileInfo(it.command).absoluteDir().dirName() : banner;
        }
    } else if (isDocument(it.command)) {
        // Nothing to extract from a document, and nothing legacy to restyle:
        // it wears the icon the desktop gives its file type.
        if (it.icon.isNull()) it.icon = mimeIcon(it.command);
    } else if (it.icon.isNull()) {
        PeIconExtractor::Source source = PeIconExtractor::Source::None;
        it.icon = PeIconExtractor::extractIcon(it.command, &source);
        it.legacyIcon = source == PeIconExtractor::Source::Ne;
    }

    // Whatever the item is, the launcher that plays it may have an icon of
    // its own in the desktop's theme.
    if (it.icon.isNull() && isDosBoxConfig(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("dosbox"));
    }
    if (it.icon.isNull() && isScummVmGame(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("scummvm"));
    }
    if (it.icon.isNull() && isRetroArchRom(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("retroarch"));
    }
    if (it.icon.isNull()) {
        QStyle::StandardPixmap fallback = QStyle::SP_ComputerIcon;
        if (isDosBoxConfig(it.command)) fallback = QStyle::SP_DriveHDIcon;
        if (isScummVmGame(it.command)) fallback = QStyle::SP_DirIcon;
        if (isRetroArchRom(it.command)) fallback = QStyle::SP_DriveFDIcon;
        if (isDocument(it.command)) fallback = QStyle::SP_FileIcon;
        it.icon = QApplication::style()->standardIcon(fallback);
    }

    if (it.workingDir.isEmpty()) it.workingDir = QFileInfo(it.command).absolutePath();
}
