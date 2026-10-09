#include "LaunchersDialog.h"

#include "CompatTools.h"
#include "DialogChrome.h"
#include "PrefixUsage.h"
#include "SteamLibrary.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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

// What a prefixes-table row stands for, and where it is on disk: a row that
// is not a name anyone typed is neither edited nor saved, and only a row with
// a folder behind it has anything to delete.
constexpr int kPrefixKindRole = Qt::UserRole + 2;
constexpr int kPrefixPathRole = Qt::UserRole + 3;
// How many items run in the prefix a row stands for, so that deleting one
// that is still in use can say so.
constexpr int kPrefixItemsRole = Qt::UserRole + 4;

// Which column of the prefixes table holds what.
constexpr int kPrefixName = 0;
constexpr int kPrefixUsed = 1;
constexpr int kPrefixSize = 2;

// How many rows a table on this page shows. Past that it scrolls: a
// launcher with twenty Proton builds should not push everything below it
// off the dialog, and a launcher with one should not leave a hole.
constexpr int kVisibleTableRows = 4;

// Holds a table to that many rows and stops it growing with the dialog.
// The space the dialog gains goes to the bottom instead, so the controls
// stay where they are whatever size the window is.
void showAtMostFourRows(QTableWidget *table) {
    const int header = table->horizontalHeader()->sizeHint().height();
    const int row = table->verticalHeader()->defaultSectionSize();
    table->setFixedHeight(header + kVisibleTableRows * row + 2);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

// The same, for a tree: it has no vertical header to ask for a row height,
// so the height one row wants is asked of the widget itself.
void showAtMostFourRows(QTreeWidget *tree) {
    const int header = tree->header()->sizeHint().height();
    const int row = tree->fontMetrics().height() + 8;
    tree->setFixedHeight(header + kVisibleTableRows * row + 2);
    tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

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
    // Tall enough for the roomiest launcher, so that picking through them
    // never resizes the window: the slack sits at the bottom of the pages
    // that need less.
    resize(760, 620);
}

void LaunchersDialog::setOpenScreen(const QString &name, const QVector<ProgramItem> &items) {
    m_openScreen = name;
    m_openItems = items;
    // Said after the page was built, so what it counted without them is
    // counted again.
    refreshPrefixTable();
}

void LaunchersDialog::buildLauncherPage() {
    m_launcherPage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_launcherPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kDialogRowSpacing);

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
    showAtMostFourRows(m_runners);
    layout->addWidget(m_runners);

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

    // Which prefixes an item can be pointed at. Every program gets one of its
    // own without anything being set up here -- that is the first row, and it
    // is not ours or anyone's to take away. The rest are bottles set up by
    // hand, for programs meant to share one.
    prefixesLayout->addWidget(new QLabel(tr("Prefixes:"), m_prefixesBox));

    // Three columns, because a prefix nobody wants looks exactly like one in
    // daily use until you are told how many items run in it and how much of
    // the disk it is holding.
    // A tree rather than a list: each prefix opens on what actually runs in
    // it, which is the thing worth knowing before taking one away. Closed to
    // begin with, the prefixes themselves being what is being read.
    m_prefixes = new QTreeWidget(m_prefixesBox);
    m_prefixes->setColumnCount(3);
    m_prefixes->setHeaderLabels({tr("Name"), tr("Used by"), tr("Size")});
    m_prefixes->header()->setSectionResizeMode(kPrefixName, QHeaderView::Stretch);
    m_prefixes->header()->setSectionResizeMode(kPrefixUsed, QHeaderView::ResizeToContents);
    m_prefixes->header()->setSectionResizeMode(kPrefixSize, QHeaderView::ResizeToContents);
    m_prefixes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_prefixes->setFrameShape(QFrame::NoFrame);
    m_prefixes->setUniformRowHeights(true);
    showAtMostFourRows(m_prefixes);
    prefixesLayout->addWidget(m_prefixes);

    m_prefixAdd = new QPushButton(tr("Add"), m_prefixesBox);
    m_prefixRemove = new QPushButton(tr("Remove..."), m_prefixesBox);
    m_prefixSizes = new QPushButton(tr("Sizes"), m_prefixesBox);
    for (QPushButton *button : {m_prefixAdd, m_prefixRemove, m_prefixSizes}) {
        button->setAutoDefault(false);
    }
    m_prefixAdd->setToolTip(tr("A bottle of its own, under the folder above, for the items "
                               "pointed at it to share."));
    m_prefixSizes->setToolTip(tr("How much of the disk each prefix is holding. Reading it "
                                 "means walking every one of them, so it is asked for "
                                 "rather than shown from the start."));
    auto *prefixButtons = new QHBoxLayout;
    prefixButtons->addWidget(m_prefixSizes);
    prefixButtons->addStretch();
    prefixButtons->addWidget(m_prefixAdd);
    prefixButtons->addWidget(m_prefixRemove);
    prefixesLayout->addLayout(prefixButtons);

    layout->addWidget(m_prefixesBox);

    connect(m_prefixAdd, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *added =
            addPrefixRow(PrefixRow::Named, QString(), tr("unused"), QString(), QString());
        m_prefixes->setCurrentItem(added);
        added->setSelected(true);
        m_prefixes->editItem(added, kPrefixName);
    });
    connect(m_prefixRemove, &QPushButton::clicked, this, &LaunchersDialog::removePrefix);
    connect(m_prefixes, &QTreeWidget::itemSelectionChanged, this,
            &LaunchersDialog::updatePrefixButtons);
    connect(m_prefixSizes, &QPushButton::clicked, this, &LaunchersDialog::measurePrefixes);
    // A prefix renamed is a different bottle: what is used and what is not
    // has to be counted again, and so has the folder that holds them all.
    connect(m_prefixes, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int column) {
                if (column == kPrefixName && !item->parent()) refreshPrefixTable();
            });
    // Which ones are open is remembered rather than reset: a rebuild happens
    // at every rename and every removal, and shutting them each time would
    // undo whoever just opened one.
    const auto remember = [this](QTreeWidgetItem *item, bool open) {
        if (item->parent()) return;
        const QString name = item->text(kPrefixName);
        m_openPrefixes.removeAll(name);
        if (open) m_openPrefixes << name;
    };
    connect(m_prefixes, &QTreeWidget::itemExpanded, this,
            [remember](QTreeWidgetItem *item) { remember(item, true); });
    connect(m_prefixes, &QTreeWidget::itemCollapsed, this,
            [remember](QTreeWidgetItem *item) { remember(item, false); });
    connect(m_prefixesDir, &QLineEdit::editingFinished, this,
            &LaunchersDialog::refreshPrefixTable);

    connect(prefixesBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Prefixes Folder"),
                                                              m_prefixesDir->text());
        if (dir.isEmpty()) return;
        m_prefixesDir->setText(dir);
        refreshPrefixTable();
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
    showAtMostFourRows(m_libraries);
    librariesLayout->addWidget(m_libraries);


    m_libraryAdd = new QPushButton(tr("Add..."), m_librariesBox);
    m_libraryRemove = new QPushButton(tr("Remove"), m_librariesBox);
    m_libraryAdd->setAutoDefault(false);
    m_libraryRemove->setAutoDefault(false);
    auto *libraryButtons = new QHBoxLayout;
    libraryButtons->addStretch();
    libraryButtons->addWidget(m_libraryAdd);
    libraryButtons->addWidget(m_libraryRemove);
    librariesLayout->addLayout(libraryButtons);
    layout->addWidget(m_librariesBox);

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
    showAtMostFourRows(m_cores);
    coresLayout->addWidget(m_cores);

    auto *addCore = new QPushButton(tr("Add"), m_coresBox);
    auto *removeCore = new QPushButton(tr("Remove"), m_coresBox);
    addCore->setAutoDefault(false);
    removeCore->setAutoDefault(false);
    auto *coreButtons = new QHBoxLayout;
    coreButtons->addStretch();
    coreButtons->addWidget(addCore);
    coreButtons->addWidget(removeCore);
    coresLayout->addLayout(coreButtons);
    layout->addWidget(m_coresBox);

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

QTreeWidgetItem *LaunchersDialog::addPrefixRow(PrefixRow kind, const QString &name,
                                               const QString &used, const QString &path,
                                               const QString &note) {
    auto *row = new QTreeWidgetItem(m_prefixes);
    row->setText(kPrefixName, name);
    row->setText(kPrefixUsed, used);
    row->setData(kPrefixName, kPrefixKindRole, static_cast<int>(kind));
    row->setData(kPrefixName, kPrefixPathRole, path);
    row->setTextAlignment(kPrefixSize, Qt::AlignRight | Qt::AlignVCenter);
    if (!path.isEmpty()) row->setToolTip(kPrefixName, path);
    if (!note.isEmpty()) row->setToolTip(kPrefixUsed, note);

    // Only a name someone typed is a name to type over.
    if (kind == PrefixRow::Named) {
        row->setFlags(row->flags() | Qt::ItemIsEditable);
        return row;
    }

    // Neither the prefix every program gets of its own nor a folder left
    // behind by an item that is gone is a name anyone typed: both are shown
    // so the list reads as the whole of what is in there, and greyed so that
    // neither reads as a row to edit.
    row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    const QBrush grey = m_prefixes->palette().brush(QPalette::Disabled, QPalette::Text);
    for (int column = 0; column < m_prefixes->columnCount(); ++column) {
        row->setForeground(column, grey);
        if (!note.isEmpty()) row->setToolTip(column, note);
    }
    return row;
}

void LaunchersDialog::addPrefixPrograms(QTreeWidgetItem *parent, const QStringList &programs) {
    const int programKind = static_cast<int>(PrefixRow::Program);
    // Under a row that is itself greyed -- a prefix nothing uses any more --
    // what it was built for is greyed with it.
    const bool greyed = parent->data(kPrefixName, kPrefixKindRole).toInt() ==
                        static_cast<int>(PrefixRow::Leftover);

    for (const QString &program : programs) {
        auto *child = new QTreeWidgetItem(parent);
        // The program's own name, the path being too long to read across a
        // column and no more use: it is on the row as a tooltip.
        child->setText(kPrefixName, QFileInfo(program).fileName());
        child->setToolTip(kPrefixName, program);
        child->setData(kPrefixName, kPrefixKindRole, programKind);
        child->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        child->setTextAlignment(kPrefixSize, Qt::AlignRight | Qt::AlignVCenter);
        if (greyed) {
            child->setForeground(kPrefixName,
                                 m_prefixes->palette().brush(QPalette::Disabled,
                                                             QPalette::Text));
        }
    }
}

QStringList LaunchersDialog::typedPrefixes() const {
    QStringList names;
    for (int row = 0; row < m_prefixes->topLevelItemCount(); ++row) {
        const QTreeWidgetItem *item = m_prefixes->topLevelItem(row);
        if (item->data(kPrefixName, kPrefixKindRole).toInt() !=
            static_cast<int>(PrefixRow::Named)) {
            continue;
        }
        names << item->text(kPrefixName);
    }
    return names;
}

void LaunchersDialog::refreshPrefixTable() {
    if (m_editingLauncher < 0 || m_editingLauncher >= m_launchers.size()) return;
    if (!m_launchers[m_editingLauncher].usesPrefixes) return;

    // Whatever has been typed so far is what the table is rebuilt around: a
    // name half entered is still that row's name.
    const QStringList names = typedPrefixes();
    const QTreeWidgetItem *current = m_prefixes->currentItem();
    const QString selected = current && !current->parent() ? current->text(kPrefixName)
                                                           : QString();

    OpenScreen open;
    open.name = m_openScreen;
    open.items = m_openItems;
    const QVector<PrefixUse> found =
        prefixesIn(m_prefixesDir->text().trimmed(), names, open);

    // Rebuilt wholesale, and the edit signal held off while it is: each row
    // put in would otherwise ask for another rebuild.
    const QSignalBlocker blocker(m_prefixes);
    m_prefixes->clear();

    // How many items are running in a prefix of their own, which is what the
    // first row stands for.
    int perApplication = 0;
    for (const PrefixUse &use : found) {
        if (use.kind == PrefixUse::PerApplication) perApplication += use.items();
    }
    QTreeWidgetItem *automatic = addPrefixRow(
        PrefixRow::Fixed, tr("Default (per application)"),
        perApplication > 0 ? tr("%1 items").arg(perApplication) : tr("nothing"), QString(),
        tr("What an item gets when it names no prefix: one of its own, under the "
           "folder above, built the first time it runs."));

    // Under it, the prefixes it stands for: one per program, which is the
    // whole of what the row means. Each is a folder of its own, so each has
    // a size of its own worth knowing -- and one whose program has left the
    // desktop is said to be unused there, where it can be deleted, rather
    // than being listed away from the row it belongs under.
    for (const PrefixUse &use : found) {
        if (use.kind != PrefixUse::PerApplication) continue;
        // The ones that exist. An item whose prefix has never been built
        // leads to a folder that is not there, and a row for a folder that
        // is not there is a row with nothing to say and nothing to do: the
        // count on the row above already has that item in it.
        if (!use.onDisk) continue;

        const bool inUse = use.items() > 0;
        const QString program = inUse ? use.programs.first() : use.program;

        auto *child = new QTreeWidgetItem(automatic);
        child->setText(kPrefixName,
                       program.isEmpty() ? use.name : QFileInfo(program).fileName());
        child->setText(kPrefixUsed, !inUse            ? tr("unused")
                                    : use.items() > 1 ? tr("%1 items").arg(use.items())
                                                      : QString());
        child->setData(kPrefixName, kPrefixPathRole, use.path);
        child->setData(kPrefixName, kPrefixItemsRole, use.items());
        child->setData(kPrefixName, kPrefixKindRole,
                       static_cast<int>(inUse ? PrefixRow::Program : PrefixRow::Leftover));
        child->setToolTip(kPrefixName,
                          (program.isEmpty() ? QString() : program + QLatin1Char('\n')) +
                              use.path);
        child->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        child->setTextAlignment(kPrefixSize, Qt::AlignRight | Qt::AlignVCenter);

        if (inUse) continue;
        // Nothing runs in it: greyed, like any other prefix nothing leads
        // to, and said to be so rather than only looking it.
        const QBrush grey = m_prefixes->palette().brush(QPalette::Disabled, QPalette::Text);
        for (int column = 0; column < m_prefixes->columnCount(); ++column) {
            child->setForeground(column, grey);
        }
        child->setToolTip(kPrefixUsed,
                          program.isEmpty()
                              ? tr("A prefix built for a program that is no longer on any "
                                   "screen. Nothing runs in it.")
                              : tr("Built for %1, which is no longer on any screen. Nothing "
                                   "runs in it.")
                                    .arg(program));
    }

    // The named ones in the order they were given, blank rows and all: a row
    // someone has begun must not vanish under them as they type.
    for (const QString &name : names) {
        const PrefixUse *use = nullptr;
        for (const PrefixUse &candidate : found) {
            if (candidate.kind == PrefixUse::Named && candidate.name == prefixFolderName(name)) {
                use = &candidate;
                break;
            }
        }
        QString used = tr("unused");
        QString note;
        if (use && use->items() > 0) {
            used = tr("%1 items").arg(use->items());
        } else if (use && !use->onDisk) {
            used = tr("not built yet");
            note = tr("Nothing has run in it, so there is nothing on disk: it is built the "
                      "first time an item pointed at it runs.");
        } else {
            note = tr("No item on any screen runs in this prefix. Removing it takes it out "
                      "of the list, and can delete its folder with it.");
        }
        QTreeWidgetItem *row =
            addPrefixRow(PrefixRow::Named, name, used, use ? use->path : QString(), note);
        row->setData(kPrefixName, kPrefixItemsRole, use ? use->items() : 0);
        // What is actually running in it, which is what makes a shared
        // prefix worth having and what says whether it can go.
        if (use) addPrefixPrograms(row, use->programs);
    }

    // What is left in the folder: a prefix built for a program that is no
    // longer on any screen, and anything in there that was never ours. These
    // are the ones worth clearing out, and the only ones nothing points at.
    // What is left at the top is what belongs to neither: a folder in the
    // prefixes directory that this application did not make. The prefixes it
    // did make are all under one row or another by now.
    for (const PrefixUse &use : found) {
        if (use.kind != PrefixUse::Foreign) continue;
        addPrefixRow(PrefixRow::Leftover, use.name, tr("not ours"), use.path,
                     tr("A folder in the prefixes directory that this application did not "
                        "make. Left alone."));
    }

    // Back to the row that was selected, and open again at whatever was
    // open: the rebuild has put a whole new tree in its place.
    for (int row = 0; row < m_prefixes->topLevelItemCount(); ++row) {
        QTreeWidgetItem *item = m_prefixes->topLevelItem(row);
        const QString name = item->text(kPrefixName);
        if (m_openPrefixes.contains(name)) item->setExpanded(true);
        if (!selected.isEmpty() && name == selected) {
            m_prefixes->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    updatePrefixButtons();
}

void LaunchersDialog::updatePrefixButtons() {
    // What is picked out rather than what the keyboard was last on: clicking
    // the empty space under the rows drops the selection, and the buttons
    // follow that.
    const QList<QTreeWidgetItem *> picked = m_prefixes->selectedItems();
    const QTreeWidgetItem *row = picked.isEmpty() ? nullptr : picked.first();
    const QTreeWidgetItem *parent = row ? row->parent() : nullptr;

    // Add makes a new prefix at the end of the list. The prefixes are the
    // rows at the top, so that is where adding one belongs; the programs
    // hanging under them are not a list to add to.
    m_prefixAdd->setEnabled(parent == nullptr);

    // Remove takes away a prefix -- one set up by hand, or one of the
    // automatic ones under the default row, each of which is a folder of its
    // own. Neither a program nor the default row itself is a prefix to take
    // away, and a folder this application did not make is left alone.
    const auto kindOf = [](const QTreeWidgetItem *item) {
        return item ? static_cast<PrefixRow>(item->data(kPrefixName, kPrefixKindRole).toInt())
                    : PrefixRow::Program;
    };
    const bool namedPrefix = row && !parent && kindOf(row) == PrefixRow::Named;
    // An automatic prefix is a folder and nothing else -- there is no list
    // it is in -- so when the folder is not there yet there is nothing to
    // remove, and the button says so by being off rather than by doing
    // nothing when it is pressed.
    const bool automaticPrefix =
        parent && kindOf(parent) == PrefixRow::Fixed &&
        QFileInfo::exists(row->data(kPrefixName, kPrefixPathRole).toString());
    m_prefixRemove->setEnabled(namedPrefix || automaticPrefix);
}

void LaunchersDialog::measurePrefixes() {
    // Every prefix is a Windows installation, so this is a walk of some
    // thousands of files: said so, and the button held, while it runs.
    m_prefixSizes->setEnabled(false);
    m_prefixSizes->setText(tr("Reading..."));
    QCoreApplication::processEvents();

    // Every row that has a folder behind it, the per-application ones under
    // the first row included: those are where the disk actually goes.
    QTreeWidgetItemIterator rows(m_prefixes);
    for (; *rows; ++rows) {
        QTreeWidgetItem *row = *rows;
        const QString path = row->data(kPrefixName, kPrefixPathRole).toString();
        if (path.isEmpty() || !QFileInfo::exists(path)) continue;
        row->setText(kPrefixSize, QLocale().formattedDataSize(folderSize(path)));
        QCoreApplication::processEvents();
    }

    m_prefixSizes->setText(tr("Sizes"));
    m_prefixSizes->setEnabled(true);
}

void LaunchersDialog::removePrefix() {
    const QList<QTreeWidgetItem *> picked = m_prefixes->selectedItems();
    if (picked.isEmpty()) return;
    QTreeWidgetItem *row = picked.first();
    QTreeWidgetItem *parent = row->parent();

    const auto kindOf = [](const QTreeWidgetItem *item) {
        return item ? static_cast<PrefixRow>(item->data(kPrefixName, kPrefixKindRole).toInt())
                    : PrefixRow::Program;
    };
    // The two kinds of row that stand for a prefix of their own: one set up
    // by hand, and one of the automatic ones under the default row.
    const bool named = !parent && kindOf(row) == PrefixRow::Named;
    const bool automatic = parent && kindOf(parent) == PrefixRow::Fixed;
    if (!named && !automatic) return;

    const QString name = row->text(kPrefixName).trimmed();
    const QString path = row->data(kPrefixName, kPrefixPathRole).toString();
    const bool onDisk = !path.isEmpty() && QFileInfo::exists(path);

    // What is in there, so that nobody deletes several gigabytes of saved
    // games on the strength of a folder name.
    QString what = path;
    if (onDisk) {
        what = tr("%1\n\n%2").arg(path, QLocale().formattedDataSize(folderSize(path)));
    }

    if (!onDisk) {
        // Nothing built yet. A named prefix is still a row to take out of
        // the list; an automatic one is only ever a folder, so there is
        // nothing to do at all.
        if (named) {
            delete row;
            refreshPrefixTable();
        }
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Remove Prefix"));
    box.setText(named ? tr("Remove the prefix '%1'?").arg(name)
                      : tr("Delete the prefix '%1'?").arg(name));
    // A prefix is a C: drive: its saved games are in there and nowhere else.
    QString warning = tr("A prefix holds the programs' settings and saved games, and "
                         "deleting its folder cannot be undone.");
    // And one still in use is one a program on the desktop will build again
    // from nothing the next time it is started.
    const int items = row->data(kPrefixName, kPrefixItemsRole).toInt();
    if (items > 0) {
        warning += QLatin1Char(' ') +
                   tr("%n item(s) still run in this one, and would start afresh.", nullptr,
                      items);
    }
    box.setInformativeText(warning + QStringLiteral("\n\n") + what);
    QPushButton *deleteFolder = box.addButton(tr("Delete Folder"), QMessageBox::DestructiveRole);
    QPushButton *listOnly = named
                                ? box.addButton(tr("Remove from List"), QMessageBox::AcceptRole)
                                : nullptr;
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == deleteFolder) {
        if (!QDir(path).removeRecursively()) {
            QMessageBox::warning(this, tr("Remove Prefix"),
                                 tr("Could not delete '%1'.").arg(path));
            return;
        }
        if (named) delete row;
        refreshPrefixTable();
        return;
    }
    if (listOnly && box.clickedButton() == listOnly) {
        // Out of the list, and the folder left where it is: it becomes one of
        // the leftovers, to be dealt with or not.
        delete row;
        refreshPrefixTable();
    }
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
        if (launcher.usesPrefixes) {
            launcher.prefixesDir = m_prefixesDir->text().trimmed();
            // Only the named ones are kept: the first row stands for the
            // prefix an item gets when it names none, which is not a name.
            QStringList prefixes;
            for (const QString &typed : typedPrefixes()) {
                const QString name = prefixFolderName(typed);
                // A row someone began and left blank is not a prefix yet, and
                // one spelled the same as another is the same bottle.
                if (name.isEmpty() || prefixes.contains(name)) continue;
                prefixes << name;
            }
            launcher.prefixes = prefixes;
        }

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

    m_extensions->setText(joinExtensions(launcher.extensions));

    m_runnersTitle->setText(tr("Available %1:").arg(launcher.name));
    m_runners->setRowCount(0);
    for (const LauncherRunner &runner : launcher.runners) addRunnerRow(runner);

    // Whether the list is the machine's or ours: a machine's own list is
    // not something to add to or take from.
    int discovered = 0;
    for (const LauncherRunner &runner : launcher.runners) discovered += runner.discovered;

    // A machine's own list is not ours to add to or take from.
    m_runnerAdd->setEnabled(discovered == 0);
    m_runnerRemove->setEnabled(discovered == 0);
    m_runnerBrowse->setEnabled(discovered == 0);

    m_prefixesBox->setVisible(launcher.usesPrefixes);
    if (launcher.usesPrefixes) {
        m_prefixesDir->setText(launcher.prefixesDir);
        {
            const QSignalBlocker blocker(m_prefixes);
            m_prefixes->clear();
            for (const QString &name : launcher.prefixes) {
                addPrefixRow(PrefixRow::Named, name, QString(), QString(), QString());
            }
        }
        refreshPrefixTable();
    }

    // Everything that does not belong to this launcher goes first. Showing
    // one before hiding another leaves both in the layout for a pass, and
    // the dialog grows to hold the pair -- then stays grown, because a
    // window is not shrunk back when its contents need less.
    if (!launcher.usesSteamLibraries) m_librariesBox->hide();
    if (!launcher.usesCores) m_coresBox->hide();
    if (!launcher.usesPrefixes) m_prefixesBox->hide();

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
