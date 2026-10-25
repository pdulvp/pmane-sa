#include "IconListWidget.h"

#include "WindowScale.h"

#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QStyledItemDelegate>

#include "Theme.h"
#include <QTextLayout>
#include <QTextOption>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const char *const kProgramItemMimeType = "application/x-progman-item";

constexpr int kTextPadding = 16;
constexpr int kIconPadding = 24;
// A long name wraps instead of stretching the whole grid: it runs to three
// icons' width before breaking, and to three lines before the rest is cut
// short with an ellipsis.
constexpr int kMaxLabelIconWidths = 3;
constexpr int kMaxLabelLines = 3;
// How far the selection highlight reaches past the label, on each side.
constexpr int kHighlightPadding = 2;
constexpr int kTopPadding = 6;
constexpr int kIconTextGap = 4;
// Dead space between cells, so a click that lands between icons hits no item.
constexpr int kCellGap = 8;
// Space above the first row, so icons do not sit against the top edge.
constexpr int kGridTopMargin = 4;
// How far past the layout box an icon may be drawn at its native size. It
// covers the spare pixel an XP-styled icon carries for its shadow; anything
// larger is real artwork and gets scaled down instead.
constexpr int kNativeDrawSlack = 4;

// The width one unbroken line of the name needs, measured with the same text
// layout that will wrap it. QFontMetrics cannot be used: it rounds a string's
// fractional advance down, so a cell built to that width ends up a fraction of
// a pixel short, and a label a fraction short of fitting wraps, leaving its
// last letter alone on a second line.
int unwrappedLabelWidth(const QString &text, const QFont &font) {
    if (text.isEmpty()) return 0;

    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    // Wide enough that nothing wraps, so the measurement is the width the name
    // needs rather than the width it was given.
    if (line.isValid()) line.setLineWidth(std::numeric_limits<int>::max());
    layout.endLayout();
    if (!line.isValid()) return 0;
    // Rounded up, never down: the layout wraps as soon as it needs more room
    // than it has, and a rounded-down pixel is exactly that shortfall.
    return int(std::ceil(line.naturalTextWidth()));
}

// Breaks a name into the lines it is drawn on: wrapped at `width`, at most
// `maxLines` of them, the last elided when the name runs past it. The grid
// measures labels with this and the delegate draws them with it, so the space
// reserved matches what appears.
QStringList labelLines(const QString &text, const QFont &font, int width, int maxLines) {
    if (text.isEmpty() || width <= 0 || maxLines <= 0) return {};

    const QFontMetrics metrics(font);
    QStringList lines;

    QTextOption option;
    // Wrapped between words and never inside one. A single word wider than the
    // cell overflows its line and is elided below rather than broken: the break
    // would fall at the end of the word and leave its last letter alone on a
    // line.
    option.setWrapMode(QTextOption::WordWrap);

    QTextLayout layout(text, font);
    layout.setTextOption(option);
    layout.beginLayout();
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);

        if (lines.size() == maxLines - 1) {
            // The last line there is room for: the remaining text goes on it,
            // elided to fit.
            lines.append(metrics.elidedText(text.mid(line.textStart()).trimmed(),
                                            Qt::ElideRight, width));
            break;
        }
        // Elided as well, for the one case the layout cannot handle: a word it
        // was told not to break is wider than its line. A line that fits is
        // returned unchanged.
        lines.append(metrics.elidedText(text.mid(line.textStart(), line.textLength()).trimmed(),
                                        Qt::ElideRight, width));
    }
    layout.endLayout();
    return lines;
}

// Draws an icon centred over its label, with the selection highlight around
// the text, as Progman did, rather than filling the cell.

// How much the artwork is dimmed in dark mode: enough to stop a grid of bright
// icons glaring, not so much that they look disabled.
constexpr qreal kDarkIconDim = 0.82;

// The feedback for a double click: the icon grows slightly and fades out, over
// about the time it takes to notice.
constexpr int kFlashMs = 380;
constexpr qreal kFlashGrowth = 0.7;

// The growing, fading copy of an icon that indicates a program is starting.
// Drawn over the list rather than by the delegate, since it has to spill
// outside its cell while the row underneath is unchanged.
class LaunchFlash : public QWidget {
public:
    LaunchFlash(QWidget *host, const QRect &iconRect, const QIcon &icon)
        : QWidget(host), m_icon(icon), m_from(iconRect) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_DeleteOnClose);
        setGeometry(host->rect());
        show();
        raise();

        auto *grow = new QVariantAnimation(this);
        grow->setStartValue(0.0);
        grow->setEndValue(1.0);
        grow->setDuration(kFlashMs);
        // Fast at first, then easing off.
        grow->setEasingCurve(QEasingCurve::OutQuad);
        connect(grow, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_through = value.toReal();
            update();
        });
        connect(grow, &QVariantAnimation::finished, this, &QWidget::close);
        grow->start(QAbstractAnimation::DeleteWhenStopped);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_icon.isNull()) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(1.0 - m_through);

        // Scaled about its centre, so it grows in place rather than drifting
        // towards a corner.
        const qreal scale = 1.0 + kFlashGrowth * m_through;
        const QSizeF grown(m_from.width() * scale, m_from.height() * scale);
        const QRectF where(m_from.center().x() + 0.5 - grown.width() / 2.0,
                           m_from.center().y() + 0.5 - grown.height() / 2.0,
                           grown.width(), grown.height());
        painter.drawPixmap(where, m_icon.pixmap(m_from.size() * 2), QRectF());
    }

private:
    QIcon m_icon;
    QRect m_from;
    qreal m_through = 0.0;
};

class ProgramItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        // The view's own icon size, read before initStyleOption clamps it to
        // what the individual icon provides. The clamped value would give small
        // icons a shorter cell, raising their labels above their neighbours'.
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

        // Artwork drawn for a white desktop glares on a dark one: a 16-colour
        // icon has no soft edges, so a row of them is harsh. Dimmed slightly
        // rather than recoloured, which would degrade the artwork.
        const qreal dim = darkMode() ? kDarkIconDim : 1.0;
        if (dim < 1.0) painter->setOpacity(painter->opacity() * dim);

        const QSize native = icon.actualSize(QSize(512, 512));
        const bool overflowsSlightly =
            native.height() > iconRect.height() || native.width() > iconRect.width();
        const bool withinSlack = native.height() <= iconRect.height() + kNativeDrawSlack &&
                                 native.width() <= iconRect.width() + kNativeDrawSlack;
        if (overflowsSlightly && withinSlack) {
            // A styled icon carries a spare pixel for its shadow. It is drawn
            // at native size with the artwork flush to the top of the layout
            // box, so the shadow falls into the gap and the box, and the label
            // below it, do not move.
            const QRect nativeRect(cell.x() + (cell.width() - native.width()) / 2,
                                   iconRect.top(), native.width(), native.height());
            painter->drawPixmap(nativeRect, icon.pixmap(native, mode));
        } else {
            // Bottom alignment gives every icon the same baseline just above
            // its label. Modern executables ship artwork up to 256px, which is
            // scaled down to the box.
            icon.paint(painter, iconRect, Qt::AlignHCenter | Qt::AlignBottom, mode);
        }

        if (dim < 1.0) painter->setOpacity(painter->opacity() / dim);

        const int textTop = iconRect.bottom() + kIconTextGap;
        // The same width the grid measured the label at, so a name takes the
        // number of lines the cell was sized for.
        const int textWidth = cell.width() - kTextPadding;
        const QStringList lines = labelLines(text, opt.font, textWidth, kMaxLabelLines);

        const QFontMetrics metrics(opt.font);
        // Each line is positioned explicitly rather than centred by drawText,
        // so the highlight can be measured against exactly where the text
        // lands. Letting the painter centre it leaves the two a rounded pixel
        // apart, which makes the box look off-centre.
        const auto lineLeft = [&](const QString &line) {
            return cell.left() + (cell.width() - metrics.horizontalAdvance(line)) / 2;
        };

        int widest = 0;
        for (const QString &line : lines) {
            widest = std::max(widest, metrics.horizontalAdvance(line));
        }
        const QRect textBounds(cell.left() + (cell.width() - widest) / 2, textTop, widest,
                               lines.size() * metrics.height());

        if (selected) {
            painter->fillRect(textBounds.adjusted(-kHighlightPadding, -1, kHighlightPadding, 1),
                              opt.palette.highlight());
            painter->setPen(opt.palette.highlightedText().color());
        } else {
            painter->setPen(opt.palette.text().color());
        }
        for (int i = 0; i < lines.size(); ++i) {
            const QRect lineRect(lineLeft(lines[i]), textTop + i * metrics.height(),
                                 metrics.horizontalAdvance(lines[i]), metrics.height());
            painter->drawText(lineRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextDontClip,
                              lines[i]);
        }

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
    // The strip created by the margin lies outside the viewport and is painted
    // by the frame, so the frame is given the viewport's colour rather than the
    // default window grey.
    setViewportMargins(0, kGridTopMargin, 0, 0);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
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

void IconListWidget::flashItem(int row) {
    QListWidgetItem *item = this->item(row);
    if (!item) return;

    // The rectangle the delegate draws the artwork in, which is what has to
    // appear to grow; the cell is mostly label and empty space.
    const QRect cell = visualItemRect(item);
    if (cell.isEmpty()) return;
    const QSize size = iconSize();
    const QRect iconRect(cell.x() + (cell.width() - size.width()) / 2,
                         cell.y() + kTopPadding, size.width(), size.height());

    new LaunchFlash(viewport(), iconRect, item->icon());
}

void IconListWidget::updateGridSize() {
    const QFontMetrics metrics(font());

    int widestText = 0;
    for (int i = 0; i < count(); ++i) {
        widestText = std::max(widestText, unwrappedLabelWidth(item(i)->text(), font()));
    }

    int cellWidth = std::max(widestText + kTextPadding, iconSize().width() + kIconPadding);
    cellWidth = std::min(cellWidth, iconSize().width() * kMaxLabelIconWidths + kTextPadding);

    // Reserve only as many label lines as the longest name needs, rather than
    // always three: unused lines would be dead space that still counts as part
    // of the item and selects it when clicked.
    const int textWidth = cellWidth - kTextPadding;
    int lineCount = 1;
    for (int i = 0; i < count(); ++i) {
        lineCount = std::max<int>(
            lineCount, labelLines(item(i)->text(), font(), textWidth, kMaxLabelLines).size());
    }

    const int contentHeight =
        kTopPadding + iconSize().height() + kIconTextGap + lineCount * metrics.height();

    // The grid is a cell bigger than the item in each direction, so the strip
    // between neighbours belongs to no item and a click there deselects.
    setGridSize(QSize(cellWidth + kCellGap, contentHeight + kCellGap));
    // Every row reports the same hint, so the cells stay uniform at the widest
    // label's width rather than collapsing to the narrowest.
    const QSize itemSize(cellWidth, contentHeight);
    for (int i = 0; i < count(); ++i) {
        item(i)->setSizeHint(itemSize);
    }
}

QStringList IconListWidget::mimeTypes() const {
    return {QString::fromLatin1(kProgramItemMimeType), QStringLiteral("text/uri-list")};
}

// The serialized form of an item, written and read in one place and in one
// order. The icon is left out: the receiving group derives it again from the
// command, and an icon chosen by hand is identified by iconIndex and
// iconSource.
void writeItem(QDataStream &stream, const ProgramItem &item) {
    stream << item.id << item.name << item.customName << item.command << item.arguments
           << item.launcher << item.prefix << item.windowScalePercent
           << int(item.windowScaleMode) << item.nativeSize << item.shader << item.legacyIcon
           << item.xpStyleIcon << item.iconIndex << item.iconSource;
}

ProgramItem readItem(QDataStream &stream) {
    ProgramItem item;
    int scaleMode = 0;
    stream >> item.id >> item.name >> item.customName >> item.command >> item.arguments >>
        item.launcher >> item.prefix >> item.windowScalePercent >> scaleMode >>
        item.nativeSize >> item.shader >> item.legacyIcon >> item.xpStyleIcon >>
        item.iconIndex >> item.iconSource;
    item.windowScaleMode = scaleMode == int(WindowScaleMode::WineDpi)
                               ? WindowScaleMode::WineDpi
                               : WindowScaleMode::Gamescope;
    return item;
}

QMimeData *IconListWidget::mimeData(const QList<QListWidgetItem *> &items) const {
    if (items.isEmpty()) return nullptr;

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << int(items.size());
    for (const QListWidgetItem *item : items) {
        stream << item->data(IndexRole).toInt();
        writeItem(stream, item->data(ItemRole).value<ProgramItem>());
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

    // A plain drag moves the item to the other group; Ctrl copies it, as in
    // Progman.
    if (drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction) == Qt::MoveAction) {
        emit programItemsMovedOut(indices);
    }
}

// Where a drop lands, as an insertion point in the row order. A drop on the
// left half of an icon goes before it, on the right half after it.
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

void IconListWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Delete && !event->modifiers()) {
        // Only a selected icon, not merely the one the focus rectangle sits on.
        QListWidgetItem *current = currentItem();
        if (current && current->isSelected()) {
            emit deleteRequested(current->data(IndexRole).toInt());
            event->accept();
            return;
        }
    }
    QListWidget::keyPressEvent(event);
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
            int sourceIndex = 0;
            stream >> sourceIndex;
            const ProgramItem item = readItem(stream);
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
