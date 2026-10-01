#pragma once

#include <QDialog>
#include <QSize>
#include "ProgramItem.h"

class QCheckBox;
class QPushButton;
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
    // Runs the program once, outside gamescope, to see how big a window it
    // puts up. Nothing else can tell us: the size is the program's own
    // decision at run time.
    void measureWindow();

private:
    // Says what was measured and what it will be shown at.
    void refreshNativeSizeLabel();
    // Shows or hides the whole scaling row, by what the item is run with.
    void updateScaleRow();

private:
    // Lists everything that can open the file named in the command -- every
    // runner of every launcher claiming it -- and keeps one the item names
    // but that is no longer set up rather than silently repointing the item.
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
    // Scaling, for the launchers that run Windows programs: how much, how
    // big the program draws, and the button that finds that out.
    QWidget *m_scaleRow;
    QComboBox *m_scaleCombo;
    QPushButton *m_measureButton;
    QLabel *m_nativeSizeLabel;
    QSize m_nativeSize;
    // The scale as chosen, kept apart from the combo: the list is rebuilt
    // whenever the launcher changes, and the choice has to survive that.
    int m_scalePercent = 100;
    WindowScaleMode m_scaleMode = WindowScaleMode::Gamescope;
    QLabel *m_iconPreview;
    QCheckBox *m_xpStyleCheck;
    QFormLayout *m_form;
    // Which entry of the scale combo is wine's own scaling rather than
    // gamescope's.
    static constexpr int kScaleModeRole = Qt::UserRole + 1;

    bool m_launcherRowShown = true;
    bool m_scaleRowShown = true;
    // The item being edited, carrying the fields the dialog does not show --
    // its icon and where that came from -- through to item().
    ProgramItem m_item;
};
