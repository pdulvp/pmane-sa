#include "IconListWidget.h"

#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QMimeData>
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

// Draws an icon centred over its label, with the selection highlight
// hugging the text the way Progman did, rather than filling the cell.
class ProgramItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QRect cell = opt.rect;
        const QString text = index.data(Qt::DisplayRole).toString();
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const bool selected = opt.state & QStyle::State_Selected;

        painter->save();

        const QSize iconSize = opt.decorationSize;
        const QRect iconRect(cell.x() + (cell.width() - iconSize.width()) / 2,
                             cell.y() + kTopPadding, iconSize.width(), iconSize.height());
        icon.paint(painter, iconRect, Qt::AlignCenter,
                   selected ? QIcon::Selected : QIcon::Normal);

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
    const int cellHeight = iconSize().height() + metrics.height() * 2 + kIconPadding;
    const QSize cell(cellWidth, cellHeight);

    setGridSize(cell);
    // Every row reports the same hint so the cells stay uniform at the
    // widest label's width rather than collapsing to the narrowest.
    for (int i = 0; i < count(); ++i) {
        item(i)->setSizeHint(cell);
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
               << item->data(WorkingDirRole).toString();
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

// Rearranging icons within one group is not supported: the grid is always
// auto-arranged, so a drop onto the originating group has nothing to do.
static bool isSelfDrag(const QMimeData *mime, const QObject *source, const QObject *self) {
    return mime->hasFormat(kProgramItemMimeType) && source == self;
}

void IconListWidget::dragEnterEvent(QDragEnterEvent *event) {
    const QMimeData *mime = event->mimeData();
    if (isSelfDrag(mime, event->source(), this)) {
        event->ignore();
    } else if (mime->hasUrls() || mime->hasFormat(kProgramItemMimeType)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void IconListWidget::dragMoveEvent(QDragMoveEvent *event) {
    const QMimeData *mime = event->mimeData();
    if (isSelfDrag(mime, event->source(), this)) {
        event->ignore();
    } else if (mime->hasUrls() || mime->hasFormat(kProgramItemMimeType)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void IconListWidget::dropEvent(QDropEvent *event) {
    const QMimeData *mime = event->mimeData();

    if (mime->hasFormat(kProgramItemMimeType)) {
        if (event->source() == this) {
            event->ignore();
            return;
        }

        QByteArray payload = mime->data(QString::fromLatin1(kProgramItemMimeType));
        QDataStream stream(&payload, QIODevice::ReadOnly);
        int count = 0;
        stream >> count;

        QVector<ProgramItem> items;
        for (int i = 0; i < count; ++i) {
            int sourceIndex;
            ProgramItem item;
            stream >> sourceIndex >> item.name >> item.command >> item.arguments >>
                item.workingDir;
            if (stream.status() != QDataStream::Ok) break;
            items.append(item);
        }

        if (!items.isEmpty()) {
            event->acceptProposedAction();
            emit programItemsDropped(items);
        } else {
            event->ignore();
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
