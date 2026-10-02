#include "GroupWindow.h"
#include "IconEffect.h"
#include "IconListWidget.h"
#include "ItemDetails.h"
#include "ItemKind.h"
#include "ItemLauncher.h"
#include "ItemPropertiesDialog.h"

#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
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
    connect(m_list, &QListWidget::itemPressed, this, &GroupWindow::itemSelected);
    connect(m_list, &IconListWidget::filesDropped, this, &GroupWindow::onFilesDropped);
    connect(m_list, &IconListWidget::programItemsDropped, this,
            &GroupWindow::onProgramItemsDropped);
    connect(m_list, &IconListWidget::programItemsReordered, this,
            &GroupWindow::onProgramItemsReordered);
    connect(m_list, &IconListWidget::programItemsMovedOut, this,
            &GroupWindow::onProgramItemsMovedOut);
    connect(m_list, &IconListWidget::deleteRequested, this, &GroupWindow::deleteItem);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            &GroupWindow::onContextMenuRequested);
}

void GroupWindow::setTitle(const QString &title) {
    m_title = title;
    emit titleChanged(title);
}

void GroupWindow::addProgramItem(const ProgramItem &item) {
    insertProgramItem(item, m_items.size());
}

void GroupWindow::insertProgramItem(const ProgramItem &item, int row) {
    ProgramItem it = item;
    completeProgramItem(it);
    m_items.insert(std::clamp(row, 0, int(m_items.size())), it);
    rebuildList();
    emit modified();
}

void GroupWindow::selectItem(int index) {
    if (index < 0 || index >= m_list->count()) return;
    QListWidgetItem *row = m_list->item(index);
    m_list->setCurrentItem(row);
    m_list->scrollToItem(row, QAbstractItemView::EnsureVisible);
    m_list->setFocus();
}

void GroupWindow::clearSelection() {
    m_list->clearSelection();
    // Also drop the current item, or the style still marks it with a focus
    // rectangle.
    m_list->setCurrentItem(nullptr);
}

void GroupWindow::adoptGlobalXpStyleIcons() {
    for (ProgramItem &it : m_items) it.xpStyleIcon = it.legacyIcon;
    rebuildList();
}

void GroupWindow::rebuildList() {
    m_list->clear();
    for (int i = 0; i < m_items.size(); ++i) {
        const ProgramItem &it = m_items[i];
        const QIcon shown = it.xpStyleIcon ? xpStyleIcon(it.icon) : it.icon;
        auto *listItem = new QListWidgetItem(shown, it.name);
        listItem->setData(IconListWidget::IndexRole, i);
        listItem->setData(IconListWidget::NameRole, it.name);
        listItem->setData(IconListWidget::CommandRole, it.command);
        listItem->setData(IconListWidget::ArgumentsRole, it.arguments);
        listItem->setData(IconListWidget::WorkingDirRole, it.workingDir);
        listItem->setData(IconListWidget::LauncherRole, it.launcher);
        // So an item dragged to another group keeps the icon somebody picked
        // for it, rather than being worked out again from its command.
        listItem->setData(IconListWidget::IconIndexRole, it.iconIndex);
        listItem->setData(IconListWidget::IconSourceRole, it.iconSource);
        listItem->setData(IconListWidget::XpStyleRole, it.xpStyleIcon);
        m_list->addItem(listItem);
    }
    m_list->updateGridSize();
}

int GroupWindow::indexOfListItem(QListWidgetItem *listItem) const {
    if (!listItem) return -1;
    return listItem->data(Qt::UserRole).toInt();
}

void GroupWindow::onItemActivated(QListWidgetItem *item) {
    const int idx = indexOfListItem(item);
    if (idx < 0 || idx >= m_items.size()) return;

    // Answered before it is acted on: starting a program can sit for a
    // moment -- a wine prefix to build, a Steam client to wake -- and until
    // something appears there is no telling whether the click was taken.
    m_list->flashItem(idx);

    // Started on the next turn of the loop, so the first frame of that is
    // on screen before anything that blocks.
    const ProgramItem starting = m_items[idx];
    QTimer::singleShot(0, this, [this, starting] { launch(starting); });
}

void GroupWindow::launch(const ProgramItem &item) {
    const QString error = launchProgramItem(item);
    if (error.isEmpty()) return;
    const bool opens = isDocument(item.command) || isFolder(item.command);
    QMessageBox::warning(this, opens ? tr("Open") : tr("Run"), error);
}

void GroupWindow::onFilesDropped(const QStringList &paths) {
    for (const QString &path : paths) {
        if (!isLaunchable(path)) continue;
        ProgramItem item;
        item.name = QFileInfo(path).completeBaseName();
        item.command = path;
        // The working directory is left to completeProgramItem(): beside the
        // file for most items, but a folder item works inside itself.
        addProgramItem(item);
    }
}

void GroupWindow::onProgramItemsDropped(const QVector<ProgramItem> &items, int row) {
    int at = std::clamp(row, 0, int(m_items.size()));
    for (const ProgramItem &item : items) {
        insertProgramItem(item, at);
        ++at;
    }
}

void GroupWindow::onProgramItemsReordered(const QVector<int> &sourceRows, int row) {
    if (sourceRows.size() != 1) return; // the grid only ever selects one
    const int from = sourceRows.first();
    if (from < 0 || from >= m_items.size()) return;

    // The insertion point was measured before the row was lifted out, so it
    // shifts down by one once the row ahead of it is removed.
    int to = row > from ? row - 1 : row;
    to = std::clamp(to, 0, int(m_items.size()) - 1);
    if (to == from) return;

    m_items.move(from, to);
    rebuildList();
    emit modified();
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
        // The command may now point somewhere else, so the icon is derived
        // again from scratch rather than kept from the previous target. An
        // icon picked by hand in the dialog is not derived at all: it is the
        // one the item is meant to wear.
        if (it.iconIndex < 0) {
            it.icon = QIcon();
            it.iconSource.clear();
            it.legacyIcon = false;
        }
        completeProgramItem(it);
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
        const QString &command = m_items[idx].command;
        QAction *runAction = menu.addAction(
            (isDocument(command) || isFolder(command)) ? tr("Open") : tr("Run"));
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
