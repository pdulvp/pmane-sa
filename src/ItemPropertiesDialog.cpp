#include "ItemPropertiesDialog.h"

#include "CompatTools.h"
#include "IconEffect.h"
#include "IconPickerDialog.h"
#include "ItemDetails.h"
#include "PeIconExtractor.h"
#include "DialogChrome.h"

#include <QCheckBox>
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

    // The banner, the coloured surfaces and the field styling the dialogs
    // share; what is left here is this dialog's own form.
    const DialogChrome chrome = applyDialogChrome(this, tr("Properties"));

    m_nameEdit = new QLineEdit(this);
    m_commandEdit = new QLineEdit(this);
    m_argsEdit = new QLineEdit(this);
    m_workDirEdit = new QLineEdit(this);
    m_runnerCombo = new QComboBox(this);
    populateRunners(QString());

    m_nameEdit->setPlaceholderText(tr("The name under the icon"));
    m_commandEdit->setPlaceholderText(tr("Program, document or folder to open"));
    m_argsEdit->setPlaceholderText(tr("Passed to the program as it starts"));
    m_workDirEdit->setPlaceholderText(tr("Where the program starts out"));

    auto *commandBrowse = new QPushButton(tr("Browse..."), this);
    auto *commandRow = new QHBoxLayout;
    commandRow->setSpacing(8);
    commandRow->addWidget(m_commandEdit);
    commandRow->addWidget(commandBrowse);

    auto *workDirBrowse = new QPushButton(tr("Browse..."), this);
    auto *workDirRow = new QHBoxLayout;
    workDirRow->setSpacing(8);
    workDirRow->addWidget(m_workDirEdit);
    workDirRow->addWidget(workDirBrowse);

    // The icon in a well of its own, so it reads as the thing being chosen
    // rather than as a picture dropped beside a button.
    m_iconPreview = new QLabel(this);
    m_iconPreview->setObjectName(QStringLiteral("iconWell"));
    m_iconPreview->setFixedSize(kPreviewSize + 14, kPreviewSize + 14);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    auto *changeIconButton = new QPushButton(tr("Change Icon..."), this);
    m_xpStyleCheck = new QCheckBox(tr("XP style"), this);
    m_xpStyleCheck->setToolTip(tr("Tip the icon into a shallow perspective and lift it off "
                                  "the desktop with a shadow, the way Windows XP drew its "
                                  "own. Suits the flat artwork of 16-bit programs and .ico "
                                  "files best."));
    auto *iconRow = new QHBoxLayout;
    iconRow->setSpacing(12);
    iconRow->addWidget(m_iconPreview);
    iconRow->addWidget(changeIconButton);
    iconRow->addWidget(m_xpStyleCheck);
    iconRow->addStretch();

    m_form = new QFormLayout;
    // Labels down the right of their column and fields down the left of
    // theirs, so the two make a single line each rather than a ragged pair.
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setHorizontalSpacing(kDialogLabelGap);
    m_form->setVerticalSpacing(kDialogRowSpacing);
    m_form->addRow(tr("Description:"), m_nameEdit);
    m_form->addRow(tr("Command Line:"), commandRow);
    m_form->addRow(tr("Arguments:"), m_argsEdit);
    m_form->addRow(tr("Working Directory:"), workDirRow);
    m_form->addRow(tr("Run With:"), m_runnerCombo);
    m_form->addRow(tr("Icon:"), iconRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // OK is what the form is filled in for.
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(ok);
        ok->setDefault(true);
    }

    chrome.fields->addLayout(m_form);
    chrome.fields->addStretch();
    chrome.footer->addWidget(buttons);

    connect(m_commandEdit, &QLineEdit::textChanged, this,
            &ItemPropertiesDialog::updateRunnerVisibility);
    updateRunnerVisibility();

    connect(commandBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseCommand);
    connect(changeIconButton, &QPushButton::clicked, this, &ItemPropertiesDialog::changeIcon);
    connect(m_xpStyleCheck, &QCheckBox::toggled, this, &ItemPropertiesDialog::refreshIconPreview);
    connect(workDirBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseWorkingDir);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(520, sizeHint().height());
}

void ItemPropertiesDialog::setItem(const ProgramItem &item) {
    m_item = item;
    m_nameEdit->setText(item.name);
    m_commandEdit->setText(item.command);
    m_argsEdit->setText(item.arguments);
    m_workDirEdit->setText(item.workingDir);
    populateRunners(item.runner);
    m_xpStyleCheck->setChecked(item.xpStyleIcon);
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

    const QIcon shown =
        m_xpStyleCheck->isChecked() ? xpStyleIcon(probe.icon) : probe.icon;
    m_iconPreview->setPixmap(shown.isNull() ? QPixmap()
                                            : shown.pixmap(kPreviewSize, kPreviewSize));
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
    it.xpStyleIcon = m_xpStyleCheck->isChecked();
    return it;
}

void ItemPropertiesDialog::browseCommand() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select Program"), m_commandEdit->text(),
        tr("Programs and documents (*.exe *.EXE *.desktop *.conf *.pdf *.PDF *.cbz *.CBZ);;"
           "Programs (*.exe *.EXE);;Applications (*.desktop);;DOSBox configs (*.conf);;"
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
