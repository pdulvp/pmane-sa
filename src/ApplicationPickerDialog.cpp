#include "ApplicationPickerDialog.h"

#include "DesktopEntry.h"
#include "ItemDetails.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// The path of the .desktop file behind a row.
constexpr int kCommandRole = Qt::UserRole;

// How far up the folders an entry sits in this will look for something to
// tell it apart from another of the same name.
constexpr int kMaxDisambiguationDepth = 3;

// Wine's start menu keeps a "Manual" and an "Uninstall" inside every game's
// own folder, so names alone would leave the list with a column of rows all
// reading the same thing. Where names collide, the nearest folder that does
// tell the entries apart is added to them.
void disambiguateNames(QVector<ProgramItem> &applications) {
    QHash<QString, QVector<int>> byName;
    for (int i = 0; i < applications.size(); ++i) {
        byName[applications[i].name].append(i);
    }

    // The folder an entry sits in, `depth` levels up.
    const auto folderName = [&applications](int index, int depth) {
        QDir dir(QFileInfo(applications[index].command).absolutePath());
        for (int up = 1; up < depth; ++up) dir.cdUp();
        return dir.dirName();
    };

    for (const QVector<int> &clash : byName) {
        if (clash.size() < 2) continue;

        for (int depth = 1; depth <= kMaxDisambiguationDepth; ++depth) {
            QHash<QString, int> seen;
            for (int index : clash) seen[folderName(index, depth)]++;
            // Go further up until one folder name settles the whole set; two
            // entries still sharing one would be no better off than before.
            if (seen.size() != clash.size() && depth < kMaxDisambiguationDepth) continue;

            for (int index : clash) {
                applications[index].name = ApplicationPickerDialog::tr("%1 (%2)")
                                               .arg(applications[index].name,
                                                    folderName(index, depth));
            }
            break;
        }
    }
}
} // namespace

ApplicationPickerDialog::ApplicationPickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Insert Application"));

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Type to narrow the list"));
    m_filterEdit->setClearButtonEnabled(true);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(32, 32));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformItemSizes(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Insert"));
    m_okButton->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Find:"), this));
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_list);
    layout->addWidget(buttons);

    // Each application is worked out exactly the way a dropped file would be,
    // so the name and icon on show here are the ones the item will wear.
    QVector<ProgramItem> applications;
    const QStringList paths = installedDesktopEntries();
    applications.reserve(paths.size());
    for (const QString &path : paths) {
        ProgramItem item;
        item.command = path;
        completeProgramItem(item);
        if (item.name.isEmpty()) item.name = QFileInfo(path).completeBaseName();
        applications.append(item);
    }
    disambiguateNames(applications);
    std::sort(applications.begin(), applications.end(),
              [](const ProgramItem &a, const ProgramItem &b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });

    for (const ProgramItem &item : applications) {
        auto *row = new QListWidgetItem(item.icon, item.name, m_list);
        row->setData(kCommandRole, item.command);
        row->setToolTip(item.command);
    }


    connect(m_filterEdit, &QLineEdit::textChanged, this, &ApplicationPickerDialog::applyFilter);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            [this] { m_okButton->setEnabled(!m_list->selectedItems().isEmpty()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Typing goes to the filter wherever the focus is, so the list can be
    // narrowed without reaching for the mouse first.
    m_filterEdit->setFocus();
    resize(360, 460);
}

void ApplicationPickerDialog::applyFilter(const QString &text) {
    const QString needle = text.trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *row = m_list->item(i);
        const bool matches =
            needle.isEmpty() || row->text().contains(needle, Qt::CaseInsensitive) ||
            row->data(kCommandRole).toString().contains(needle, Qt::CaseInsensitive);
        // A row that is filtered away takes its selection with it, so Insert
        // never adds something the list is not showing.
        if (!matches && row->isSelected()) row->setSelected(false);
        row->setHidden(!matches);
    }
}

QVector<ProgramItem> ApplicationPickerDialog::chosenItems() const {
    QVector<ProgramItem> items;
    for (QListWidgetItem *row : m_list->selectedItems()) {
        ProgramItem item;
        item.command = row->data(kCommandRole).toString();
        // The name the row was picked by, so whatever told two "Manual"
        // entries apart here still tells them apart on the desktop.
        item.name = row->text();
        items.append(item);
    }
    return items;
}
