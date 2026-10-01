#pragma once

#include <QDialog>
#include <QVector>

#include "Launchers.h"

class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// "Launchers": what each kind of file is started with. The launchers down the
// left, and what the one selected there has to be said about it on the right:
// the file types it claims, the programs that can act as it, and the folders
// it keeps things in.
//
// Nothing here is a command line. How a launcher is handed a file belongs to
// the launcher, and a program's own arguments belong to the item.
class LaunchersDialog : public QDialog {
    Q_OBJECT
public:
    explicit LaunchersDialog(QWidget *parent = nullptr);

    // The launchers as the dialog leaves them. Only meaningful after OK.
    QVector<Launcher> launchers() const { return m_launchers; }

private:
    // Which launcher a row stands for, kept on the row itself.
    enum Role { LauncherRole = Qt::UserRole };

    void buildLauncherPage();
    // One row of the runners table. A wine or a Proton build read off the
    // machine is shown but not edited.
    void addRunnerRow(const LauncherRunner &runner);
    bool runnerRowIsDiscovered(int row) const;
    // How many rows are this dialog's own, the discovered ones being the
    // machine's: the last of those cannot be removed.
    int editableRunnerRows() const;
    void browseForRunner();

    void buildTree(int selectLauncher);
    // Reads the page on show back into the launcher it belongs to, which is
    // what makes moving between rows keep what was typed.
    void commitCurrentPage();
    void showPageFor(QTreeWidgetItem *item);

    QVector<Launcher> m_launchers;

    QTreeWidget *m_tree;

    // The launcher page: the only page there is.
    QWidget *m_launcherPage;
    QLabel *m_launcherName;
    QLabel *m_launcherDescription;
    QLineEdit *m_extensions;
    QFormLayout *m_launcherForm;
    QLabel *m_runnersTitle;
    QTableWidget *m_runners;
    QPushButton *m_runnerAdd;
    QPushButton *m_runnerRemove;
    QPushButton *m_runnerBrowse;
    QLabel *m_runnersNote;
    QWidget *m_prefixesBox;
    QLineEdit *m_prefixesDir;
    QWidget *m_coresBox;
    QLineEdit *m_coresDir;
    QTableWidget *m_cores;

    // The launcher the fields on show belong to, so they can be written back
    // before another row takes their place.
    int m_editingLauncher = -1;
};
