#include "LaunchersDialog.h"

#include "DialogChrome.h"
#include "SteamLibrary.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Wide enough for the tree to sit beside a form rather than above it.
constexpr int kTreeWidth = 190;
// Rows with some air in them: there are only a handful, and they read as
// tabs down the side rather than as a list to be scrolled.
constexpr int kTreeRowHeight = 28;

// Which column of the runners table holds what. No arguments among them:
// how a launcher is handed its file is the launcher's business.
constexpr int kRunnerName = 0;
constexpr int kRunnerCommand = 1;
constexpr int kRunnerShell = 2;

// Set on a runners-table row that was read off the machine rather than set
// up here, so that it is shown but not edited.
constexpr int kDiscoveredRole = Qt::UserRole + 1;

QString joinExtensions(const QStringList &extensions) {
    return extensions.join(QLatin1Char(' '));
}

QStringList splitExtensions(const QString &text) {
    QStringList out;
    // Written however it comes naturally -- spaces, commas, dots and all --
    // and kept as bare lower-case extensions.
    for (const QString &piece :
         text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        QString extension = piece.trimmed().toLower();
        while (extension.startsWith(QLatin1Char('.'))) extension.remove(0, 1);
        if (!extension.isEmpty() && !out.contains(extension)) out << extension;
    }
    return out;
}

void fillCoreTable(QTableWidget *table, const QVector<QPair<QString, QString>> &cores) {
    table->setRowCount(cores.size());
    for (int row = 0; row < cores.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(cores[row].first));
        table->setItem(row, 1, new QTableWidgetItem(cores[row].second));
    }
}

// An id no other runner of this launcher is using. A runner keeps the id it
// was given for as long as it lives, renaming and all: it is what the items
// pointing at it store.
QString uniqueRunnerId(const QStringList &taken, const QString &name) {
    static const QRegularExpression unsafe(QStringLiteral("[^a-z0-9]+"));
    QString base = QFileInfo(name).fileName().toLower().replace(unsafe, QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-'))) base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-'))) base.chop(1);
    if (base.isEmpty()) base = QStringLiteral("runner");

    QString candidate = base;
    for (int n = 2; taken.contains(candidate); ++n) {
        candidate = base + QLatin1Char('-') + QString::number(n);
    }
    return candidate;
}

} // namespace

LaunchersDialog::LaunchersDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Launchers"));

    const DialogChrome chrome = applyDialogChrome(this, tr("Launchers"));

    m_launchers = ::launchers();

    m_tree = new QTreeWidget(this);
    m_tree->setFixedWidth(kTreeWidth);
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame); // the style sheet draws the edge
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // A flat list: a launcher has nothing under it any more.
    m_tree->setRootIsDecorated(false);

    buildLauncherPage();

    auto *columns = new QHBoxLayout;
    columns->setSpacing(kDialogMargin);
    columns->addWidget(m_tree);
    columns->addWidget(m_launcherPage, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(ok);
        ok->setDefault(true);
    }

    chrome.fields->addLayout(columns, 1);
    chrome.footer->addWidget(buttons);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                commitCurrentPage();
                showPageFor(current);
            });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        commitCurrentPage();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    buildTree(0);
    resize(760, 520);
}

void LaunchersDialog::buildLauncherPage() {
    m_launcherPage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_launcherPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kDialogRowSpacing);

    m_launcherName = new QLabel(m_launcherPage);
    QFont heading = m_launcherName->font();
    heading.setBold(true);
    m_launcherName->setFont(heading);
    layout->addWidget(m_launcherName);

    m_launcherDescription = new QLabel(m_launcherPage);
    markHintLabel(m_launcherDescription);
    m_launcherDescription->setWordWrap(true);
    layout->addWidget(m_launcherDescription);

    m_extensions = new QLineEdit(m_launcherPage);
    m_extensions->setPlaceholderText(tr("conf  iso  nes"));
    m_extensions->setToolTip(tr("More than one launcher may claim the same type: an item then "
                                "picks between them in its properties."));

    m_launcherForm = new QFormLayout;
    m_launcherForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_launcherForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_launcherForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_launcherForm->setHorizontalSpacing(kDialogLabelGap);
    m_launcherForm->setVerticalSpacing(kDialogRowSpacing);
    m_launcherForm->addRow(tr("File types:"), m_extensions);
    layout->addLayout(m_launcherForm);

    // The programs that can act as this launcher. For wine and Proton the
    // list is read off the machine; for the rest it is where a full path to
    // an install outside the PATH goes.
    m_runnersTitle = new QLabel(m_launcherPage);
    layout->addWidget(m_runnersTitle);

    m_runners = new QTableWidget(0, 3, m_launcherPage);
    m_runners->setHorizontalHeaderLabels({tr("Name"), tr("Program"), tr("Shell")});
    m_runners->horizontalHeader()->setSectionResizeMode(kRunnerName,
                                                        QHeaderView::ResizeToContents);
    m_runners->horizontalHeader()->setSectionResizeMode(kRunnerCommand, QHeaderView::Stretch);
    m_runners->horizontalHeader()->setSectionResizeMode(kRunnerShell,
                                                        QHeaderView::ResizeToContents);
    m_runners->verticalHeader()->setVisible(false);
    m_runners->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_runners->setFrameShape(QFrame::NoFrame);
    m_runners->setMinimumHeight(130);
    layout->addWidget(m_runners, 1);

    m_runnersNote = new QLabel(m_launcherPage);
    markHintLabel(m_runnersNote);
    m_runnersNote->setWordWrap(true);
    layout->addWidget(m_runnersNote);

    m_runnerAdd = new QPushButton(tr("Add"), m_launcherPage);
    m_runnerRemove = new QPushButton(tr("Remove"), m_launcherPage);
    m_runnerBrowse = new QPushButton(tr("Browse..."), m_launcherPage);
    for (QPushButton *button : {m_runnerAdd, m_runnerRemove, m_runnerBrowse}) {
        button->setAutoDefault(false);
    }
    m_runnerBrowse->setToolTip(tr("Pick the program for the row selected."));
    auto *runnerButtons = new QHBoxLayout;
    runnerButtons->addWidget(m_runnerBrowse);
    runnerButtons->addStretch();
    runnerButtons->addWidget(m_runnerAdd);
    runnerButtons->addWidget(m_runnerRemove);
    layout->addLayout(runnerButtons);

    connect(m_runnerAdd, &QPushButton::clicked, this, [this] {
        addRunnerRow(LauncherRunner{});
        const int row = m_runners->rowCount() - 1;
        m_runners->setCurrentCell(row, kRunnerName);
        m_runners->editItem(m_runners->item(row, kRunnerName));
    });
    connect(m_runnerRemove, &QPushButton::clicked, this, [this] {
        const int row = m_runners->currentRow();
        if (row < 0) return;
        // A wine or a Proton build goes when the machine stops carrying it,
        // not from here; and a launcher with nothing of its own left could
        // start nothing at all, so the last one stays.
        if (runnerRowIsDiscovered(row)) return;
        if (editableRunnerRows() > 1) m_runners->removeRow(row);
    });
    connect(m_runnerBrowse, &QPushButton::clicked, this, &LaunchersDialog::browseForRunner);

    // Where the wine prefixes its programs build are kept: one to a program,
    // holding its C: drive, its registry and its saved games.
    m_prefixesBox = new QWidget(m_launcherPage);
    auto *prefixesLayout = new QVBoxLayout(m_prefixesBox);
    prefixesLayout->setContentsMargins(0, 0, 0, 0);
    prefixesLayout->setSpacing(kDialogRowSpacing);

    m_prefixesDir = new QLineEdit(m_prefixesBox);
    m_prefixesDir->setPlaceholderText(tr("Kept with this application's own files when empty"));
    auto *prefixesBrowse = new QPushButton(tr("Browse..."), m_prefixesBox);
    prefixesBrowse->setAutoDefault(false);
    auto *prefixesRow = new QHBoxLayout;
    prefixesRow->setSpacing(8);
    prefixesRow->addWidget(new QLabel(tr("Prefixes folder:"), m_prefixesBox));
    prefixesRow->addWidget(m_prefixesDir, 1);
    prefixesRow->addWidget(prefixesBrowse);
    prefixesLayout->addLayout(prefixesRow);

    auto *prefixesNote =
        new QLabel(tr("Each program gets a wine prefix of its own under here, built the "
                      "first time it runs."),
                   m_prefixesBox);
    markHintLabel(prefixesNote);
    prefixesNote->setWordWrap(true);
    prefixesLayout->addWidget(prefixesNote);
    layout->addWidget(m_prefixesBox);

    connect(prefixesBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Prefixes Folder"),
                                                              m_prefixesDir->text());
        if (!dir.isEmpty()) m_prefixesDir->setText(dir);
    });

    // Where Steam keeps its games. Steam lists its own libraries in a file
    // of its own, so those are read rather than asked for; a folder it does
    // not list -- a disk it has not been told about -- is added here.
    m_librariesBox = new QWidget(m_launcherPage);
    auto *librariesLayout = new QVBoxLayout(m_librariesBox);
    librariesLayout->setContentsMargins(0, 0, 0, 0);
    librariesLayout->setSpacing(kDialogRowSpacing);
    librariesLayout->addWidget(new QLabel(tr("Library folders:"), m_librariesBox));

    m_libraries = new QTableWidget(0, 2, m_librariesBox);
    m_libraries->setHorizontalHeaderLabels({tr("Folder"), tr("Games")});
    m_libraries->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_libraries->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_libraries->verticalHeader()->setVisible(false);
    m_libraries->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_libraries->setFrameShape(QFrame::NoFrame);
    m_libraries->setMinimumHeight(110);
    librariesLayout->addWidget(m_libraries, 1);

    auto *librariesNote = new QLabel(
        tr("The greyed rows are the libraries Steam lists itself, which come and go with "
           "what Steam knows about. Add a folder here only for one it does not list. "
           "Insert > Steam Games lists everything found across them."),
        m_librariesBox);
    markHintLabel(librariesNote);
    librariesNote->setWordWrap(true);
    librariesLayout->addWidget(librariesNote);

    m_libraryAdd = new QPushButton(tr("Add..."), m_librariesBox);
    m_libraryRemove = new QPushButton(tr("Remove"), m_librariesBox);
    m_libraryAdd->setAutoDefault(false);
    m_libraryRemove->setAutoDefault(false);
    auto *libraryButtons = new QHBoxLayout;
    libraryButtons->addStretch();
    libraryButtons->addWidget(m_libraryAdd);
    libraryButtons->addWidget(m_libraryRemove);
    librariesLayout->addLayout(libraryButtons);
    layout->addWidget(m_librariesBox, 1);

    connect(m_libraryAdd, &QPushButton::clicked, this, &LaunchersDialog::browseForLibrary);
    connect(m_libraryRemove, &QPushButton::clicked, this, [this] {
        const int row = m_libraries->currentRow();
        if (row < 0) return;
        const QTableWidgetItem *cell = m_libraries->item(row, 0);
        // Steam's own list is not ours to take from.
        if (cell && cell->data(kDiscoveredRole).toBool()) return;
        m_libraries->removeRow(row);
    });

    // The cores an emulator plays each file type with, shared by every
    // profile that has not pinned one of its own.
    m_coresBox = new QWidget(m_launcherPage);
    auto *coresLayout = new QVBoxLayout(m_coresBox);
    coresLayout->setContentsMargins(0, 0, 0, 0);
    coresLayout->setSpacing(kDialogRowSpacing);

    m_coresDir = new QLineEdit(m_coresBox);
    m_coresDir->setPlaceholderText(tr("Found beside the program when left empty"));
    auto *coresBrowse = new QPushButton(tr("Browse..."), m_coresBox);
    coresBrowse->setAutoDefault(false);
    auto *coresDirRow = new QHBoxLayout;
    coresDirRow->setSpacing(8);
    coresDirRow->addWidget(new QLabel(tr("Cores folder:"), m_coresBox));
    coresDirRow->addWidget(m_coresDir, 1);
    coresDirRow->addWidget(coresBrowse);
    coresLayout->addLayout(coresDirRow);

    m_cores = new QTableWidget(0, 2, m_coresBox);
    m_cores->setHorizontalHeaderLabels({tr("File type"), tr("Core")});
    m_cores->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_cores->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_cores->verticalHeader()->setVisible(false);
    m_cores->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_cores->setFrameShape(QFrame::NoFrame);
    m_cores->setMinimumHeight(110);
    coresLayout->addWidget(m_cores, 1);

    auto *addCore = new QPushButton(tr("Add"), m_coresBox);
    auto *removeCore = new QPushButton(tr("Remove"), m_coresBox);
    addCore->setAutoDefault(false);
    removeCore->setAutoDefault(false);
    auto *coreButtons = new QHBoxLayout;
    coreButtons->addStretch();
    coreButtons->addWidget(addCore);
    coreButtons->addWidget(removeCore);
    coresLayout->addLayout(coreButtons);
    layout->addWidget(m_coresBox, 1);

    connect(addCore, &QPushButton::clicked, this, [this] {
        const int row = m_cores->rowCount();
        m_cores->insertRow(row);
        m_cores->setItem(row, 0, new QTableWidgetItem);
        m_cores->setItem(row, 1, new QTableWidgetItem);
        m_cores->editItem(m_cores->item(row, 0));
    });
    connect(removeCore, &QPushButton::clicked, this, [this] {
        const int row = m_cores->currentRow();
        if (row >= 0) m_cores->removeRow(row);
    });
    connect(coresBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Cores Folder"),
                                                              m_coresDir->text());
        if (!dir.isEmpty()) m_coresDir->setText(dir);
    });

    layout->addStretch();
}

void LaunchersDialog::addRunnerRow(const LauncherRunner &runner) {
    const int row = m_runners->rowCount();
    m_runners->insertRow(row);

    auto *name = new QTableWidgetItem(runner.name);
    // The id travels with the row rather than being shown: it is what the
    // items have stored, so it has to survive a rename.
    name->setData(Qt::UserRole, runner.id);
    name->setData(kDiscoveredRole, runner.discovered);
    m_runners->setItem(row, kRunnerName, name);
    m_runners->setItem(row, kRunnerCommand, new QTableWidgetItem(runner.command));

    // Not typed but ticked: whether the program is an alias needing a shell.
    auto *shell = new QTableWidgetItem;
    shell->setFlags((shell->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    shell->setCheckState(runner.useShell ? Qt::Checked : Qt::Unchecked);
    shell->setToolTip(tr("Start it from an interactive shell, for an alias or a shell "
                         "function rather than a program."));
    m_runners->setItem(row, kRunnerShell, shell);

    if (!runner.discovered) return;

    // Read off the machine, so there is nothing here to change: the row is
    // shown so that an item can be pointed at it, and greyed so that it is
    // plainly not this dialog's to edit.
    const QString note = tr("Found on this machine, so it comes and goes with what is "
                            "installed rather than being set here.");
    for (int column = 0; column < m_runners->columnCount(); ++column) {
        QTableWidgetItem *cell = m_runners->item(row, column);
        cell->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        cell->setForeground(m_runners->palette().brush(QPalette::Disabled, QPalette::Text));
        cell->setToolTip(note);
    }
}

bool LaunchersDialog::runnerRowIsDiscovered(int row) const {
    const QTableWidgetItem *name = m_runners->item(row, kRunnerName);
    return name && name->data(kDiscoveredRole).toBool();
}

int LaunchersDialog::editableRunnerRows() const {
    int rows = 0;
    for (int row = 0; row < m_runners->rowCount(); ++row) {
        if (!runnerRowIsDiscovered(row)) ++rows;
    }
    return rows;
}

void LaunchersDialog::browseForRunner() {
    const int row = m_runners->currentRow();
    if (row < 0 || runnerRowIsDiscovered(row)) return;
    QTableWidgetItem *command = m_runners->item(row, kRunnerCommand);
    QTableWidgetItem *name = m_runners->item(row, kRunnerName);
    if (!command || !name) return;

    const QString picked = QFileDialog::getOpenFileName(this, tr("Program"), command->text(),
                                                        tr("All Files (*)"));
    if (picked.isEmpty()) return;
    command->setText(picked);
    // A program picked by hand is a path, so no shell is wanted for it, and
    // a row still unnamed takes the program's own name.
    if (QTableWidgetItem *shell = m_runners->item(row, kRunnerShell)) {
        shell->setCheckState(Qt::Unchecked);
    }
    if (name->text().trimmed().isEmpty()) name->setText(QFileInfo(picked).fileName());
}

void LaunchersDialog::addLibraryRow(const QString &path, bool discovered) {
    const int row = m_libraries->rowCount();
    m_libraries->insertRow(row);

    auto *folder = new QTableWidgetItem(path);
    folder->setData(kDiscoveredRole, discovered);
    folder->setFlags(folder->flags() & ~Qt::ItemIsEditable);
    m_libraries->setItem(row, 0, folder);

    // What is actually in it, which is the thing worth knowing about a
    // folder someone has just added.
    const int found = steamGamesIn({path}).size();
    auto *count = new QTableWidgetItem(QString::number(found));
    count->setFlags(count->flags() & ~Qt::ItemIsEditable);
    m_libraries->setItem(row, 1, count);

    if (!discovered) return;
    const QString note = tr("Listed by Steam itself, so it comes and goes with what Steam "
                            "knows about rather than being set here.");
    for (int column = 0; column < m_libraries->columnCount(); ++column) {
        QTableWidgetItem *cell = m_libraries->item(row, column);
        cell->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        cell->setForeground(m_libraries->palette().brush(QPalette::Disabled, QPalette::Text));
        cell->setToolTip(note);
    }
}

void LaunchersDialog::browseForLibrary() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Steam Library Folder"));
    if (dir.isEmpty()) return;

    for (int row = 0; row < m_libraries->rowCount(); ++row) {
        if (m_libraries->item(row, 0)->text() == dir) return; // already listed
    }
    addLibraryRow(dir, false);
}

void LaunchersDialog::buildTree(int selectLauncher) {
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    QTreeWidgetItem *toSelect = nullptr;
    for (int r = 0; r < m_launchers.size(); ++r) {
        auto *row = new QTreeWidgetItem(m_tree, {m_launchers[r].name});
        row->setData(0, LauncherRole, r);
        row->setSizeHint(0, QSize(0, kTreeRowHeight));
        if (r == selectLauncher) toSelect = row;
    }

    if (!toSelect && m_tree->topLevelItemCount() > 0) toSelect = m_tree->topLevelItem(0);
    m_editingLauncher = -1;
    m_tree->setCurrentItem(toSelect);
    showPageFor(toSelect);
}

void LaunchersDialog::commitCurrentPage() {
    if (m_editingLauncher < 0 || m_editingLauncher >= m_launchers.size()) return;
    Launcher &launcher = m_launchers[m_editingLauncher];

    {
        launcher.extensions = splitExtensions(m_extensions->text());
        if (launcher.usesPrefixes) launcher.prefixesDir = m_prefixesDir->text().trimmed();

        QVector<LauncherRunner> runners;
        QStringList taken;
        for (const LauncherRunner &existing : launcher.runners) taken << existing.id;
        for (int row = 0; row < m_runners->rowCount(); ++row) {
            QTableWidgetItem *nameCell = m_runners->item(row, kRunnerName);
            const QTableWidgetItem *commandCell = m_runners->item(row, kRunnerCommand);
            const QTableWidgetItem *shellCell = m_runners->item(row, kRunnerShell);
            if (!nameCell || !commandCell) continue;

            LauncherRunner runner;
            runner.command = commandCell->text().trimmed();
            // A row with no program names nothing that could be started, so
            // it is not a runner yet: it is a row someone has begun.
            if (runner.command.isEmpty()) continue;
            runner.name = nameCell->text().trimmed();
            if (runner.name.isEmpty()) runner.name = runner.command;
            runner.id = nameCell->data(Qt::UserRole).toString();

            // What was read off the machine stays what it was: its id, and
            // the fact that it is not ours to save.
            runner.discovered = nameCell->data(kDiscoveredRole).toBool();
            if (runner.discovered) {
                if (const LauncherRunner *was = launcher.runner(runner.id)) runner = *was;
            } else {
                runner.useShell = shellCell && shellCell->checkState() == Qt::Checked;
                if (runner.id.isEmpty()) {
                    runner.id = uniqueRunnerId(taken, runner.name);
                    taken << runner.id;
                    // Back onto the row, so that leaving the page and coming
                    // back does not mint a second id for the same runner.
                    nameCell->setData(Qt::UserRole, runner.id);
                }
            }
            runners.append(runner);
        }
        // Emptied out entirely -- every row cleared rather than removed --
        // leaves the launcher what it had: it has to keep something to start
        // things with, whether its own or the machine's.
        if (!runners.isEmpty()) launcher.runners = runners;

        if (launcher.usesSteamLibraries) {
            // Only the ones added by hand are kept: Steam's own are read
            // afresh each time, and saving them would freeze a list that
            // should follow what Steam knows.
            QStringList folders;
            for (int row = 0; row < m_libraries->rowCount(); ++row) {
                const QTableWidgetItem *cell = m_libraries->item(row, 0);
                if (!cell || cell->data(kDiscoveredRole).toBool()) continue;
                if (!cell->text().isEmpty()) folders << cell->text();
            }
            launcher.libraryFolders = folders;
        }

        if (!launcher.usesCores) return;
        launcher.coresDir = m_coresDir->text().trimmed();
        QVector<QPair<QString, QString>> cores;
        for (int row = 0; row < m_cores->rowCount(); ++row) {
            const QTableWidgetItem *extension = m_cores->item(row, 0);
            const QTableWidgetItem *core = m_cores->item(row, 1);
            if (!extension || !core) continue;
            const QString type = extension->text().trimmed().toLower();
            if (type.isEmpty() || core->text().trimmed().isEmpty()) continue;
            cores.append({type, core->text().trimmed()});
        }
        launcher.cores = cores;
    }
}

void LaunchersDialog::showPageFor(QTreeWidgetItem *item) {
    if (!item) return;
    const int r = item->data(0, LauncherRole).toInt();
    if (r < 0 || r >= m_launchers.size()) return;
    const Launcher &launcher = m_launchers[r];

    m_editingLauncher = r;

    m_launcherName->setText(launcher.name);
    m_launcherDescription->setText(launcher.description);
    m_extensions->setText(joinExtensions(launcher.extensions));

    m_runnersTitle->setText(tr("Available %1:").arg(launcher.name));
    m_runners->setRowCount(0);
    for (const LauncherRunner &runner : launcher.runners) addRunnerRow(runner);

    // Where the list comes from, which differs: wine and Proton are read
    // off the machine, the rest are where a full path is given.
    int discovered = 0;
    for (const LauncherRunner &runner : launcher.runners) discovered += runner.discovered;
    if (discovered > 0) {
        m_runnersNote->setText(
            tr("Every %1 found on this machine is listed. An item picks one of them in "
               "its properties; the greyed rows come and go with what is installed.")
                .arg(launcher.name));
    } else {
        m_runnersNote->setText(
            tr("One row to each %1 installed, so an item can be pointed at the one it "
               "wants. Browse to a program outside the PATH, or tick Shell for a shell "
               "alias.")
                .arg(launcher.name));
    }
    // A machine's own list is not ours to add to or take from.
    m_runnerAdd->setEnabled(discovered == 0);
    m_runnerRemove->setEnabled(discovered == 0);
    m_runnerBrowse->setEnabled(discovered == 0);

    m_prefixesBox->setVisible(launcher.usesPrefixes);
    if (launcher.usesPrefixes) m_prefixesDir->setText(launcher.prefixesDir);

    m_librariesBox->setVisible(launcher.usesSteamLibraries);
    if (launcher.usesSteamLibraries) {
        m_libraries->setRowCount(0);
        // Steam's own first, then whatever has been added by hand.
        const QStringList discovered = discoveredSteamLibraries();
        for (const QString &path : discovered) addLibraryRow(path, true);
        for (const QString &path : launcher.libraryFolders) {
            if (discovered.contains(path)) continue;
            addLibraryRow(path, false);
        }
    }

    m_coresBox->setVisible(launcher.usesCores);
    if (launcher.usesCores) {
        m_coresDir->setText(launcher.coresDir);
        fillCoreTable(m_cores, launcher.cores);
    }
}
