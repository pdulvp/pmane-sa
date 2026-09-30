#pragma once

#include <QDialog>
#include <QVector>

#include "Launchers.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// "Launchers": what each kind of file is started with. A tree down the left --
// a launcher, with its launcher profiles under it -- and whatever the row
// selected there has to be said about it on the right: the file types and the
// shared tables on a launcher, the command line and the options on a profile.
class LaunchersDialog : public QDialog {
    Q_OBJECT
public:
    explicit LaunchersDialog(QWidget *parent = nullptr);

    // The launchers as the dialog leaves them. Only meaningful after OK.
    QVector<Launcher> launchers() const { return m_launchers; }

private:
    // Which row of the tree is which, kept on the item itself: a launcher
    // carries its index, a profile its launcher's index and its own.
    enum Role { LauncherRole = Qt::UserRole, ProfileRole };

    void buildLauncherPage();
    void buildProfilePage();
    // A profile is renamed where its name is: on its row in the tree, by
    // double click or through the menu on a right click.
    void renameSelectedProfile();
    void showTreeMenu(const QPoint &pos);
    void onTreeItemChanged(QTreeWidgetItem *item, int column);
    void rebuildTree(int selectLauncher, int selectProfile);
    // Reads the page on show back into the launcher it belongs to, which is
    // what makes moving between rows keep what was typed.
    void commitCurrentPage();
    void showPageFor(QTreeWidgetItem *item);
    void addProfile();
    void duplicateProfile();
    void removeProfile();
    // Which launcher and profile the selected row stands for; profile -1 on a
    // launcher row.
    void currentSelection(int *launcher, int *profile) const;

    QVector<Launcher> m_launchers;

    QTreeWidget *m_tree;
    QStackedWidget *m_stack;
    QPushButton *m_addButton;
    QPushButton *m_duplicateButton;
    QPushButton *m_removeButton;

    // The launcher page.
    QWidget *m_launcherPage;
    QLabel *m_launcherName;
    QLabel *m_launcherDescription;
    QLineEdit *m_extensions;
    QLineEdit *m_launcherCommand;
    QLineEdit *m_launcherArguments;
    QCheckBox *m_launcherUseShell;
    QFormLayout *m_launcherForm;
    QLabel *m_launcherPlaceholders;
    QWidget *m_coresBox;
    QLineEdit *m_coresDir;
    QTableWidget *m_cores;
    QWidget *m_buildsBox;
    QListWidget *m_builds;

    // The profile page.
    QWidget *m_profilePage;
    QFormLayout *m_profileForm;
    QLabel *m_profileHeading;
    QLineEdit *m_extraArguments;
    QComboBox *m_compatTool;
    QLineEdit *m_prefixesDir;
    QLineEdit *m_baseConfig;
    QLineEdit *m_core;
    QLabel *m_placeholders;

    // The row the fields on show belong to, so they can be written back
    // before another row takes their place.
    int m_editingLauncher = -1;
    int m_editingProfile = -1;
};
