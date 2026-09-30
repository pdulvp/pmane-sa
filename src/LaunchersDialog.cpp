#include "LaunchersDialog.h"

#include "CompatTools.h"
#include "DialogChrome.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
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

// The rows on the profile page, in the order they are added: the two that
// hold a field and a browse button are addressed by number, a layout having
// no widget to look them up by.
constexpr int kPrefixesRow = 1;
constexpr int kBaseConfigRow = 3;

// An id no other profile of this launcher is using, derived from the name so
// that a saved desktop names something readable.
QString uniqueProfileId(const Launcher &launcher, const QString &name) {
    static const QRegularExpression unsafe(QStringLiteral("[^a-z0-9]+"));
    QString base = name.toLower().replace(unsafe, QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-'))) base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-'))) base.chop(1);
    if (base.isEmpty()) base = QStringLiteral("profile");

    QString candidate = base;
    for (int n = 2; launcher.profile(candidate); ++n) {
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
    m_tree->setRootIsDecorated(true);

    m_stack = new QStackedWidget(this);
    buildLauncherPage();
    buildProfilePage();
    m_stack->addWidget(m_launcherPage);
    m_stack->addWidget(m_profilePage);

    m_addButton = new QPushButton(tr("Add"), this);
    m_duplicateButton = new QPushButton(tr("Duplicate"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    for (QPushButton *button : {m_addButton, m_duplicateButton, m_removeButton}) {
        button->setAutoDefault(false);
    }
    m_addButton->setToolTip(tr("A new launcher profile under the launcher selected."));

    auto *treeButtons = new QHBoxLayout;
    treeButtons->setSpacing(6);
    treeButtons->addWidget(m_addButton);
    treeButtons->addWidget(m_duplicateButton);
    treeButtons->addWidget(m_removeButton);

    auto *left = new QVBoxLayout;
    left->setSpacing(kDialogRowSpacing);
    left->addWidget(m_tree, 1);
    left->addLayout(treeButtons);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(kDialogMargin);
    columns->addLayout(left);
    columns->addWidget(m_stack, 1);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            &LaunchersDialog::showTreeMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, &LaunchersDialog::onTreeItemChanged);
    connect(m_addButton, &QPushButton::clicked, this, &LaunchersDialog::addProfile);
    connect(m_duplicateButton, &QPushButton::clicked, this, &LaunchersDialog::duplicateProfile);
    connect(m_removeButton, &QPushButton::clicked, this, &LaunchersDialog::removeProfile);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        commitCurrentPage();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildTree(0, -1);
    resize(760, 620);
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
    // One program to a launcher: the profiles under it differ by what they
    // tell it, not by what they start.
    m_launcherCommand = new QLineEdit(m_launcherPage);
    m_launcherCommand->setPlaceholderText(tr("A program on the PATH, a full path, or an alias"));
    m_launcherUseShell = new QCheckBox(tr("Start it from an interactive shell"), m_launcherPage);
    m_launcherUseShell->setToolTip(
        tr("Needed when the program above is a shell alias or function rather than something "
           "on the PATH."));
    m_launcherArguments = new QLineEdit(m_launcherPage);

    m_launcherForm = new QFormLayout;
    m_launcherForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_launcherForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_launcherForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_launcherForm->setHorizontalSpacing(kDialogLabelGap);
    m_launcherForm->setVerticalSpacing(kDialogRowSpacing);
    m_launcherForm->addRow(tr("File types:"), m_extensions);
    m_launcherForm->addRow(tr("Command:"), m_launcherCommand);
    m_launcherForm->addRow(QString(), m_launcherUseShell);
    m_launcherForm->addRow(tr("Arguments:"), m_launcherArguments);
    layout->addLayout(m_launcherForm);

    m_launcherPlaceholders = new QLabel(m_launcherPage);
    markHintLabel(m_launcherPlaceholders);
    m_launcherPlaceholders->setWordWrap(true);
    layout->addWidget(m_launcherPlaceholders);

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
    m_cores->setMinimumHeight(120);
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

    // What is on the machine to run a Windows program with, read off it
    // rather than set here. A profile picks one of these.
    m_buildsBox = new QWidget(m_launcherPage);
    auto *buildsLayout = new QVBoxLayout(m_buildsBox);
    buildsLayout->setContentsMargins(0, 0, 0, 0);
    buildsLayout->setSpacing(kDialogRowSpacing);
    buildsLayout->addWidget(new QLabel(tr("Installed on this machine:"), m_buildsBox));

    m_builds = new QListWidget(m_buildsBox);
    m_builds->setSelectionMode(QAbstractItemView::NoSelection);
    m_builds->setFocusPolicy(Qt::NoFocus);
    m_builds->setFrameShape(QFrame::NoFrame);
    buildsLayout->addWidget(m_builds, 1);

    auto *buildsNote = new QLabel(
        tr("Proton builds come from Steam's compatibilitytools.d. A launcher profile picks "
           "one of them, and an item picks the profile."),
        m_buildsBox);
    markHintLabel(buildsNote);
    buildsNote->setWordWrap(true);
    buildsLayout->addWidget(buildsNote);
    layout->addWidget(m_buildsBox, 1);

    layout->addStretch();
}

void LaunchersDialog::buildProfilePage() {
    m_profilePage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_profilePage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kDialogRowSpacing);

    // The name lives on the row in the tree, which is where it is renamed.
    m_profileHeading = new QLabel(m_profilePage);
    QFont heading = m_profileHeading->font();
    heading.setBold(true);
    m_profileHeading->setFont(heading);
    layout->addWidget(m_profileHeading);

    m_compatTool = new QComboBox(m_profilePage);
    m_compatTool->setToolTip(
        tr("Which of the wine builds on this machine the profile runs through."));
    m_prefixesDir = new QLineEdit(m_profilePage);
    m_prefixesDir->setPlaceholderText(tr("Kept with this application's own files when empty"));
    m_prefixesDir->setToolTip(
        tr("A program run through Proton gets a wine prefix of its own -- its C: drive, its "
           "registry and its saved games -- built under this folder the first time it runs."));
    m_baseConfig = new QLineEdit(m_profilePage);
    m_baseConfig->setPlaceholderText(tr("A .conf applied before the item's own"));
    m_baseConfig->setToolTip(tr("Reached as {config} in the launcher's arguments."));
    m_core = new QLineEdit(m_profilePage);
    m_core->setPlaceholderText(tr("Left to the launcher's table when empty"));
    m_extraArguments = new QLineEdit(m_profilePage);
    m_extraArguments->setPlaceholderText(tr("Added to the launcher's own arguments"));

    m_profileForm = new QFormLayout;
    m_profileForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_profileForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_profileForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_profileForm->setHorizontalSpacing(kDialogLabelGap);
    m_profileForm->setVerticalSpacing(kDialogRowSpacing);
    // A folder and a file are picked, not typed, wherever one can be.
    auto *prefixBrowse = new QPushButton(tr("Browse..."), m_profilePage);
    prefixBrowse->setAutoDefault(false);
    auto *prefixRow = new QHBoxLayout;
    prefixRow->setSpacing(8);
    prefixRow->addWidget(m_prefixesDir, 1);
    prefixRow->addWidget(prefixBrowse);

    auto *configBrowse = new QPushButton(tr("Browse..."), m_profilePage);
    configBrowse->setAutoDefault(false);
    auto *configRow = new QHBoxLayout;
    configRow->setSpacing(8);
    configRow->addWidget(m_baseConfig, 1);
    configRow->addWidget(configBrowse);

    m_profileForm->addRow(tr("Runs through:"), m_compatTool);
    m_profileForm->addRow(tr("Prefixes folder:"), prefixRow);
    m_profileForm->addRow(tr("Core:"), m_core);
    m_profileForm->addRow(tr("Base config:"), configRow);
    m_profileForm->addRow(tr("Extra arguments:"), m_extraArguments);

    connect(prefixBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Prefixes Folder"),
                                                               m_prefixesDir->text());
        if (!dir.isEmpty()) m_prefixesDir->setText(dir);
    });
    connect(configBrowse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Base Configuration"), m_baseConfig->text(),
            tr("DOSBox configs (*.conf);;All Files (*)"));
        if (!file.isEmpty()) m_baseConfig->setText(file);
    });
    layout->addLayout(m_profileForm);

    m_placeholders = new QLabel(m_profilePage);
    markHintLabel(m_placeholders);
    m_placeholders->setWordWrap(true);
    layout->addWidget(m_placeholders);
    layout->addStretch();
}

void LaunchersDialog::rebuildTree(int selectLauncher, int selectProfile) {
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    QTreeWidgetItem *toSelect = nullptr;
    for (int r = 0; r < m_launchers.size(); ++r) {
        auto *launcherItem = new QTreeWidgetItem(m_tree, {m_launchers[r].name});
        launcherItem->setData(0, LauncherRole, r);
        launcherItem->setData(0, ProfileRole, -1);
        launcherItem->setSizeHint(0, QSize(0, kTreeRowHeight));
        QFont bold = launcherItem->font(0);
        bold.setBold(true);
        launcherItem->setFont(0, bold);

        for (int p = 0; p < m_launchers[r].profiles.size(); ++p) {
            auto *profileItem =
                new QTreeWidgetItem(launcherItem, {m_launchers[r].profiles[p].name});
            profileItem->setData(0, LauncherRole, r);
            profileItem->setData(0, ProfileRole, p);
            profileItem->setSizeHint(0, QSize(0, kTreeRowHeight));
            // The name lives here: a double click on the row edits it.
            profileItem->setFlags(profileItem->flags() | Qt::ItemIsEditable);
            if (r == selectLauncher && p == selectProfile) toSelect = profileItem;
        }
        launcherItem->setExpanded(true);
        if (r == selectLauncher && selectProfile < 0) toSelect = launcherItem;
    }

    if (!toSelect && m_tree->topLevelItemCount() > 0) toSelect = m_tree->topLevelItem(0);
    m_editingLauncher = -1;
    m_editingProfile = -1;
    m_tree->setCurrentItem(toSelect);
    showPageFor(toSelect);
}

void LaunchersDialog::currentSelection(int *launcher, int *profile) const {
    *launcher = -1;
    *profile = -1;
    if (QTreeWidgetItem *item = m_tree->currentItem()) {
        *launcher = item->data(0, LauncherRole).toInt();
        *profile = item->data(0, ProfileRole).toInt();
    }
}

void LaunchersDialog::commitCurrentPage() {
    if (m_editingLauncher < 0 || m_editingLauncher >= m_launchers.size()) return;
    Launcher &launcher = m_launchers[m_editingLauncher];

    if (m_editingProfile < 0) {
        launcher.extensions = splitExtensions(m_extensions->text());
        launcher.command = m_launcherCommand->text().trimmed();
        launcher.arguments = m_launcherArguments->text().trimmed();
        launcher.useShell = m_launcherUseShell->isChecked();
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
        return;
    }

    if (m_editingProfile >= launcher.profiles.size()) return;
    LauncherProfile &profile = launcher.profiles[m_editingProfile];
    profile.extraArguments = m_extraArguments->text().trimmed();
    if (launcher.usesCompatTools) {
        profile.compatToolId = m_compatTool->currentData().toString();
        profile.prefixesDir = m_prefixesDir->text().trimmed();
    }
    if (launcher.usesCores) profile.core = m_core->text().trimmed();
    if (launcher.usesBaseConfig) profile.baseConfig = m_baseConfig->text().trimmed();
}

void LaunchersDialog::showPageFor(QTreeWidgetItem *item) {
    if (!item) return;
    const int r = item->data(0, LauncherRole).toInt();
    const int p = item->data(0, ProfileRole).toInt();
    if (r < 0 || r >= m_launchers.size()) return;
    const Launcher &launcher = m_launchers[r];

    m_editingLauncher = r;
    m_editingProfile = p;

    // A launcher always keeps one way of starting things, so the profile it
    // shipped with cannot be taken away.
    m_removeButton->setEnabled(p > 0);
    m_duplicateButton->setEnabled(p >= 0);

    if (p < 0) {
        m_launcherName->setText(launcher.name);
        m_launcherDescription->setText(launcher.description);
        m_extensions->setText(joinExtensions(launcher.extensions));
        m_launcherCommand->setText(launcher.command);
        m_launcherArguments->setText(launcher.arguments);
        m_launcherUseShell->setChecked(launcher.useShell);
        QStringList launcherHints{tr("{file} is the item's path, {dir} the folder it sits in")};
        if (launcher.usesCores) launcherHints << tr("{core} the core chosen for it");
        if (launcher.usesBaseConfig) launcherHints << tr("{config} the profile's base config");
        QString hint = launcherHints.join(QStringLiteral(", ")) + QLatin1Char('.');
        if (launcher.usesCores || launcher.usesBaseConfig) {
            hint += QLatin1Char(' ') +
                    tr("A placeholder with nothing to put in it drops its argument, and the "
                       "flag in front of it.");
        }
        m_launcherPlaceholders->setText(hint);

        m_coresBox->setVisible(launcher.usesCores);
        if (launcher.usesCores) {
            m_coresDir->setText(launcher.coresDir);
            fillCoreTable(m_cores, launcher.cores);
        }

        m_buildsBox->setVisible(launcher.usesCompatTools);
        if (launcher.usesCompatTools) {
            m_builds->clear();
            const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
            auto *system = new QListWidgetItem(wine.isEmpty()
                                                   ? tr("Wine (system) -- not on the PATH")
                                                   : tr("Wine (system)"),
                                               m_builds);
            if (!wine.isEmpty()) system->setToolTip(wine);
            for (const CompatTool &tool : installedCompatTools()) {
                auto *row = new QListWidgetItem(tool.displayName, m_builds);
                row->setToolTip(tool.protonPath);
            }
        }
        m_stack->setCurrentWidget(m_launcherPage);
        return;
    }

    if (p >= launcher.profiles.size()) return;
    const LauncherProfile &profile = launcher.profiles[p];

    m_profileHeading->setText(profile.name.contains(launcher.name)
                                  ? profile.name
                                  : tr("%1 - %2").arg(launcher.name, profile.name));
    m_extraArguments->setText(profile.extraArguments);
    m_core->setText(profile.core);
    m_baseConfig->setText(profile.baseConfig);
    m_prefixesDir->setText(profile.prefixesDir);

    // Which wine build this profile runs through, rebuilt each time so that a
    // Proton installed since the dialog opened is offered too.
    if (launcher.usesCompatTools) {
        m_compatTool->clear();
        m_compatTool->addItem(tr("Wine (the launcher's own command)"), QString());
        for (const CompatTool &tool : installedCompatTools()) {
            m_compatTool->addItem(tool.displayName, tool.id);
        }
        int index = m_compatTool->findData(profile.compatToolId);
        if (index < 0 && !profile.compatToolId.isEmpty()) {
            m_compatTool->addItem(tr("%1 (not installed)").arg(profile.compatToolId),
                                  profile.compatToolId);
            index = m_compatTool->count() - 1;
        }
        m_compatTool->setCurrentIndex(std::max(index, 0));
    }

    // Only the rows this family understands: a DOSBox profile has nothing to
    // say about cores, and a ROM has no base configuration.
    m_profileForm->setRowVisible(m_compatTool, launcher.usesCompatTools);
    m_profileForm->setRowVisible(kPrefixesRow, launcher.usesCompatTools);
    m_profileForm->setRowVisible(m_core, launcher.usesCores);
    m_profileForm->setRowVisible(kBaseConfigRow, launcher.usesBaseConfig);

    m_placeholders->setText(
        tr("The program and its arguments belong to %1, on the row above; what is set here "
           "is what this way of running it does differently.")
            .arg(launcher.name));

    m_stack->setCurrentWidget(m_profilePage);
}

void LaunchersDialog::addProfile() {
    commitCurrentPage();
    int r = -1;
    int p = -1;
    currentSelection(&r, &p);
    if (r < 0) return;

    Launcher &launcher = m_launchers[r];
    // Started from the launcher's own way of doing things, which is a better
    // place to edit from than an empty form.
    LauncherProfile profile = launcher.profiles.isEmpty() ? LauncherProfile()
                                                        : launcher.profiles.first();
    profile.name = tr("New profile");
    profile.id = uniqueProfileId(launcher, profile.name);
    launcher.profiles.append(profile);

    rebuildTree(r, launcher.profiles.size() - 1);
    renameSelectedProfile();
}

void LaunchersDialog::duplicateProfile() {
    commitCurrentPage();
    int r = -1;
    int p = -1;
    currentSelection(&r, &p);
    if (r < 0 || p < 0) return;

    Launcher &launcher = m_launchers[r];
    LauncherProfile copy = launcher.profiles[p];
    copy.name = tr("%1 (copy)").arg(copy.name);
    copy.id = uniqueProfileId(launcher, copy.name);
    launcher.profiles.append(copy);

    rebuildTree(r, launcher.profiles.size() - 1);
    renameSelectedProfile();
}

void LaunchersDialog::removeProfile() {
    int r = -1;
    int p = -1;
    currentSelection(&r, &p);
    // The first profile stays: a launcher with none could start nothing.
    if (r < 0 || p <= 0) return;

    m_editingLauncher = -1; // nothing to write back into: it is going
    m_editingProfile = -1;
    m_launchers[r].profiles.remove(p);
    rebuildTree(r, std::min(p, int(m_launchers[r].profiles.size()) - 1));
}

void LaunchersDialog::renameSelectedProfile() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item || item->data(0, ProfileRole).toInt() < 0) return;
    m_tree->editItem(item, 0);
}

void LaunchersDialog::onTreeItemChanged(QTreeWidgetItem *item, int column) {
    if (!item || column != 0) return;
    const int r = item->data(0, LauncherRole).toInt();
    const int p = item->data(0, ProfileRole).toInt();
    if (r < 0 || r >= m_launchers.size() || p < 0 || p >= m_launchers[r].profiles.size()) return;

    const QString name = item->text(0).trimmed();
    if (name.isEmpty()) {
        // A profile with no name could not be picked out of the item's list,
        // so an empty one is simply refused and the row put back.
        const QSignalBlocker blocker(m_tree);
        item->setText(0, m_launchers[r].profiles[p].name);
        return;
    }
    m_launchers[r].profiles[p].name = name;
    if (r == m_editingLauncher && p == m_editingProfile) {
        m_profileHeading->setText(name.contains(m_launchers[r].name)
                                      ? name
                                      : tr("%1 - %2").arg(m_launchers[r].name, name));
    }
}

void LaunchersDialog::showTreeMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item) return;
    m_tree->setCurrentItem(item);

    const int p = item->data(0, ProfileRole).toInt();
    QMenu menu(this);
    if (p < 0) {
        QAction *add = menu.addAction(tr("Add Profile"));
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == add) addProfile();
        return;
    }

    QAction *rename = menu.addAction(tr("Rename"));
    QAction *duplicate = menu.addAction(tr("Duplicate"));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove"));
    // The launcher's own profile stays: it is what starts anything the item
    // has not been pointed somewhere else.
    remove->setEnabled(p > 0);

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (chosen == rename) renameSelectedProfile();
    else if (chosen == duplicate) duplicateProfile();
    else if (chosen == remove) removeProfile();
}
