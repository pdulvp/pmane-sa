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
    void addProgramItem(const ProgramItem &item);
    void newItemDialog() { showPropertiesDialog(-1); }

    // Renders legacy (16-bit / .ico) icons in the Windows XP manner.
    void setXpStyleIcons(bool enabled);

    // Drops this group's highlighted icon, so only the active group carries
    // a selection.
    void clearSelection();

signals:
    void titleChanged(const QString &title);
    void modified();

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onFilesDropped(const QStringList &paths);
    void onProgramItemsDropped(const QVector<ProgramItem> &items);
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
    bool m_xpStyleIcons = false;
};
