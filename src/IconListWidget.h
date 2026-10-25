#pragma once

#include <QListWidget>
#include <QVector>
#include "ProgramItem.h"

// A Progman-style icon grid: fixed grid positions (no free dragging),
// large icon + label underneath. Accepts .exe files dropped in from an
// external file manager, and program items dragged over from another
// group window (move by default, copy while holding Ctrl).
class IconListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit IconListWidget(QWidget *parent = nullptr);

    // What each row carries, so a drag can be serialized without reaching back
    // into the owning group: the item's index and the item itself. It used to
    // be serialized field by field, until a field added to ProgramItem and not
    // added here went missing from every item dragged between groups.
    enum DataRole {
        IndexRole = Qt::UserRole,
        ItemRole,
    };

    // Resizes the (uniform) grid cells to fit the widest label. Call after
    // the rows have been repopulated.
    void updateGridSize();

    // Shows that an item has been started: its icon expands and fades in place.
    // Without it, double clicking a game gives no feedback at all, a program
    // taking several seconds to open a window.
    void flashItem(int row);

signals:
    void filesDropped(const QStringList &paths);
    void programItemsDropped(const QVector<ProgramItem> &items, int row);
    void programItemsMovedOut(const QVector<int> &indices);
    // A drag that started and ended in the same group: the rows are moved
    // rather than copied.
    void programItemsReordered(const QVector<int> &sourceRows, int row);
    // Delete pressed with an icon selected, carrying the same item index the
    // context menu uses. The group asks for confirmation, so the key never
    // deletes an item outright.
    void deleteRequested(int index);

protected:
    int dropRow(const QPoint &pos) const;

    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
};
