#include "GroupWindow.h"
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
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace {

bool isDosBoxConfig(const QString &path) {
    return path.endsWith(QLatin1String(".conf"), Qt::CaseInsensitive);
}

bool isLaunchable(const QString &path) {
    return path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) || isDosBoxConfig(path);
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

QIcon loadIconFile(const QString &path) {
    if (path.endsWith(QLatin1String(".ico"), Qt::CaseInsensitive)) {
        QIcon icon = PeIconExtractor::extractIcoFile(path);
        if (!icon.isNull()) return icon;
    }
    if (path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
        return PeIconExtractor::extractIcon(path);
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
            it.icon = loadIconFile(resolved);
        }
    } else if (it.icon.isNull()) {
        it.icon = PeIconExtractor::extractIcon(it.command);
    }
    if (it.icon.isNull() && isDosBoxConfig(it.command)) {
        it.icon = QIcon::fromTheme(QStringLiteral("dosbox"));
    }
    if (it.icon.isNull()) {
        it.icon = QApplication::style()->standardIcon(
            isDosBoxConfig(it.command) ? QStyle::SP_DriveHDIcon : QStyle::SP_ComputerIcon);
    }
    if (it.workingDir.isEmpty()) {
        it.workingDir = QFileInfo(it.command).absolutePath();
    }
    m_items.append(it);
    rebuildList();
    emit modified();
}

void GroupWindow::rebuildList() {
    m_list->clear();
    for (int i = 0; i < m_items.size(); ++i) {
        const ProgramItem &it = m_items[i];
        auto *listItem = new QListWidgetItem(it.icon, it.name);
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
