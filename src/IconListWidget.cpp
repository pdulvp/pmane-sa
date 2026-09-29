#include "IconListWidget.h"

#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QUrl>

#include <algorithm>

namespace {
const char *const kProgramItemMimeType = "application/x-progman-item";

constexpr int kTextPadding = 16;
constexpr int kIconPadding = 24;
// A single very long name wraps instead of stretching the whole grid.
constexpr int kMaxCellWidth = 180;
constexpr int kTopPadding = 6;
constexpr int kIconTextGap = 4;
// Dead space between cells, so a click that lands between icons hits no item.
constexpr int kCellGap = 8;
// How far past the layout box an icon may be drawn at its native size. It
// covers the spare pixel an XP-styled icon carries for its shadow; anything
// larger is real artwork and gets scaled down instead.
constexpr int kNativeDrawSlack = 4;

// Draws an icon centred over its label, with the selection highlight
// hugging the text the way Progman did, rather than filling the cell.
class ProgramItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        // The view's own icon size, captured before initStyleOption clamps it
        // down to whatever the individual icon happens to offer. Using the
        // clamped value would give short icons a shorter cell, floating their
        // labels above their neighbours'.
        const QSize iconSize = option.decorationSize;

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QRect cell = opt.rect;
        const QString text = index.data(Qt::DisplayRole).toString();
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const bool selected = opt.state & QStyle::State_Selected;

        painter->save();

        const QRect iconRect(cell.x() + (cell.width() - iconSize.width()) / 2,
                             cell.y() + kTopPadding, iconSize.width(), iconSize.height());
        const QIcon::Mode mode = selected ? QIcon::Selected : QIcon::Normal;

        const QSize native = icon.actualSize(QSize(512, 512));
        const bool overflowsSlightly =
            native.height() > iconRect.height() || native.width() > iconRect.width();
        const bool withinSlack = native.height() <= iconRect.height() + kNativeDrawSlack &&
                                 native.width() <= iconRect.width() + kNativeDrawSlack;
        if (overflowsSlightly && withinSlack) {
            // A styled icon carries a spare pixel for its shadow. Draw it at
            // native size with the artwork flush to the top of the layout box
            // so the shadow spills into the gap; the box, and therefore the
            // label below it, stays exactly where it was.
            const QRect nativeRect(cell.x() + (cell.width() - native.width()) / 2,
                                   iconRect.top(), native.width(), native.height());
            painter->drawPixmap(nativeRect, icon.pixmap(native, mode));
        } else {
            // Bottom alignment gives every icon the same ground line just
            // above its label. Modern executables ship artwork as large as
            // 256px, which this scales down to the box.
            icon.paint(painter, iconRect, Qt::AlignHCenter | Qt::AlignBottom, mode);
        }

        const int textTop = iconRect.bottom() + kIconTextGap;
        const QRect textArea(cell.left() + 2, textTop, cell.width() - 4,
                             cell.bottom() - textTop);
        const int flags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
        const QRect textBounds =
            painter->fontMetrics().boundingRect(textArea, flags, text);

        if (selected) {
            painter->fillRect(textBounds.adjusted(-2, -1, 2, 1), opt.palette.highlight());
            painter->setPen(opt.palette.highlightedText().color());
        } else {
            painter->setPen(opt.palette.text().color());
        }
        painter->drawText(textArea, flags, text);

        painter->restore();
    }
};
}

IconListWidget::IconListWidget(QWidget *parent) : QListWidget(parent) {
    setViewMode(QListWidget::IconMode);
    setIconSize(QSize(32, 32));
    setResizeMode(QListWidget::Adjust);
    setMovement(QListWidget::Static);
    setWrapping(true);
    setFlow(QListWidget::LeftToRight);
    setWordWrap(true);
    setItemDelegate(new ProgramItemDelegate(this));
    updateGridSize();
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setAcceptDrops(true);
    // The viewport is the widget actually under the cursor during a drag,
    // and Qt hit-tests it for WA_AcceptDrops. Without this, external drops
    // are rejected before dragEnterEvent is ever called.
    viewport()->setAcceptDrops(true);
}

void IconListWidget::updateGridSize() {
    const QFontMetrics metrics(font());

    int widestText = 0;
    for (int i = 0; i < count(); ++i) {
        widestText = std::max(widestText, metrics.horizontalAdvance(item(i)->text()));
    }

    int cellWidth = std::max(widestText + kTextPadding, iconSize().width() + kIconPadding);
    cellWidth = std::min(cellWidth, kMaxCellWidth);

    // Reserve only as many label lines as some name actually needs, rather
    // than always two: unused lines would be dead space that still counts as
    // part of the item and keeps it selected when clicked.
    const int textWidth = cellWidth - kTextPadding;
    int labelLines = 1;
    for (int i = 0; i < count(); ++i) {
        const QRect bounds =
            metrics.boundingRect(QRect(0, 0, textWidth, 0),
                                 Qt::AlignHCenter | Qt::TextWordWrap, item(i)->text());
        labelLines = std::max(labelLines, bounds.height() / std::max(1, metrics.height()));
    }

    const int contentHeight =
        kTopPadding + iconSize().height() + kIconTextGap + labelLines * metrics.height();

    // The grid is a cell bigger than the item in each direction, so the strip
    // between neighbours belongs to no item and a click there deselects.
    setGridSize(QSize(cellWidth + kCellGap, contentHeight + kCellGap));
    // Every row reports the same hint so the cells stay uniform at the
    // widest label's width rather than collapsing to the narrowest.
    const QSize itemSize(cellWidth, contentHeight);
    for (int i = 0; i < count(); ++i) {
        item(i)->setSizeHint(itemSize);
    }
}

QStringList IconListWidget::mimeTypes() const {
    return {QString::fromLatin1(kProgramItemMimeType), QStringLiteral("text/uri-list")};
}

QMimeData *IconListWidget::mimeData(const QList<QListWidgetItem *> &items) const {
    if (items.isEmpty()) return nullptr;

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << int(items.size());
    for (const QListWidgetItem *item : items) {
        stream << item->data(IndexRole).toInt() << item->data(NameRole).toString()
               << item->data(CommandRole).toString() << item->data(ArgumentsRole).toString()
               << item->data(WorkingDirRole).toString() << item->data(RunnerRole).toString()
               << item->data(IconIndexRole).toInt() << item->data(IconSourceRole).toString()
               << item->data(XpStyleRole).toBool();
    }

    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(kProgramItemMimeType), payload);
    return mime;
}

void IconListWidget::startDrag(Qt::DropActions supportedActions) {
    Q_UNUSED(supportedActions);

    const QList<QListWidgetItem *> selected = selectedItems();
    if (selected.isEmpty()) return;

    QVector<int> indices;
    indices.reserve(selected.size());
    for (const QListWidgetItem *item : selected) {
        indices.append(item->data(IndexRole).toInt());
    }

    QMimeData *mime = mimeData(selected);
    if (!mime) return;

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(selected.first()->icon().pixmap(32, 32));
    drag->setHotSpot(QPoint(16, 16));

    // Plain drag moves the item to the other group; Ctrl copies it, which
    // is what Progman did.
    if (drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction) == Qt::MoveAction) {
        emit programItemsMovedOut(indices);
    }
}

// Where a drop lands, as an insertion point in the row order. A drop on the
// left half of an icon goes before it and the right half after it.
int IconListWidget::dropRow(const QPoint &pos) const {
    const int rows = count();
    if (rows == 0) return 0;

    const QModelIndex index = indexAt(pos);
    if (index.isValid()) {
        const QRect rect = visualRect(index);
        return pos.x() < rect.center().x() ? index.row() : index.row() + 1;
    }

    // Empty space. Pick the band of icons the drop belongs to — the lowest one
    // starting at or above it, so a drop under the grid belongs to the last
    // row rather than to the end of the list — then position within that band
    // by x, exactly as if an icon had been hit.
    bool found = false;
    int band = 0;
    for (int i = 0; i < rows; ++i) {
        const int top = visualRect(model()->index(i, 0)).top();
        if (top <= pos.y() && (!found || top > band)) {
            band = top;
            found = true;
        }
    }
    if (!found) return 0; // above the first row

    int insertAt = rows;
    for (int i = 0; i < rows; ++i) {
        const QRect rect = visualRect(model()->index(i, 0));
        if (rect.top() != band) continue;
        if (pos.x() < rect.center().x()) return i;
        insertAt = i + 1;
    }
    return insertAt;
}

void IconListWidget::mousePressEvent(QMouseEvent *event) {
    // A press in the gap between icons, or below a label, belongs to no item.
    // The view does not drop the selection on its own, so do it here.
    if (!indexAt(event->pos()).isValid()) {
        clearSelection();
        setCurrentItem(nullptr);
    }
    QListWidget::mousePressEvent(event);
}

void IconListWidget::dragEnterEvent(QDragEnterEvent *event) {
    const QMimeData *mime = event->mimeData();
    if (mime->hasUrls() || mime->hasFormat(kProgramItemMimeType)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void IconListWidget::dragMoveEvent(QDragMoveEvent *event) {
    const QMimeData *mime = event->mimeData();
    if (mime->hasUrls() || mime->hasFormat(kProgramItemMimeType)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void IconListWidget::dropEvent(QDropEvent *event) {
    const QMimeData *mime = event->mimeData();

    if (mime->hasFormat(kProgramItemMimeType)) {
        const int row = dropRow(event->position().toPoint());

        QByteArray payload = mime->data(QString::fromLatin1(kProgramItemMimeType));
        QDataStream stream(&payload, QIODevice::ReadOnly);
        int count = 0;
        stream >> count;

        QVector<ProgramItem> items;
        QVector<int> sourceRows;
        for (int i = 0; i < count; ++i) {
            int sourceIndex;
            ProgramItem item;
            stream >> sourceIndex >> item.name >> item.command >> item.arguments >>
                item.workingDir >> item.runner >> item.iconIndex >> item.iconSource >>
                item.xpStyleIcon;
            if (stream.status() != QDataStream::Ok) break;
            items.append(item);
            sourceRows.append(sourceIndex);
        }

        if (items.isEmpty()) {
            event->ignore();
            return;
        }

        if (event->source() == this) {
            // Reordering within the group. Report IgnoreAction so startDrag
            // does not also strip the rows out as if they had left.
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            emit programItemsReordered(sourceRows, row);
        } else {
            event->acceptProposedAction();
            emit programItemsDropped(items, row);
        }
        return;
    }

    if (!mime->hasUrls()) {
        event->ignore();
        return;
    }

    QStringList paths;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }

    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        emit filesDropped(paths);
    } else {
        event->ignore();
    }
}
