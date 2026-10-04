#pragma once

#include <QWidget>
#include <QVector>
#include "ProgramItem.h"

class IconListWidget;
class QListWidgetItem;

// A Program Group window: a titled MDI child holding a grid of
// ProgramItem icons, equivalent to a [group] section in progman.ini.
class GroupWindow : public QWidget {
    Q_OBJECT
public:
    explicit GroupWindow(const QString &title, QWidget *parent = nullptr);

    QString title() const { return m_title; }
    void setTitle(const QString &title);

    const QVector<ProgramItem> &items() const { return m_items; }
    // Takes one out without asking and without announcing it: what redoing
    // a deletion does, the asking and the announcing having happened the
    // first time round.
    void removeProgramItem(int index);
    void addProgramItem(const ProgramItem &item);
    void insertProgramItem(const ProgramItem &item, int row);
    void newItemDialog() { showPropertiesDialog(-1); }

    // Migration for desktops saved when XP icon styling was one switch for
    // the whole Program Manager: what that switch actually restyled was the
    // legacy-artwork items, and each item now carries the setting itself.
    void adoptGlobalXpStyleIcons();

    // Drops this group's highlighted icon, so only the active group carries
    // a selection.
    void clearSelection();
    // Picks one out and shows it: scrolled into view, selected and focused.
    // What a search result does when it is activated.
    void selectItem(int index);

signals:
    void titleChanged(const QString &title);
    // One of this group's icons was clicked, so nothing else on the desktop
    // should stay selected.
    void itemSelected();
    void modified();
    // An item the user deleted, and where it was, so that it can be put
    // back. Not emitted for an item dragged to another group: that one has
    // not gone anywhere.
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
    int indexOfListItem(QListWidgetItem *listItem) const;

    QString m_title;
    IconListWidget *m_list;
    QVector<ProgramItem> m_items;
};
