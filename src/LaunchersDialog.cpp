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

// Wide enough for the tree to sit beside the form rather than above it.
constexpr int kTreeWidth = 190;
// Roomy rows: there are only a handful, and they read as tabs down the side
// rather than as a scrollable list.
constexpr int kTreeRowHeight = 28;

// The columns of the runners table. No arguments column: how a launcher is
// handed its file is the launcher's business.
constexpr int kRunnerName = 0;
constexpr int kRunnerCommand = 1;
constexpr int kRunnerShell = 2;

// Set on a runners-table row that was detected rather than configured here,
// so it is shown but not editable.
constexpr int kDiscoveredRole = Qt::UserRole + 1;

// What a prefixes-table row stands for, and where it is on disk. A row that is
// not a typed name is neither editable nor saved, and only a row with a folder
// behind it has anything to delete.
constexpr int kPrefixKindRole = Qt::UserRole + 2;
constexpr int kPrefixPathRole = Qt::UserRole + 3;
// How many items run in the prefix a row stands for, so deleting one still in
// use can warn about it.
constexpr int kPrefixItemsRole = Qt::UserRole + 4;

// The columns of the prefixes table.
constexpr int kPrefixName = 0;
constexpr int kPrefixUsed = 1;
constexpr int kPrefixSize = 2;

// How many rows a table on this page shows before it scrolls. A launcher with
// twenty Proton builds must not push everything below it off the dialog, and
// one with a single runner must not leave a gap.
constexpr int kVisibleTableRows = 4;

// Fixes a table to that many rows and stops it growing with the dialog. Extra
// space goes to the bottom instead, so the controls stay put at any window
// size.
void showAtMostFourRows(QTableWidget *table) {
    const int header = table->horizontalHeader()->sizeHint().height();
    const int row = table->verticalHeader()->defaultSectionSize();
    table->setFixedHeight(header + kVisibleTableRows * row + 2);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

// The same for a tree, which has no vertical header to give a row height, so
// it is derived from the widget's font metrics.
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
    // Accepts spaces, commas and leading dots, and stores bare lower-case
    // extensions.
    for (const QString &piece :
         text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        QString extension = piece.trimmed().toLower();
        while (extension.startsWith(QLatin1Char('.'))) extension.remove(0, 1);
        if (!extension.isEmpty() && !out.contains(extension)) out << extension;
    }
    return out;
}

// An id no other runner of this launcher uses. A runner keeps its id for life,
// renames included, since that is what the items pointing at it store.
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
    // A flat list: a launcher no longer has children.
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
    // Tall enough for the largest launcher page, so selecting another one never
    // resizes the window; smaller pages leave space at the bottom.
    resize(760, 620);
}

void LaunchersDialog::setOpenScreen(const QString &name, const QVector<ProgramItem> &items) {
    m_openScreen = name;
    m_openItems = items;
    // Called after the page was built, so the count made without these items is
    // redone.
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

    // The programs that can act as this launcher. For wine and Proton the list
    // is detected; for the others it is where a full path to an install
    // outside the PATH is entered.
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
        // A detected wine or Proton build disappears when it is uninstalled,
        // not from here; and a launcher with no runner at all could start
        // nothing, so the last editable row stays.
        if (runnerRowIsDiscovered(row)) return;
        if (editableRunnerRows() > 1) m_runners->removeRow(row);
    });
    connect(m_runnerBrowse, &QPushButton::clicked, this, &LaunchersDialog::browseForRunner);

    // Where the wine prefixes its programs create are kept: one per program,
    // holding its C: drive, registry and saved games.
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
    // own with no configuration, which is the first row and cannot be removed.
    // The rest are prefixes created by hand, for programs meant to share one.
    prefixesLayout->addWidget(new QLabel(tr("Prefixes:"), m_prefixesBox));

    // Three columns, since an unused prefix looks like one in daily use until
    // the item count and the disk usage are shown. A tree rather than a list,
    // so each prefix expands to what runs in it, which is what matters before
    // deleting one; collapsed initially.
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
    // A renamed prefix is a different folder, so the usage counts and the
    // folder listing have to be recomputed.
    connect(m_prefixes, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int column) {
                if (column == kPrefixName && !item->parent()) refreshPrefixTable();
            });
    // The expanded rows are remembered rather than reset: a rebuild happens on
    // every rename and removal, and collapsing them each time would undo what
    // the user just expanded.
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

    // Where Steam keeps its games. Steam lists its own libraries in its config,
    // so those are read rather than entered; a folder it does not list, on a
    // disk it has not been told about, is added here.
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
        // Steam's own entries cannot be removed here.
        if (cell && cell->data(kDiscoveredRole).toBool()) return;
        m_libraries->removeRow(row);
    });

    layout->addStretch();
}

void LaunchersDialog::addRunnerRow(const LauncherRunner &runner) {
    const int row = m_runners->rowCount();
    m_runners->insertRow(row);

    auto *name = new QTableWidgetItem(runner.name);
    // The id is carried on the row rather than displayed: items store it, so it
    // has to survive a rename.
    name->setData(Qt::UserRole, runner.id);
    name->setData(kDiscoveredRole, runner.discovered);
    m_runners->setItem(row, kRunnerName, name);
    m_runners->setItem(row, kRunnerCommand, new QTableWidgetItem(runner.command));

    // A checkbox rather than a field: whether the program is an alias needing a
    // shell.
    auto *shell = new QTableWidgetItem;
    shell->setFlags((shell->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    shell->setCheckState(runner.useShell ? Qt::Checked : Qt::Unchecked);
    shell->setToolTip(tr("Start it from an interactive shell, for an alias or a shell "
                         "function rather than a program."));
    m_runners->setItem(row, kRunnerShell, shell);

    if (!runner.discovered) return;

    // Detected rather than configured, so there is nothing to change. The row
    // is shown so an item can be pointed at it, and greyed to make clear it is
    // not editable.
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
    // A program picked from the file dialog is a path, so no shell is needed,
    // and a row still unnamed takes the program's file name.
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

    // Only a typed name is editable.
    if (kind == PrefixRow::Named) {
        row->setFlags(row->flags() | Qt::ItemIsEditable);
        return row;
    }

    // Neither the per-program prefix nor a folder left behind by a deleted item
    // is a typed name. Both are shown so the list covers everything in the
    // folder, and greyed so neither looks editable.
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
    // Under a greyed row, a prefix nothing uses any more, the programs are
    // greyed too.
    const bool greyed = parent->data(kPrefixName, kPrefixKindRole).toInt() ==
                        static_cast<int>(PrefixRow::Leftover);

    for (const QString &program : programs) {
        auto *child = new QTreeWidgetItem(parent);
        // The file name only: the full path is too long for the column and is
        // put in the tooltip instead.
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

    // The table is rebuilt around the names as typed, a half-entered name
    // included.
    const QStringList names = typedPrefixes();
    const QTreeWidgetItem *current = m_prefixes->currentItem();
    const QString selected = current && !current->parent() ? current->text(kPrefixName)
                                                           : QString();

    OpenScreen open;
    open.name = m_openScreen;
    open.items = m_openItems;
    const QVector<PrefixUse> found =
        prefixesIn(m_prefixesDir->text().trimmed(), names, open);

    // Rebuilt from scratch with the edit signal blocked, since each row added
    // would otherwise trigger another rebuild.
    const QSignalBlocker blocker(m_prefixes);
    m_prefixes->clear();

    // How many items run in a prefix of their own, which is what the first row
    // stands for.
    int perApplication = 0;
    for (const PrefixUse &use : found) {
        if (use.kind == PrefixUse::PerApplication) perApplication += use.items();
    }
    QTreeWidgetItem *automatic = addPrefixRow(
        PrefixRow::Fixed, tr("Default (per application)"),
        perApplication > 0 ? tr("%1 items").arg(perApplication) : tr("nothing"), QString(),
        tr("What an item gets when it names no prefix: one of its own, under the "
           "folder above, built the first time it runs."));

    // Under it, the prefixes it stands for, one per program. Each is a separate
    // folder with a size of its own, and one whose program has left the desktop
    // is marked unused here, under the row it belongs to, where it can be
    // deleted.
    for (const PrefixUse &use : found) {
        if (use.kind != PrefixUse::PerApplication) continue;
        // Only prefixes that exist. An item whose prefix has never been created
        // points at a missing folder, which has nothing to show and nothing to
        // delete; the count on the row above already includes that item.
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
        // Nothing runs in it: greyed like any other unused prefix, and labelled
        // as unused rather than only looking different.
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

    // The named ones in the order given, blank rows included: a row being typed
    // must not disappear as it is edited.
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
        // What actually runs in it, which is what tells whether a shared prefix
        // is still needed.
        if (use) addPrefixPrograms(row, use->programs);
    }

    // Last, at the top level: folders in the prefixes directory that this
    // application did not create. The ones it did create are all listed under a
    // row by now.
    for (const PrefixUse &use : found) {
        if (use.kind != PrefixUse::Foreign) continue;
        addPrefixRow(PrefixRow::Leftover, use.name, tr("not ours"), use.path,
                     tr("A folder in the prefixes directory that this application did not "
                        "make. Left alone."));
    }

    // Restore the selection and the expanded rows, since the rebuild replaced
    // the whole tree.
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
    // The selection rather than the current item: clicking the empty space
    // below the rows clears the selection, and the buttons follow it.
    const QList<QTreeWidgetItem *> picked = m_prefixes->selectedItems();
    const QTreeWidgetItem *row = picked.isEmpty() ? nullptr : picked.first();
    const QTreeWidgetItem *parent = row ? row->parent() : nullptr;

    // Add appends a new prefix to the list. Prefixes are the top-level rows,
    // so Add applies there; the programs listed under them cannot be added to.
    m_prefixAdd->setEnabled(parent == nullptr);

    // Remove deletes a prefix: one created by hand, or one of the automatic
    // ones under the default row, each being a folder of its own. Neither a
    // program row nor the default row itself is a prefix, and a folder this
    // application did not create is left alone.
    const auto kindOf = [](const QTreeWidgetItem *item) {
        return item ? static_cast<PrefixRow>(item->data(kPrefixName, kPrefixKindRole).toInt())
                    : PrefixRow::Program;
    };
    const bool namedPrefix = row && !parent && kindOf(row) == PrefixRow::Named;
    // An automatic prefix is only a folder, not an entry in a list, so when the
    // folder does not exist yet there is nothing to remove and the button is
    // disabled rather than silently doing nothing.
    const bool automaticPrefix =
        parent && kindOf(parent) == PrefixRow::Fixed &&
        QFileInfo::exists(row->data(kPrefixName, kPrefixPathRole).toString());
    m_prefixRemove->setEnabled(namedPrefix || automaticPrefix);
}

void LaunchersDialog::measurePrefixes() {
    // Every prefix is a Windows installation, so this walks some thousands of
    // files; the button is disabled and relabelled while it runs.
    m_prefixSizes->setEnabled(false);
    m_prefixSizes->setText(tr("Reading..."));
    QCoreApplication::processEvents();

    // Every row with a folder behind it, including the per-application ones
    // under the first row, which is where most of the disk usage is.
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
    // The two kinds of row that stand for a prefix of their own: one created by
    // hand, and one of the automatic ones under the default row.
    const bool named = !parent && kindOf(row) == PrefixRow::Named;
    const bool automatic = parent && kindOf(parent) == PrefixRow::Fixed;
    if (!named && !automatic) return;

    const QString name = row->text(kPrefixName).trimmed();
    const QString path = row->data(kPrefixName, kPrefixPathRole).toString();
    const bool onDisk = !path.isEmpty() && QFileInfo::exists(path);

    // The path and its size, so the confirmation is not based on a folder name
    // alone when it may hold gigabytes of saved games.
    QString what = path;
    if (onDisk) {
        what = tr("%1\n\n%2").arg(path, QLocale().formattedDataSize(folderSize(path)));
    }

    if (!onDisk) {
        // Nothing created yet. A named prefix is still a row to remove from the
        // list; an automatic one is only a folder, so there is nothing to do.
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
    // A prefix is a C: drive: its saved games are there and nowhere else.
    QString warning = tr("A prefix holds the programs' settings and saved games, and "
                         "deleting its folder cannot be undone.");
    // One still in use will be recreated from scratch the next time the
    // program runs.
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
        // Removed from the list with the folder left in place, where it is
        // listed as a leftover.
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

    // How many games it holds, which is what matters about a folder just added.
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
            // Only the named ones are saved: the first row stands for the
            // prefix an item gets when it names none.
            QStringList prefixes;
            for (const QString &typed : typedPrefixes()) {
                const QString name = prefixFolderName(typed);
                // A row left blank is not a prefix yet, and two rows with the
                // same name are the same prefix.
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
            // A row with no program cannot start anything, so it is not a
            // runner yet.
            if (runner.command.isEmpty()) continue;
            runner.name = nameCell->text().trimmed();
            if (runner.name.isEmpty()) runner.name = runner.command;
            runner.id = nameCell->data(Qt::UserRole).toString();

            // A detected runner is restored as it was, id included, and is not
            // saved.
            runner.discovered = nameCell->data(kDiscoveredRole).toBool();
            if (runner.discovered) {
                if (const LauncherRunner *was = launcher.runner(runner.id)) runner = *was;
            } else {
                runner.useShell = shellCell && shellCell->checkState() == Qt::Checked;
                if (runner.id.isEmpty()) {
                    runner.id = uniqueRunnerId(taken, runner.name);
                    taken << runner.id;
                    // Written back onto the row, so leaving the page and
                    // returning does not generate a second id for the same
                    // runner.
                    nameCell->setData(Qt::UserRole, runner.id);
                }
            }
            runners.append(runner);
        }
        // If every row was cleared rather than removed, the launcher keeps the
        // runners it had: it needs at least one way to start something.
        if (!runners.isEmpty()) launcher.runners = runners;

        if (launcher.usesSteamLibraries) {
            // Only the ones added by hand are saved: Steam's own are re-read
            // each time, and saving them would freeze a list that has to follow
            // Steam.
            QStringList folders;
            for (int row = 0; row < m_libraries->rowCount(); ++row) {
                const QTableWidgetItem *cell = m_libraries->item(row, 0);
                if (!cell || cell->data(kDiscoveredRole).toBool()) continue;
                if (!cell->text().isEmpty()) folders << cell->text();
            }
            launcher.libraryFolders = folders;
        }

        // An emulator's cores are not edited here: they are read from the
        // install, and which one plays a file belongs to the item. The
        // launcher's `coresDir` and `cores`, if an older version wrote any,
        // are left as they are.
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

    // A detected list cannot be added to or removed from.
    int discovered = 0;
    for (const LauncherRunner &runner : launcher.runners) discovered += runner.discovered;

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

    // Hide the sections this launcher does not use before showing the others.
    // Showing one before hiding another leaves both in the layout for a pass,
    // which grows the dialog to fit the pair, and a window is not shrunk back
    // when its contents need less.
    if (!launcher.usesSteamLibraries) m_librariesBox->hide();
    if (!launcher.usesPrefixes) m_prefixesBox->hide();

    m_librariesBox->setVisible(launcher.usesSteamLibraries);
    if (launcher.usesSteamLibraries) {
        m_libraries->setRowCount(0);
        // Steam's own first, then the folders added by hand.
        const QStringList discovered = discoveredSteamLibraries();
        for (const QString &path : discovered) addLibraryRow(path, true);
        for (const QString &path : launcher.libraryFolders) {
            if (discovered.contains(path)) continue;
            addLibraryRow(path, false);
        }
    }

}
