#include "RunnersDialog.h"

#include "CompatTools.h"
#include "DialogChrome.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// Wide enough for the list of runners to sit beside a form rather than
// above it.
constexpr int kListWidth = 150;
// Rows with some air in them: there are only a handful, and they read as
// tabs down the side rather than as a list to be scrolled.
constexpr int kListRowHeight = 34;

QString joinExtensions(const QStringList &extensions) {
    return extensions.join(QLatin1Char(' '));
}

QStringList splitExtensions(const QString &text) {
    QStringList out;
    // Written however it comes naturally -- spaces, commas, dots and all --
    // and kept as bare lower-case extensions.
    for (const QString &piece : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                            Qt::SkipEmptyParts)) {
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

} // namespace

RunnersDialog::RunnersDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Runners"));

    const DialogChrome chrome = applyDialogChrome(this, tr("Runners"));

    m_runners = ::runners();

    m_list = new QListWidget(this);
    m_list->setFixedWidth(kListWidth);
    m_list->setFrameShape(QFrame::NoFrame); // the style sheet draws the edge
    // The rows are as wide as the list; nothing is ever scrolled sideways.
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_pages = new QStackedWidget(this);
    m_fields.resize(m_runners.size());
    for (int i = 0; i < m_runners.size(); ++i) {
        auto *row = new QListWidgetItem(m_runners[i].name, m_list);
        row->setSizeHint(QSize(0, kListRowHeight));
        row->setTextAlignment(Qt::AlignCenter);
        m_pages->addWidget(buildPage(m_runners[i], m_fields[i]));
    }

    auto *columns = new QHBoxLayout;
    columns->setSpacing(kDialogMargin);
    columns->addWidget(m_list);
    columns->addWidget(m_pages, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        this);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(ok);
        ok->setDefault(true);
    }
    QPushButton *defaults = buttons->button(QDialogButtonBox::RestoreDefaults);
    defaults->setAutoDefault(false);
    defaults->setToolTip(tr("Put this runner back the way it shipped."));

    chrome.fields->addLayout(columns, 1);
    chrome.footer->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(defaults, &QPushButton::clicked, this, &RunnersDialog::restoreDefaults);

    m_list->setCurrentRow(0);
    resize(700, 580);
}

QWidget *RunnersDialog::buildPage(const Runner &runner, Page &page) {
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kDialogRowSpacing);

    auto *heading = new QLabel(runner.description, widget);
    markHintLabel(heading);
    heading->setWordWrap(true);
    layout->addWidget(heading);

    page.command = new QLineEdit(runner.command, widget);
    page.command->setPlaceholderText(tr("A program on the PATH, a full path, or an alias"));
    page.arguments = new QLineEdit(runner.arguments, widget);
    page.useShell = new QCheckBox(tr("Start it from an interactive shell"), widget);
    page.useShell->setChecked(runner.useShell);
    page.useShell->setToolTip(
        tr("Needed when the program above is a shell alias or function rather than something "
           "on the PATH."));
    page.extensions = new QLineEdit(joinExtensions(runner.extensions), widget);
    page.extensions->setPlaceholderText(tr("conf  iso  nes"));

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(kDialogLabelGap);
    form->setVerticalSpacing(kDialogRowSpacing);
    form->addRow(tr("Command:"), page.command);
    form->addRow(QString(), page.useShell);
    form->addRow(tr("Arguments:"), page.arguments);

    page.coreArguments = nullptr;
    page.coresDir = nullptr;
    page.cores = nullptr;
    page.prefixesDir = nullptr;
    if (runner.usesCores) {
        page.coreArguments = new QLineEdit(runner.coreArguments, widget);
        page.coreArguments->setToolTip(
            tr("Used instead of the line above when a core is known for the file."));
        form->addRow(tr("With a core:"), page.coreArguments);
    }
    form->addRow(tr("File types:"), page.extensions);
    layout->addLayout(form);

    // What the placeholders stand for, said once where they are typed.
    auto *placeholders =
        new QLabel(runner.usesCores
                       ? tr("{file} is the item's path, {dir} the folder it sits in, and "
                            "{core} the core chosen for it below.")
                       : tr("{file} is the item's path, {dir} the folder it sits in."),
                   widget);
    markHintLabel(placeholders);
    placeholders->setWordWrap(true);
    layout->addWidget(placeholders);

    if (runner.id == QLatin1String("wine")) {
        // What is on the machine to run a Windows program with, which is
        // read off it rather than set here: the system wine, and every
        // Proton build Steam knows about. An item picks one of these on its
        // own properties page.
        layout->addWidget(new QLabel(tr("Installed on this machine:"), widget));

        auto *installed = new QListWidget(widget);
        installed->setSelectionMode(QAbstractItemView::NoSelection);
        installed->setFocusPolicy(Qt::NoFocus);
        installed->setFrameShape(QFrame::NoFrame);

        const QString wine = QStandardPaths::findExecutable(runner.command);
        auto *system = new QListWidgetItem(
            wine.isEmpty() ? tr("Wine (system) -- not found on the PATH")
                           : tr("Wine (system)"),
            installed);
        if (!wine.isEmpty()) system->setToolTip(wine);

        for (const CompatTool &tool : installedCompatTools()) {
            auto *row = new QListWidgetItem(tool.displayName, installed);
            row->setToolTip(tool.protonPath);
        }
        layout->addWidget(installed, 1);

        auto *note = new QLabel(
            installedCompatTools().isEmpty()
                ? tr("No Proton builds found under Steam's compatibilitytools.d.")
                : tr("Proton builds come from Steam's compatibilitytools.d; the item's "
                     "properties page is where one is chosen."),
            widget);
        markHintLabel(note);
        note->setWordWrap(true);
        layout->addWidget(note);
        // Where the prefixes are built. Set here rather than on the item,
        // since it is one folder for all of them.
        page.prefixesDir = new QLineEdit(runner.prefixesDir, widget);
        page.prefixesDir->setPlaceholderText(
            tr("Kept with this application's own files when left empty"));
        auto *prefixBrowse = new QPushButton(tr("Browse..."), widget);
        prefixBrowse->setAutoDefault(false);
        auto *prefixRow = new QHBoxLayout;
        prefixRow->setSpacing(8);
        prefixRow->addWidget(new QLabel(tr("Prefixes folder:"), widget));
        prefixRow->addWidget(page.prefixesDir, 1);
        prefixRow->addWidget(prefixBrowse);
        layout->addLayout(prefixRow);

        auto *prefixNote = new QLabel(
            tr("A program run with Proton gets a wine prefix of its own -- its C: drive, "
               "its registry and its saved games -- built under this folder the first time "
               "it runs. They grow to gigabytes apiece, so a roomy disk suits them."),
            widget);
        markHintLabel(prefixNote);
        prefixNote->setWordWrap(true);
        layout->addWidget(prefixNote);

        QLineEdit *prefixEdit = page.prefixesDir;
        connect(prefixBrowse, &QPushButton::clicked, this, [this, prefixEdit] {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Prefixes Folder"),
                                                                   prefixEdit->text());
            if (!dir.isEmpty()) prefixEdit->setText(dir);
        });

    }

    if (!runner.usesCores) {
        layout->addStretch();
        return widget;
    }

    auto *coresLabel = new QLabel(tr("Cores, by file type:"), widget);
    layout->addWidget(coresLabel);

    page.coresDir = new QLineEdit(runner.coresDir, widget);
    page.coresDir->setPlaceholderText(tr("Found beside the program when left empty"));
    auto *browse = new QPushButton(tr("Browse..."), widget);
    browse->setAutoDefault(false);
    auto *coresDirRow = new QHBoxLayout;
    coresDirRow->setSpacing(8);
    coresDirRow->addWidget(new QLabel(tr("Cores folder:"), widget));
    coresDirRow->addWidget(page.coresDir, 1);
    coresDirRow->addWidget(browse);
    layout->addLayout(coresDirRow);

    page.cores = new QTableWidget(0, 2, widget);
    page.cores->setHorizontalHeaderLabels({tr("File type"), tr("Core")});
    page.cores->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    page.cores->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    page.cores->verticalHeader()->setVisible(false);
    page.cores->setSelectionBehavior(QAbstractItemView::SelectRows);
    page.cores->setFrameShape(QFrame::NoFrame);
    // Room for a handful of rows without scrolling: a console or two each.
    page.cores->setMinimumHeight(170);
    fillCoreTable(page.cores, runner.cores);
    layout->addWidget(page.cores, 1);

    auto *add = new QPushButton(tr("Add"), widget);
    auto *remove = new QPushButton(tr("Remove"), widget);
    add->setAutoDefault(false);
    remove->setAutoDefault(false);
    auto *coreButtons = new QHBoxLayout;
    coreButtons->addStretch();
    coreButtons->addWidget(add);
    coreButtons->addWidget(remove);
    layout->addLayout(coreButtons);

    QTableWidget *table = page.cores;
    connect(add, &QPushButton::clicked, this, [table] {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem);
        table->setItem(row, 1, new QTableWidgetItem);
        table->editItem(table->item(row, 0));
    });
    connect(remove, &QPushButton::clicked, this, [table] {
        const int row = table->currentRow();
        if (row >= 0) table->removeRow(row);
    });
    QLineEdit *coresDir = page.coresDir;
    connect(browse, &QPushButton::clicked, this, [this, coresDir] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Cores Folder"),
                                                               coresDir->text());
        if (!dir.isEmpty()) coresDir->setText(dir);
    });

    return widget;
}

void RunnersDialog::restoreDefaults() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_runners.size()) return;

    const QVector<Runner> shipped = defaultRunners();
    for (const Runner &runner : shipped) {
        if (runner.id != m_runners[row].id) continue;

        const Page &page = m_fields[row];
        page.command->setText(runner.command);
        page.arguments->setText(runner.arguments);
        page.useShell->setChecked(runner.useShell);
        page.extensions->setText(joinExtensions(runner.extensions));
        if (page.coreArguments) page.coreArguments->setText(runner.coreArguments);
        if (page.coresDir) page.coresDir->setText(runner.coresDir);
        if (page.prefixesDir) page.prefixesDir->setText(runner.prefixesDir);
        if (page.cores) fillCoreTable(page.cores, runner.cores);
        return;
    }
}

QVector<Runner> RunnersDialog::runners() const {
    QVector<Runner> result = m_runners;
    for (int i = 0; i < result.size(); ++i) {
        const Page &page = m_fields[i];
        result[i].command = page.command->text().trimmed();
        result[i].arguments = page.arguments->text().trimmed();
        result[i].useShell = page.useShell->isChecked();
        result[i].extensions = splitExtensions(page.extensions->text());
        if (page.coreArguments) result[i].coreArguments = page.coreArguments->text().trimmed();
        if (page.coresDir) result[i].coresDir = page.coresDir->text().trimmed();
        if (page.prefixesDir) result[i].prefixesDir = page.prefixesDir->text().trimmed();
        if (!page.cores) continue;

        QVector<QPair<QString, QString>> cores;
        for (int row = 0; row < page.cores->rowCount(); ++row) {
            const QTableWidgetItem *extension = page.cores->item(row, 0);
            const QTableWidgetItem *core = page.cores->item(row, 1);
            if (!extension || !core) continue;
            const QString type = extension->text().trimmed().toLower();
            if (type.isEmpty() || core->text().trimmed().isEmpty()) continue;
            cores.append({type, core->text().trimmed()});
        }
        result[i].cores = cores;
    }
    return result;
}
