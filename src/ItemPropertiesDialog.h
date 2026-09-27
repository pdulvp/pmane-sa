#pragma once

#include <QDialog>
#include "ProgramItem.h"

class QLineEdit;

// The "Program Item Properties" dialog, modeled after Progman's own:
// Description / Command Line / Working Directory.
class ItemPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ItemPropertiesDialog(QWidget *parent = nullptr);

    void setItem(const ProgramItem &item);
    ProgramItem item() const;

private slots:
    void browseCommand();
    void browseWorkingDir();

private:
    QLineEdit *m_nameEdit;
    QLineEdit *m_commandEdit;
    QLineEdit *m_argsEdit;
    QLineEdit *m_workDirEdit;
};
