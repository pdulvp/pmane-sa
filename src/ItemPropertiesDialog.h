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

// The "Program Item Properties" dialog, modelled on Progman's own:
// Description / Command Line / Working Directory.
class ItemPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ItemPropertiesDialog(QWidget *parent = nullptr);

    void setItem(const ProgramItem &item);
    ProgramItem item() const;

private slots:
    void browseCommand();
    // Picks one of the icons the item's executable or .ico holds, as Progman's
    // own "Change Icon" did.
    void changeIcon();
    // Which launchers accept this file depends on the command, and the row is
    // shown only when there is more than one to choose between.
    void updateLauncherVisibility();
    // Runs the program once, outside gamescope, to see what size window it
    // opens. There is no other way to find out: the size is the program's own
    // decision at run time.
    void measureWindow();

private:
    // Shows the measured size and the size it will be displayed at.
    void refreshNativeSizeLabel();
    // Shows or hides the scaling row according to the launcher in use.
    void updateScaleRow();
    // The prefixes the chosen launcher offers: the per-program one, then the
    // named ones set up on the launcher. The row is shown only for launchers
    // that use prefixes.
    void updatePrefixRow();
    // Shows the arguments row only where the item's own arguments reach the
    // program, which means a Windows program started by wine or a Proton
    // build. The other launchers are given a file or an id they determine
    // themselves and have nowhere to put arguments.
    void updateArgumentsRow();

private:
    // Lists everything that can open the file named in the command: every
    // runner of every launcher claiming it. A launcher the item names that no
    // longer exists is kept in the list rather than silently repointing the
    // item.
    void populateLaunchers(const QString &selected);

    // Shows the icon the item would end up with. Called after anything that may
    // have changed it, but never on every keystroke, since resolving it means
    // reading the file.
    void refreshIconPreview();

    QLineEdit *m_nameEdit;
    QLineEdit *m_commandEdit;
    QLineEdit *m_argsEdit;
    QComboBox *m_launcherCombo;
    // Which wine prefix it runs in: its own, or one of the launcher's named
    // ones, for programs meant to share a prefix.
    QComboBox *m_prefixCombo;
    // Scaling, for the launchers that run Windows programs: the factor, the
    // size the program draws at, and the button that measures it.
    QWidget *m_scaleRow;
    QComboBox *m_scaleCombo;
    QPushButton *m_measureButton;
    QLabel *m_nativeSizeLabel;
    // The ReShade effect to run it through, from those installed. Only
    // gamescope can apply one.
    QComboBox *m_shaderCombo;
    QSize m_nativeSize;
    // The chosen scale, held separately from the combo box: the list is rebuilt
    // whenever the launcher changes, and the choice has to survive that.
    int m_scalePercent = 100;
    WindowScaleMode m_scaleMode = WindowScaleMode::Gamescope;
    QString m_shaderName;
    // The chosen prefix, held separately from the combo box for the same reason
    // as the scale: the list is rebuilt whenever the launcher changes.
    QString m_prefixName;
    QLabel *m_iconPreview;
    QCheckBox *m_xpStyleCheck;
    QFormLayout *m_form;
    // Marks the scale combo entries that use wine's own scaling rather than
    // gamescope's.
    static constexpr int kScaleModeRole = Qt::UserRole + 1;

    // Whether the description was typed by the user rather than filled in
    // automatically. This is what marks the name as the user's, which discovery
    // then leaves alone, even when it happens to match the file name.
    bool m_nameEdited = false;

    bool m_argumentsRowShown = true;
    bool m_launcherRowShown = true;
    bool m_prefixRowShown = true;
    bool m_scaleRowShown = true;
    // The item being edited, carrying the fields the dialog does not show, its
    // icon and icon source, through to item().
    ProgramItem m_item;
};
