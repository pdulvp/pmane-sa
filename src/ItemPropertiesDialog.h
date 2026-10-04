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
    // The prefixes the chosen launcher offers: the one every program gets of
    // its own, then whatever has been set up by hand on the launcher. The row
    // is there only for the launchers that keep prefixes at all.
    void updatePrefixRow();
    // Shows the arguments row only where the item's own arguments reach the
    // program: a Windows program started by wine or by a Proton build. The
    // rest of the launchers are handed a file or an id of their own working
    // out and have nowhere to put them.
    void updateArgumentsRow();

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
    QComboBox *m_launcherCombo;
    // Which wine prefix it runs in: its own, or one of the launcher's named
    // ones for programs meant to share a bottle.
    QComboBox *m_prefixCombo;
    // Scaling, for the launchers that run Windows programs: how much, how
    // big the program draws, and the button that finds that out.
    QWidget *m_scaleRow;
    QComboBox *m_scaleCombo;
    QPushButton *m_measureButton;
    QLabel *m_nativeSizeLabel;
    // The ReShade effect to run it through, from what is installed. Only
    // gamescope can apply one.
    QComboBox *m_shaderCombo;
    QSize m_nativeSize;
    // The scale as chosen, kept apart from the combo: the list is rebuilt
    // whenever the launcher changes, and the choice has to survive that.
    int m_scalePercent = 100;
    WindowScaleMode m_scaleMode = WindowScaleMode::Gamescope;
    QString m_shaderName;
    // The prefix as chosen, kept apart from the combo for the same reason the
    // scale is: the list is rebuilt whenever the launcher changes.
    QString m_prefixName;
    QLabel *m_iconPreview;
    QCheckBox *m_xpStyleCheck;
    QFormLayout *m_form;
    // Which entry of the scale combo is wine's own scaling rather than
    // gamescope's.
    static constexpr int kScaleModeRole = Qt::UserRole + 1;

    // Whether the description was typed in rather than filled in for us.
    // What makes it the item's own name, which discovery then leaves alone:
    // a name that happens to match its file's is still the user's.
    bool m_nameEdited = false;

    bool m_argumentsRowShown = true;
    bool m_launcherRowShown = true;
    bool m_prefixRowShown = true;
    bool m_scaleRowShown = true;
    // The item being edited, carrying the fields the dialog does not show --
    // its icon and where that came from -- through to item().
    ProgramItem m_item;
};
