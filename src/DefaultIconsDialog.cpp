#include "DefaultIconsDialog.h"

#include "DialogChrome.h"
#include "IconPickerDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// The columns: the file type, the icon it gets, and where that icon comes
// from.
constexpr int kType = 0;
constexpr int kIcon = 1;
constexpr int kSource = 2;

// The size the group windows draw their icons at, so what is shown here is
// what will be on the desktop.
constexpr int kIconSize = 32;

} // namespace

DefaultIconsDialog::DefaultIconsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Default Icons"));

    const DialogChrome chrome = applyDialogChrome(this, tr("Default Icons"));

    m_icons = defaultIcons();

    m_hint = new QLabel(tr("The icon a file of each type gets when it carries none of its "
                           "own. Artwork inside a program, and an icon chosen for an item "
                           "itself, both come first."),
                        this);
    m_hint->setWordWrap(true);
    markHintLabel(m_hint);
    chrome.fields->addWidget(m_hint);

    const QStringList types = everyFileType();

    m_table = new QTableWidget(types.size(), 3, this);
    m_table->setHorizontalHeaderLabels({tr("File type"), tr("Icon"), tr("From")});
    m_table->horizontalHeader()->setSectionResizeMode(kType, QHeaderView::ResizeToContents);
    // Wide enough for the artwork whether or not the type has any, so the
    // column does not collapse to nothing on a desktop where none is set.
    m_table->horizontalHeader()->setSectionResizeMode(kIcon, QHeaderView::Fixed);
    m_table->setColumnWidth(kIcon, kIconSize + 16);
    m_table->horizontalHeader()->setSectionResizeMode(kSource, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setIconSize(QSize(kIconSize, kIconSize));
    m_table->setFrameShape(QFrame::NoFrame);

    for (int row = 0; row < types.size(); ++row) {
        auto *type = new QTableWidgetItem(types.at(row));
        m_table->setItem(row, kType, type);
        m_table->setItem(row, kIcon, new QTableWidgetItem);
        m_table->setItem(row, kSource, new QTableWidgetItem);
        // Tall enough for the artwork to be drawn at its own size.
        m_table->setRowHeight(row, kIconSize + 8);
        showRow(row);
    }
    chrome.fields->addWidget(m_table, 1);

    m_change = new QPushButton(tr("Change Icon..."), this);
    m_clear = new QPushButton(tr("Clear"), this);
    m_change->setAutoDefault(false);
    m_clear->setAutoDefault(false);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_change);
    buttons->addWidget(m_clear);
    chrome.fields->addLayout(buttons);

    connect(m_change, &QPushButton::clicked, this, &DefaultIconsDialog::changeIcon);
    connect(m_clear, &QPushButton::clicked, this, &DefaultIconsDialog::clearIcon);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &DefaultIconsDialog::updateButtons);
    // A double click on a row is the quickest way in, as it is in the item
    // listing of a group.
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *) { changeIcon(); });

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    markDefaultButton(box->button(QDialogButtonBox::Ok));
    chrome.footer->addWidget(box);

    if (!types.isEmpty()) m_table->selectRow(0);
    updateButtons();
    resize(520, 520);
}

QString DefaultIconsDialog::typeAt(int row) const {
    const QTableWidgetItem *cell = m_table->item(row, kType);
    return cell ? cell->text() : QString();
}

void DefaultIconsDialog::showRow(int row) {
    const QString type = typeAt(row);
    const DefaultIcon icon = m_icons.value(type);

    QTableWidgetItem *artwork = m_table->item(row, kIcon);
    QTableWidgetItem *source = m_table->item(row, kSource);
    if (!artwork || !source) return;

    if (!icon.isSet()) {
        artwork->setIcon(QIcon());
        // Said rather than left blank: an empty row reads as a row that failed
        // to fill in.
        source->setText(tr("None"));
        source->setToolTip(QString());
        return;
    }

    bool legacy = false;
    artwork->setIcon(iconFromDefault(icon, &legacy));
    source->setText(QFileInfo(icon.file).fileName());
    // The whole path, which the file name alone does not give.
    source->setToolTip(icon.file);
}

void DefaultIconsDialog::changeIcon() {
    const int row = m_table->currentRow();
    if (row < 0) return;
    const QString type = typeAt(row);
    const DefaultIcon current = m_icons.value(type);

    IconPickerDialog dlg(this);
    dlg.setSource(current.file, current.index);
    if (dlg.exec() != QDialog::Accepted) return;

    if (dlg.isReset() || dlg.sourcePath().isEmpty()) {
        m_icons.remove(type);
    } else {
        DefaultIcon icon;
        icon.file = dlg.sourcePath();
        icon.index = dlg.selectedIndex();
        icon.legacy = dlg.selectedIsLegacy();
        m_icons.insert(type, icon);
    }
    showRow(row);
    updateButtons();
}

void DefaultIconsDialog::clearIcon() {
    const int row = m_table->currentRow();
    if (row < 0) return;
    m_icons.remove(typeAt(row));
    showRow(row);
    updateButtons();
}

void DefaultIconsDialog::updateButtons() {
    const int row = m_table->currentRow();
    m_change->setEnabled(row >= 0);
    m_clear->setEnabled(row >= 0 && m_icons.value(typeAt(row)).isSet());
}
