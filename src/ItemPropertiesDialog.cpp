#include "ItemPropertiesDialog.h"

#include "CompatTools.h"
#include "IconPickerDialog.h"
#include "ItemDetails.h"
#include "PeIconExtractor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// The size the group windows show their icons at.
constexpr int kPreviewSize = 32;
} // namespace

ItemPropertiesDialog::ItemPropertiesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Program Item Properties"));

    m_nameEdit = new QLineEdit(this);
    m_commandEdit = new QLineEdit(this);
    m_argsEdit = new QLineEdit(this);
    m_workDirEdit = new QLineEdit(this);
    m_runnerCombo = new QComboBox(this);
    populateRunners(QString());

    auto *commandBrowse = new QPushButton(tr("Browse..."), this);
    auto *commandRow = new QHBoxLayout;
    commandRow->addWidget(m_commandEdit);
    commandRow->addWidget(commandBrowse);

    auto *workDirBrowse = new QPushButton(tr("Browse..."), this);
    auto *workDirRow = new QHBoxLayout;
    workDirRow->addWidget(m_workDirEdit);
    workDirRow->addWidget(workDirBrowse);

    m_iconPreview = new QLabel(this);
    m_iconPreview->setFixedSize(kPreviewSize, kPreviewSize);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    auto *changeIconButton = new QPushButton(tr("Change Icon..."), this);
    auto *iconRow = new QHBoxLayout;
    iconRow->addWidget(m_iconPreview);
    iconRow->addWidget(changeIconButton);
    iconRow->addStretch();

    m_form = new QFormLayout;
    m_form->addRow(tr("Description:"), m_nameEdit);
    m_form->addRow(tr("Command Line:"), commandRow);
    m_form->addRow(tr("Arguments:"), m_argsEdit);
    m_form->addRow(tr("Working Directory:"), workDirRow);
    m_form->addRow(tr("Run With:"), m_runnerCombo);
    m_form->addRow(tr("Icon:"), iconRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(m_form);
    layout->addWidget(buttons);

    connect(m_commandEdit, &QLineEdit::textChanged, this,
            &ItemPropertiesDialog::updateRunnerVisibility);
    updateRunnerVisibility();

    connect(commandBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseCommand);
    connect(changeIconButton, &QPushButton::clicked, this, &ItemPropertiesDialog::changeIcon);
    connect(workDirBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseWorkingDir);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(420, sizeHint().height());
}

void ItemPropertiesDialog::setItem(const ProgramItem &item) {
    m_item = item;
    m_nameEdit->setText(item.name);
    m_commandEdit->setText(item.command);
    m_argsEdit->setText(item.arguments);
    m_workDirEdit->setText(item.workingDir);
    populateRunners(item.runner);
    updateRunnerVisibility();
    refreshIconPreview();
}

void ItemPropertiesDialog::refreshIconPreview() {
    // The icon the item will actually end up with, worked out on a copy: the
    // one picked by hand if there is one, otherwise whatever the command it
    // now names leads to. Never stored back on m_item, so an icon nobody
    // chose stays a preview and is derived again for real on OK.
    ProgramItem probe = m_item;
    probe.command = m_commandEdit->text().trimmed();
    if (!probe.command.isEmpty()) completeProgramItem(probe);

    m_iconPreview->setPixmap(probe.icon.isNull()
                                 ? QPixmap()
                                 : probe.icon.pixmap(kPreviewSize, kPreviewSize));
}

void ItemPropertiesDialog::changeIcon() {
    ProgramItem probe = m_item;
    probe.command = m_commandEdit->text().trimmed();

    IconPickerDialog dlg(this);
    dlg.setSource(iconSourceFile(probe), m_item.iconIndex);
    if (dlg.exec() != QDialog::Accepted) return;

    if (dlg.isReset()) {
        // Back to whatever the item would find on its own, worked out again
        // once the dialog is closed.
        m_item.iconIndex = -1;
        m_item.iconSource.clear();
        m_item.icon = QIcon();
        m_item.legacyIcon = false;
    } else {
        if (dlg.selectedIcon().isNull()) return;
        m_item.iconIndex = dlg.selectedIndex();
        m_item.iconSource = dlg.sourcePath();
        m_item.icon = dlg.selectedIcon();
        m_item.legacyIcon = dlg.selectedIsLegacy();
    }
    refreshIconPreview();
}

void ItemPropertiesDialog::populateRunners(const QString &selected) {
    m_runnerCombo->clear();
    m_runnerCombo->addItem(tr("Wine (system)"), QString());
    for (const CompatTool &tool : installedCompatTools()) {
        m_runnerCombo->addItem(tool.displayName, tool.id);
    }

    if (selected.isEmpty()) {
        m_runnerCombo->setCurrentIndex(0);
        return;
    }

    const int index = m_runnerCombo->findData(selected);
    if (index >= 0) {
        m_runnerCombo->setCurrentIndex(index);
        return;
    }
    // The build this item was set up with has since been removed or
    // renamed. Offer it anyway, so saving the dialog does not quietly
    // demote the item to plain wine.
    m_runnerCombo->addItem(tr("%1 (not installed)").arg(selected), selected);
    m_runnerCombo->setCurrentIndex(m_runnerCombo->count() - 1);
}

void ItemPropertiesDialog::updateRunnerVisibility() {
    const bool show =
        m_commandEdit->text().trimmed().endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
    if (show == m_runnerRowShown) return;

    m_runnerRowShown = show;
    m_form->setRowVisible(m_runnerCombo, show);
    // The dialog is a row shorter or taller now. Resized only on the flip,
    // not on every keystroke in the command line.
    resize(width(), sizeHint().height());
}

ProgramItem ItemPropertiesDialog::item() const {
    // Everything the dialog does not show -- the icon and the file it came
    // from -- is carried over from the item being edited.
    ProgramItem it = m_item;
    it.name = m_nameEdit->text().trimmed();
    it.command = m_commandEdit->text().trimmed();
    it.arguments = m_argsEdit->text().trimmed();
    it.workingDir = m_workDirEdit->text().trimmed();
    it.runner = m_runnerCombo->currentData().toString();
    return it;
}

void ItemPropertiesDialog::browseCommand() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select Program"), m_commandEdit->text(),
        tr("Programs and documents (*.exe *.EXE *.conf *.pdf *.PDF *.cbz *.CBZ);;"
           "Programs (*.exe *.EXE);;DOSBox configs (*.conf);;"
           "Documents (*.pdf *.PDF *.cbz *.CBZ);;All Files (*)"));
    if (path.isEmpty()) return;

    m_commandEdit->setText(path);
    QFileInfo info(path);
    if (m_nameEdit->text().isEmpty()) m_nameEdit->setText(info.completeBaseName());
    if (m_workDirEdit->text().isEmpty()) m_workDirEdit->setText(info.absolutePath());

    // The item now points somewhere else, so it wears that file's icon --
    // unless one was picked by hand, which outlives a change of command.
    if (m_item.iconIndex < 0) {
        m_item.icon = QIcon();
        m_item.iconSource.clear();
        m_item.legacyIcon = false;
    }
    refreshIconPreview();
}

void ItemPropertiesDialog::browseWorkingDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"),
                                                      m_workDirEdit->text());
    if (!dir.isEmpty()) m_workDirEdit->setText(dir);
}
