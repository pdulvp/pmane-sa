#pragma once

#include <QDialog>
#include "ProgramItem.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
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
    // Picks one of the several icons the item's executable or .ico holds,
    // the way Progman's own "Change Icon" did.
    void changeIcon();
    // Which launchers take this file changes with the command, and the row
    // is there only when there is more than one of them to choose between.
    void updateLauncherVisibility();

private:
    // Lists the launcher profiles that take the file named in the command,
    // and keeps a profile the item names but that is no longer set up rather
    // than silently repointing the item.
    void populateLaunchers(const QString &selected);

    // Shows the icon the item would end up with. Called after anything that
    // may have changed it -- never on every keystroke, since working it out
    // means reading the file.
    void refreshIconPreview();

    QLineEdit *m_nameEdit;
    QLineEdit *m_commandEdit;
    QLineEdit *m_argsEdit;
    QLineEdit *m_workDirEdit;
    QComboBox *m_launcherCombo;
    QLabel *m_iconPreview;
    QCheckBox *m_xpStyleCheck;
    QFormLayout *m_form;
    bool m_launcherRowShown = true;
    // The item being edited, carrying the fields the dialog does not show --
    // its icon and where that came from -- through to item().
    ProgramItem m_item;
};
