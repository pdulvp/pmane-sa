#include "GroupWindow.h"
#include "IconListWidget.h"
#include "ItemPropertiesDialog.h"
#include "PeIconExtractor.h"

#include <QApplication>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

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
    if (it.icon.isNull()) {
        it.icon = PeIconExtractor::extractIcon(it.command);
    }
    if (it.icon.isNull()) {
        it.icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
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
    QStringList args = QProcess::splitCommand(item.arguments);
    args.prepend(item.command);
    QString workDir = item.workingDir.isEmpty() ? QFileInfo(item.command).absolutePath()
                                                 : item.workingDir;
    if (!QProcess::startDetached("wine", args, workDir)) {
        QMessageBox::warning(this, tr("Run"),
                              tr("Could not launch '%1' with wine.").arg(item.command));
    }
}

void GroupWindow::onFilesDropped(const QStringList &paths) {
    for (const QString &path : paths) {
        if (!path.endsWith(".exe", Qt::CaseInsensitive)) continue;
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
