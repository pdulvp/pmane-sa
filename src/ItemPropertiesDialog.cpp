#include "ItemPropertiesDialog.h"

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

    auto *commandBrowse = new QPushButton(tr("Browse..."), this);
    auto *commandRow = new QHBoxLayout;
    commandRow->addWidget(m_commandEdit);
    commandRow->addWidget(commandBrowse);

    auto *workDirBrowse = new QPushButton(tr("Browse..."), this);
    auto *workDirRow = new QHBoxLayout;
    workDirRow->addWidget(m_workDirEdit);
    workDirRow->addWidget(workDirBrowse);

    auto *form = new QFormLayout;
    form->addRow(tr("Description:"), m_nameEdit);
    form->addRow(tr("Command Line:"), commandRow);
    form->addRow(tr("Arguments:"), m_argsEdit);
    form->addRow(tr("Working Directory:"), workDirRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

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
}

ProgramItem ItemPropertiesDialog::item() const {
    ProgramItem it;
    it.name = m_nameEdit->text().trimmed();
    it.command = m_commandEdit->text().trimmed();
    it.arguments = m_argsEdit->text().trimmed();
    it.workingDir = m_workDirEdit->text().trimmed();
    return it;
}

void ItemPropertiesDialog::browseCommand() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select Program"), m_commandEdit->text(),
        tr("Programs (*.exe *.EXE);;All Files (*)"));
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
