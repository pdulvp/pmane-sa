#include "GroupWindow.h"
#include "IconEffect.h"
#include "IconListWidget.h"
#include "ItemPropertiesDialog.h"
#include "PeIconExtractor.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace {

bool isDosBoxConfig(const QString &path) {
    return path.endsWith(QLatin1String(".conf"), Qt::CaseInsensitive);
}

// A ScummVM game, identified by one of its data files. ScummVM is pointed at
// the folder that holds it, not the file itself.
bool isScummVmGame(const QString &path) {
    return path.endsWith(QLatin1String(".trs"), Qt::CaseInsensitive);
}

// Console ROMs and disc images RetroArch plays.
bool isRetroArchRom(const QString &path) {
    static const QStringList extensions = {
        QStringLiteral(".nes"), QStringLiteral(".z64"), QStringLiteral(".n64"),
        QStringLiteral(".sfc"), QStringLiteral(".gb"),  QStringLiteral(".gba"),
        QStringLiteral(".gcm"), QStringLiteral(".iso"),
    };
    for (const QString &extension : extensions) {
        if (path.endsWith(extension, Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool isLaunchable(const QString &path) {
    return path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) || isDosBoxConfig(path) ||
           isScummVmGame(path) || isRetroArchRom(path);
}

// RetroArch does not pick a core from the content on its own, and ignores a
// bare core name, so the full path to one has to be passed. Rather than
// hard-coding where the cores live, read it back out of the retroarch alias
// and cache it for the session.
QString retroArchCoresDir() {
    static const QString dir = []() -> QString {
        QProcess probe;
        probe.start(QStringLiteral("bash"), {QStringLiteral("-ic"), QStringLiteral("alias retroarch")});
        if (!probe.waitForFinished(3000)) return {};

        const QString definition = QString::fromLocal8Bit(probe.readAllStandardOutput());
        static const QRegularExpression binary(QStringLiteral("(/[^\\s'\"]+)/retroarch\\b"));
        const QRegularExpressionMatch match = binary.match(definition);
        if (!match.hasMatch()) return {};

        const QString cores = match.captured(1) + QStringLiteral("/cores");
        return QDir(cores).exists() ? cores : QString();
    }();
    return dir;
}

// The libretro core for a ROM, by extension. Core file names are standard
// across libretro installs; only the directory is site-specific.
QString retroArchCoreFor(const QString &romPath) {
    static const QHash<QString, QString> coreByExtension = {
        {QStringLiteral("nes"), QStringLiteral("mesen_libretro.so")},
        {QStringLiteral("gba"), QStringLiteral("vba_next_libretro.so")},
        {QStringLiteral("sfc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("smc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("n64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("z64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("v64"), QStringLiteral("mupen64plus_next_libretro.so")},
    };

    const QString core = coreByExtension.value(QFileInfo(romPath).suffix().toLower());
    if (core.isEmpty()) return {};

    const QString dir = retroArchCoresDir();
    if (dir.isEmpty()) return {};

    const QString path = QDir(dir).filePath(core);
    return QFile::exists(path) ? path : QString();
}

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

} // namespace

GroupWindow::GroupWindow(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title) {
    m_list = new IconListWidget(this);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemActivated, this, &GroupWindow::onItemActivated);
    connect(m_list, &QListWidget::itemPressed, this, &GroupWindow::itemSelected);
    connect(m_list, &IconListWidget::filesDropped, this, &GroupWindow::onFilesDropped);
    connect(m_list, &IconListWidget::programItemsDropped, this,
            &GroupWindow::onProgramItemsDropped);
    connect(m_list, &IconListWidget::programItemsMovedOut, this,
            &GroupWindow::onProgramItemsMovedOut);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            &GroupWindow::onContextMenuRequested);
}

void GroupWindow::setTitle(const QString &title) {
    m_title = title;
    emit titleChanged(title);
}

void GroupWindow::addProgramItem(const ProgramItem &item) {
    ProgramItem it = item;
    if (isDosBoxConfig(it.command)) {
        const DosBoxUi ui = readDosBoxUi(it.command);
        // A [ui] title names the entry, but never overrides a name the user
        // typed in the properties dialog.
        if (!ui.title.isEmpty() &&
            (it.name.isEmpty() || it.name == QFileInfo(it.command).completeBaseName())) {
            it.name = ui.title;
        }
        if (it.icon.isNull() && !ui.iconPath.isEmpty()) {
            const QString resolved =
                QFileInfo(it.command).absoluteDir().filePath(ui.iconPath);
            it.icon = loadIconFile(resolved, &it.legacyIcon);
        }
    } else if (isScummVmGame(it.command)) {
        // Prefer the title the .trs names itself, falling back to the folder
        // holding it ("The Dig", not "DIG"). Never override a name the user
        // typed in the properties dialog.
        if (it.name.isEmpty() || it.name == QFileInfo(it.command).completeBaseName()) {
            const QString banner = readTrsBannerTitle(it.command);
            it.name = banner.isEmpty() ? QFileInfo(it.command).absoluteDir().dirName() : banner;
        }
    } else if (it.icon.isNull()) {
        PeIconExtractor::Source source = PeIconExtractor::Source::None;
        it.icon = PeIconExtractor::extractIcon(it.command, &source);
        it.legacyIcon = source == PeIconExtractor::Source::Ne;
    }
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
        it.icon = QApplication::style()->standardIcon(fallback);
    }
    if (it.workingDir.isEmpty()) {
        it.workingDir = QFileInfo(it.command).absolutePath();
    }
    m_items.append(it);
    rebuildList();
    emit modified();
}

void GroupWindow::clearSelection() {
    m_list->clearSelection();
    // Also drop the current item, or the style still marks it with a focus
    // rectangle.
    m_list->setCurrentItem(nullptr);
}

void GroupWindow::setXpStyleIcons(bool enabled) {
    if (m_xpStyleIcons == enabled) return;
    m_xpStyleIcons = enabled;
    rebuildList();
}

void GroupWindow::rebuildList() {
    m_list->clear();
    for (int i = 0; i < m_items.size(); ++i) {
        const ProgramItem &it = m_items[i];
        const QIcon shown =
            (m_xpStyleIcons && it.legacyIcon) ? xpStyleIcon(it.icon) : it.icon;
        auto *listItem = new QListWidgetItem(shown, it.name);
        listItem->setData(IconListWidget::IndexRole, i);
        listItem->setData(IconListWidget::NameRole, it.name);
        listItem->setData(IconListWidget::CommandRole, it.command);
        listItem->setData(IconListWidget::ArgumentsRole, it.arguments);
        listItem->setData(IconListWidget::WorkingDirRole, it.workingDir);
        m_list->addItem(listItem);
    }
    m_list->updateGridSize();
}

int GroupWindow::indexOfListItem(QListWidgetItem *listItem) const {
    if (!listItem) return -1;
    return listItem->data(Qt::UserRole).toInt();
}

void GroupWindow::onItemActivated(QListWidgetItem *item) {
    int idx = indexOfListItem(item);
    if (idx >= 0 && idx < m_items.size()) launch(m_items[idx]);
}

void GroupWindow::launch(const ProgramItem &item) {
    const QStringList extraArgs = QProcess::splitCommand(item.arguments);
    const QString workDir = item.workingDir.isEmpty() ? QFileInfo(item.command).absolutePath()
                                                       : item.workingDir;

    QString program;
    QStringList args;
    QString failureHint;

    if (isDosBoxConfig(item.command)) {
        // `db` is a shell alias, so it exists only in an interactive bash.
        // The path goes through as a positional argument so names with
        // spaces survive and nothing is re-parsed by the shell.
        program = QStringLiteral("bash");
        args = QStringList{QStringLiteral("-ic"), QStringLiteral("db -conf \"$@\""),
                           QStringLiteral("_"), item.command};
        args += extraArgs;
        failureHint = tr("Could not launch '%1' with dosbox (the 'db' alias).").arg(item.command);
    } else if (isScummVmGame(item.command)) {
        // `scm` is a shell alias too, and it wants the folder holding the game
        // data rather than the file itself.
        program = QStringLiteral("bash");
        args = QStringList{QStringLiteral("-ic"), QStringLiteral("scm -p \"$1\" \"${@:2}\""),
                           QStringLiteral("_"), QFileInfo(item.command).absolutePath()};
        args += extraArgs;
        failureHint =
            tr("Could not launch '%1' with ScummVM (the 'scm' alias).").arg(item.command);
    } else if (isRetroArchRom(item.command)) {
        // Another shell alias, taking the ROM itself. The alias pins one
        // core, so pass the right one for this ROM when we can identify it;
        // a later -L overrides the alias's own --libretro.
        program = QStringLiteral("bash");
        const QString core = retroArchCoreFor(item.command);
        if (core.isEmpty()) {
            args = QStringList{QStringLiteral("-ic"), QStringLiteral("retroarch \"$@\""),
                               QStringLiteral("_"), item.command};
        } else {
            args = QStringList{QStringLiteral("-ic"), QStringLiteral("retroarch -L \"$1\" \"$2\""),
                               QStringLiteral("_"), core, item.command};
        }
        args += extraArgs;
        failureHint =
            tr("Could not launch '%1' with RetroArch (the 'retroarch' alias).").arg(item.command);
    } else {
        program = QStringLiteral("wine");
        args = QStringList{item.command} + extraArgs;
        failureHint = tr("Could not launch '%1' with wine.").arg(item.command);
    }

    if (!QProcess::startDetached(program, args, workDir)) {
        QMessageBox::warning(this, tr("Run"), failureHint);
    }
}

void GroupWindow::onFilesDropped(const QStringList &paths) {
    for (const QString &path : paths) {
        if (!isLaunchable(path)) continue;
        ProgramItem item;
        item.name = QFileInfo(path).completeBaseName();
        item.command = path;
        item.workingDir = QFileInfo(path).absolutePath();
        addProgramItem(item);
    }
}

void GroupWindow::onProgramItemsDropped(const QVector<ProgramItem> &items) {
    for (const ProgramItem &item : items) addProgramItem(item);
}

void GroupWindow::onProgramItemsMovedOut(const QVector<int> &indices) {
    QVector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int index : sorted) {
        if (index >= 0 && index < m_items.size()) m_items.remove(index);
    }
    rebuildList();
    emit modified();
}

void GroupWindow::showPropertiesDialog(int index) {
    ItemPropertiesDialog dlg(this);
    if (index >= 0) dlg.setItem(m_items[index]);

    if (dlg.exec() != QDialog::Accepted) return;
    ProgramItem it = dlg.item();
    if (it.command.isEmpty()) return;

    if (index >= 0) {
        it.icon = PeIconExtractor::extractIcon(it.command);
        if (it.icon.isNull()) it.icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
        m_items[index] = it;
        rebuildList();
        emit modified();
    } else {
        addProgramItem(it);
    }
}

void GroupWindow::deleteItem(int index) {
    if (index < 0 || index >= m_items.size()) return;
    auto reply = QMessageBox::question(this, tr("Delete"),
                                        tr("Delete '%1'?").arg(m_items[index].name));
    if (reply != QMessageBox::Yes) return;
    m_items.remove(index);
    rebuildList();
    emit modified();
}

void GroupWindow::onContextMenuRequested(const QPoint &pos) {
    QListWidgetItem *listItem = m_list->itemAt(pos);
    QMenu menu(this);

    if (listItem) {
        int idx = indexOfListItem(listItem);
        QAction *runAction = menu.addAction(tr("Run"));
        QAction *propsAction = menu.addAction(tr("Properties..."));
        menu.addSeparator();
        QAction *deleteAction = menu.addAction(tr("Delete"));

        QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
        if (chosen == runAction) launch(m_items[idx]);
        else if (chosen == propsAction) showPropertiesDialog(idx);
        else if (chosen == deleteAction) deleteItem(idx);
    } else {
        QAction *newAction = menu.addAction(tr("New Program Item..."));
        QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
        if (chosen == newAction) showPropertiesDialog(-1);
    }
}
