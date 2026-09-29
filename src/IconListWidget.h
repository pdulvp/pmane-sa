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

    // Roles carrying a ProgramItem's fields on each row, so a drag can be
    // serialized without reaching back into the owning group.
    enum DataRole {
        IndexRole = Qt::UserRole,
        NameRole,
        CommandRole,
        ArgumentsRole,
        WorkingDirRole,
        RunnerRole,
        IconIndexRole,
        IconSourceRole,
        XpStyleRole,
    };

    // Resizes the (uniform) grid cells to fit the widest label. Call after
    // the rows have been repopulated.
    void updateGridSize();

signals:
    void filesDropped(const QStringList &paths);
    void programItemsDropped(const QVector<ProgramItem> &items, int row);
    void programItemsMovedOut(const QVector<int> &indices);
    // A drag that started and ended in this same group: move the rows rather
    // than copying them across.
    void programItemsReordered(const QVector<int> &sourceRows, int row);

protected:
    int dropRow(const QPoint &pos) const;

    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
};
