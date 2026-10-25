#pragma once

#include <QWidget>
#include <QVector>
#include "ProgramItem.h"

class IconListWidget;
class QListWidgetItem;

// A Program Group window: a titled MDI child holding a grid of ProgramItem
// icons, equivalent to a [group] section in progman.ini.
class GroupWindow : public QWidget {
    Q_OBJECT
public:
    explicit GroupWindow(const QString &title, QWidget *parent = nullptr);

    // Identifies this group as distinct from another with the same title.
    // Assigned when the group is created and kept from then on, see Ids.h, so
    // that what is recorded about a group survives a rename or a move.
    QString id() const { return m_id; }
    void setId(const QString &id) { if (!id.isEmpty()) m_id = id; }

    QString title() const { return m_title; }
    void setTitle(const QString &title);

    const QVector<ProgramItem> &items() const { return m_items; }
    // Removes an item without confirmation and without emitting itemDeleted:
    // what redoing a deletion does, both having happened the first time.
    void removeProgramItem(int index);
    void addProgramItem(const ProgramItem &item);
    void insertProgramItem(const ProgramItem &item, int row);
    void newItemDialog() { showPropertiesDialog(-1); }

    // Migration for desktops saved when XP icon styling was a single
    // application-wide switch: it restyled the legacy-artwork items, and each
    // item now carries the setting itself.
    void adoptGlobalXpStyleIcons();

    // Clears this group's selection, so only the active group has one.
    void clearSelection();
    // Scrolls an item into view, selects it and gives it focus. Called when a
    // search result is activated.
    void selectItem(int index);

signals:
    void titleChanged(const QString &title);
    // One of this group's icons was clicked, so nothing else on the desktop
    // should stay selected.
    void itemSelected();
    void modified();
    // An item the user deleted, and its position, so it can be restored. Not
    // emitted for an item dragged to another group, which still exists.
    void itemDeleted(int index, const ProgramItem &item);

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onFilesDropped(const QStringList &paths);
    void onProgramItemsDropped(const QVector<ProgramItem> &items, int row);
    void onProgramItemsReordered(const QVector<int> &sourceRows, int row);
    void onProgramItemsMovedOut(const QVector<int> &indices);
    void onContextMenuRequested(const QPoint &pos);

private:
    void rebuildList();
    void launch(const ProgramItem &item);
    void showPropertiesDialog(int index); // -1 == new item
    void deleteItem(int index);
    // A second icon for the same program, with an id, and so a wine prefix, of
    // its own.
    void duplicateItem(int index);
    int indexOfListItem(QListWidgetItem *listItem) const;

    QString m_id;
    QString m_title;
    IconListWidget *m_list;
    QVector<ProgramItem> m_items;
};
