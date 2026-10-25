#pragma once

#include <QDialog>
#include <QVector>

#include "Launchers.h"
#include "ProgramItem.h"

class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// "Launchers": what each kind of file is started with. The launchers are
// listed on the left; the settings of the selected one are on the right: the
// file types it claims, the programs that can act as it, and the folders it
// uses.
//
// Nothing here is a command line. How a launcher is handed a file belongs to
// the launcher, and a program's own arguments belong to the item.
class LaunchersDialog : public QDialog {
    Q_OBJECT
public:
    explicit LaunchersDialog(QWidget *parent = nullptr);

    // The edited launchers. Only meaningful after OK.
    QVector<Launcher> launchers() const { return m_launchers; }

    // The current desktop, so the prefix usage count covers the items on
    // screen rather than the ones last written to file: a screen is only saved
    // when it is closed, so a newly added item is not in the file yet.
    void setOpenScreen(const QString &name, const QVector<ProgramItem> &items);

private:
    // The launcher a row stands for, stored on the row itself.
    enum Role { LauncherRole = Qt::UserRole };

    void buildLauncherPage();
    // One row of the runners table. A detected wine or Proton build is shown
    // but not editable.
    void addRunnerRow(const LauncherRunner &runner);
    bool runnerRowIsDiscovered(int row) const;
    // How many rows are editable, the detected ones not being: the last
    // editable row cannot be removed.
    int editableRunnerRows() const;
    void browseForRunner();

    // What a row of the prefixes table stands for.
    enum class PrefixRow {
        Fixed,    // the per-item prefix, which cannot be edited here
        Named,    // one created here, shared by the items pointed at it
        Leftover, // a folder in there that no item refers to any more
        Program,  // a program running in the prefix above it, not a prefix
    };
    QTreeWidgetItem *addPrefixRow(PrefixRow kind, const QString &name, const QString &used,
                                  const QString &path, const QString &note);
    // The programs running in a prefix, listed as children of it, which is how
    // one tells whether a prefix is still needed. Collapsed initially; the ones
    // expanded stay expanded across a rebuild.
    void addPrefixPrograms(QTreeWidgetItem *parent, const QStringList &programs);
    // Rebuilds the table from the names as typed and a fresh scan of the
    // prefixes folder, so used and unused follow the current items rather than
    // those present when the dialog opened.
    void refreshPrefixTable();
    // The named prefixes as the tree holds them, blank rows included, which a
    // rebuild has to preserve.
    QStringList typedPrefixes() const;
    // Fills the size column, which means walking every prefix, so it runs on
    // request rather than automatically.
    void measurePrefixes();
    // Removes the selected prefix, and offers to delete its folder too.
    void removePrefix();
    // Enables the buttons according to what is selected: a program row is not
    // a prefix, and the per-item prefix cannot be added to or removed.
    void updatePrefixButtons();
    // One row of the Steam library table; a `discovered` one comes from Steam
    // itself and is not editable.
    void addLibraryRow(const QString &path, bool discovered);
    void browseForLibrary();

    void buildTree(int selectLauncher);
    // Writes the visible page back into its launcher, so moving between rows
    // keeps what was typed.
    void commitCurrentPage();
    void showPageFor(QTreeWidgetItem *item);

    QVector<Launcher> m_launchers;

    // The current screen and its items, for the prefix usage count.
    QString m_openScreen;
    QVector<ProgramItem> m_openItems;

    QTreeWidget *m_tree;

    // The launcher page, the only page there is.
    QWidget *m_launcherPage;
    QLineEdit *m_extensions;
    QFormLayout *m_launcherForm;
    QLabel *m_runnersTitle;
    QTableWidget *m_runners;
    QPushButton *m_runnerAdd;
    QPushButton *m_runnerRemove;
    QPushButton *m_runnerBrowse;
    QWidget *m_prefixesBox;
    QLineEdit *m_prefixesDir;
    // The prefixes an item can be pointed at: the per-item one first, then the
    // named ones created by hand.
    QTreeWidget *m_prefixes;
    // Which prefixes were expanded, so a rebuild after a rename, a removal or
    // a size measurement does not collapse them.
    QStringList m_openPrefixes;
    QPushButton *m_prefixAdd;
    QPushButton *m_prefixRemove;
    QPushButton *m_prefixSizes;
    // The Steam library folders. The ones Steam lists itself are shown but not
    // editable, like a detected runner.
    QWidget *m_librariesBox;
    QTableWidget *m_libraries;
    QPushButton *m_libraryAdd;
    QPushButton *m_libraryRemove;

    QWidget *m_coresBox;
    QLineEdit *m_coresDir;
    QTableWidget *m_cores;

    // The launcher the visible fields belong to, so they can be written back
    // before another row replaces them.
    int m_editingLauncher = -1;
};
