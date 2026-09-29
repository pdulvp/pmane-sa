#pragma once

#include <QDialog>
#include "ProgramItem.h"

class QComboBox;
class QFormLayout;
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
    // Only a Windows executable goes through Wine or Proton -- DOSBox
    // configs, ScummVM games, ROMs and documents each have a launcher of
    // their own -- so the row is there only when it has something to say.
    void updateRunnerVisibility();

private:
    // Lists the system wine plus every installed Proton build, and keeps a
    // tool the item names but that is no longer installed rather than
    // silently rewriting the item to wine.
    void populateRunners(const QString &selected);

    QLineEdit *m_nameEdit;
    QLineEdit *m_commandEdit;
    QLineEdit *m_argsEdit;
    QLineEdit *m_workDirEdit;
    QComboBox *m_runnerCombo;
    QFormLayout *m_form;
    bool m_runnerRowShown = true;
};
