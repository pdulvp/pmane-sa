#include "ItemPropertiesDialog.h"

#include "CompatTools.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

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

    m_form = new QFormLayout;
    m_form->addRow(tr("Description:"), m_nameEdit);
    m_form->addRow(tr("Command Line:"), commandRow);
    m_form->addRow(tr("Arguments:"), m_argsEdit);
    m_form->addRow(tr("Working Directory:"), workDirRow);
    m_form->addRow(tr("Run With:"), m_runnerCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(m_form);
    layout->addWidget(buttons);

    connect(m_commandEdit, &QLineEdit::textChanged, this,
            &ItemPropertiesDialog::updateRunnerVisibility);
    updateRunnerVisibility();

    connect(commandBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseCommand);
    connect(workDirBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseWorkingDir);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(420, sizeHint().height());
}

void ItemPropertiesDialog::setItem(const ProgramItem &item) {
    m_nameEdit->setText(item.name);
    m_commandEdit->setText(item.command);
    m_argsEdit->setText(item.arguments);
    m_workDirEdit->setText(item.workingDir);
    populateRunners(item.runner);
    updateRunnerVisibility();
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
    ProgramItem it;
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
}

void ItemPropertiesDialog::browseWorkingDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"),
                                                      m_workDirEdit->text());
    if (!dir.isEmpty()) m_workDirEdit->setText(dir);
}
