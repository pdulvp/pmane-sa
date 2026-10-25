#include "IconPickerDialog.h"

#include "DialogChrome.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Big enough to show classic 32x32 artwork unscaled, with room around it for
// the selection frame.
constexpr int kCellIconSize = 32;
constexpr int kCellSize = 48;

// The index stored on each cell, which is the file's own numbering rather than
// the row: an icon that fails to decode leaves a gap in the listing.
constexpr int kIconIndexRole = Qt::UserRole;

} // namespace

IconPickerDialog::IconPickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Change Icon"));

    // The banner, coloured surfaces and field styling shared by the dialogs;
    // what follows is the file field and the icon listing.
    const DialogChrome chrome = applyDialogChrome(this, tr("Change Icon"));

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("Program, library or .ico file"));
    auto *browseButton = new QPushButton(tr("Browse..."), this);
    browseButton->setAutoDefault(false);

    auto *pathRow = new QHBoxLayout;
    pathRow->setSpacing(8);
    pathRow->addWidget(m_pathEdit);
    pathRow->addWidget(browseButton);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(kDialogLabelGap);
    form->setVerticalSpacing(kDialogRowSpacing);
    form->addRow(tr("File Name:"), pathRow);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kCellIconSize, kCellIconSize));
    m_list->setGridSize(QSize(kCellSize, kCellSize));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumHeight(kCellSize * 3 + 12);
    m_list->setSpacing(4);
    m_list->setFrameShape(QFrame::NoFrame); // the style sheet draws the edge

    auto *listLabel = new QLabel(tr("Icons in this file:"), this);

    // The result of reading the file, below the listing it describes.
    m_status = new QLabel(this);
    markHintLabel(m_status);
    m_status->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);
    markDefaultButton(m_okButton);
    m_okButton->setDefault(true);
    QPushButton *defaultButton = buttons->button(QDialogButtonBox::RestoreDefaults);
    defaultButton->setText(tr("Default Icon"));
    defaultButton->setAutoDefault(false);
    defaultButton->setToolTip(tr("Go back to the icon the item finds on its own."));

    chrome.fields->addLayout(form);
    chrome.fields->addWidget(listLabel);
    chrome.fields->addWidget(m_list, 1);
    chrome.fields->addWidget(m_status);
    chrome.footer->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this, &IconPickerDialog::browse);
    connect(m_pathEdit, &QLineEdit::editingFinished, this, &IconPickerDialog::reload);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            [this] { m_okButton->setEnabled(currentRow() >= 0); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(defaultButton, &QPushButton::clicked, this, [this] {
        m_reset = true;
        accept();
    });

    resize(520, 400);
}

void IconPickerDialog::setSource(const QString &path, int index) {
    m_pathEdit->setText(path);
    reload();

    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(kIconIndexRole).toInt() == index) {
            m_list->setCurrentRow(row);
            return;
        }
    }
    // The item uses the file's first icon, which is the one the listing opens
    // on, so pressing OK without choosing changes nothing.
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void IconPickerDialog::reload() {
    const QString path = m_pathEdit->text().trimmed();

    m_list->clear();
    m_icons = PeIconExtractor::listIcons(path, &m_source);

    for (const PeIconExtractor::IconInfo &info : m_icons) {
        auto *cell = new QListWidgetItem(info.icon, QString(), m_list);
        // Uniform cells, so the selection is a square around the icon rather
        // than the size that particular icon needed.
        cell->setSizeHint(QSize(kCellSize, kCellSize));
        cell->setData(kIconIndexRole, info.index);
        cell->setToolTip(info.bitCount > 0
                             ? tr("%1 x %2, %3-bit")
                                   .arg(info.width)
                                   .arg(info.height)
                                   .arg(info.bitCount)
                             : tr("%1 x %2").arg(info.width).arg(info.height));
    }

    if (m_icons.size() == 1) {
        m_status->setText(tr("One icon in this file."));
    } else if (!m_icons.isEmpty()) {
        m_status->setText(tr("%1 icons in this file.").arg(m_icons.size()));
    } else if (path.isEmpty()) {
        m_status->setText(tr("Name a program or icon file to choose an icon from."));
    } else if (!QFileInfo::exists(path)) {
        m_status->setText(tr("There is no file at that path."));
    } else {
        m_status->setText(tr("This file holds no icons we can read."));
    }

    m_okButton->setEnabled(false);
}

int IconPickerDialog::currentRow() const {
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    return selected.isEmpty() ? -1 : m_list->row(selected.first());
}

QString IconPickerDialog::sourcePath() const {
    return m_pathEdit->text().trimmed();
}

int IconPickerDialog::selectedIndex() const {
    const int row = currentRow();
    return row < 0 ? -1 : m_list->item(row)->data(kIconIndexRole).toInt();
}

QIcon IconPickerDialog::selectedIcon() const {
    const int row = currentRow();
    return row < 0 ? QIcon() : m_list->item(row)->icon();
}

bool IconPickerDialog::selectedIsLegacy() const {
    return m_source == PeIconExtractor::Source::Ne || m_source == PeIconExtractor::Source::Ico;
}

void IconPickerDialog::browse() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Icon Source"), m_pathEdit->text(),
        tr("Icon sources (*.exe *.EXE *.dll *.DLL *.ico *.ICO);;"
           "Programs (*.exe *.EXE);;Libraries (*.dll *.DLL);;Icons (*.ico *.ICO);;"
           "All Files (*)"));
    if (path.isEmpty()) return;

    m_pathEdit->setText(path);
    reload();
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}
