#include "SteamGamePickerDialog.h"

#include "DialogChrome.h"
#include "ItemDetails.h"
#include "Launchers.h"
#include "SteamLibrary.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// The manifest behind a row, which is what the item points at.
constexpr int kCommandRole = Qt::UserRole;
} // namespace

SteamGamePickerDialog::SteamGamePickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Insert Steam Game"));

    const DialogChrome chrome = applyDialogChrome(this, tr("Steam Games"));

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Type to narrow the list"));
    m_filterEdit->setClearButtonEnabled(true);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(kDialogLabelGap);
    form->setVerticalSpacing(kDialogRowSpacing);
    form->addRow(tr("Find:"), m_filterEdit);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(32, 32));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame); // the style sheet draws the edge

    m_summary = new QLabel(this);
    markHintLabel(m_summary);
    m_summary->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Insert"));
    m_okButton->setEnabled(false);
    markDefaultButton(m_okButton);
    m_okButton->setDefault(true);

    chrome.fields->addLayout(form);
    chrome.fields->addWidget(m_list, 1);
    chrome.fields->addWidget(m_summary);
    chrome.footer->addWidget(buttons);

    // The libraries Steam lists itself, plus any added by hand in
    // Options > Launchers.
    QStringList libraries = discoveredSteamLibraries();
    if (const Launcher *steam = launcherById(QStringLiteral("steam"))) {
        for (const QString &folder : steam->libraryFolders) {
            if (!libraries.contains(folder)) libraries.append(folder);
        }
    }

    // Each game is resolved exactly as a dropped manifest would be, so the name
    // and icon shown here are the ones the item will have.
    const QVector<SteamGame> games = steamGamesIn(libraries);
    for (const SteamGame &game : games) {
        ProgramItem item;
        item.command = game.manifest;
        completeProgramItem(item);
        auto *row = new QListWidgetItem(item.icon, item.name, m_list);
        row->setData(kCommandRole, game.manifest);
        row->setToolTip(tr("%1\nApp %2").arg(game.manifest, game.appId));
    }

    if (games.isEmpty()) {
        m_summary->setText(
            libraries.isEmpty()
                ? tr("No Steam library was found. Options > Launchers > Steam is where one "
                     "can be added by hand.")
                : tr("No games are installed in the %n library folder(s) found.", nullptr,
                     libraries.size()));
    } else {
        m_summary->setText(tr("%n game(s) across %1 library folder(s). Pick as many as you "
                              "like: they all go into the group.",
                              nullptr, games.size())
                               .arg(libraries.size()));
    }

    connect(m_filterEdit, &QLineEdit::textChanged, this, &SteamGamePickerDialog::applyFilter);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            [this] { m_okButton->setEnabled(!m_list->selectedItems().isEmpty()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Typing goes to the filter wherever the focus is, so the list can be
    // narrowed without using the mouse first.
    m_filterEdit->setFocus();
    resize(520, 520);
}

void SteamGamePickerDialog::applyFilter(const QString &text) {
    const QString needle = text.trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *row = m_list->item(i);
        const bool matches =
            needle.isEmpty() || row->text().contains(needle, Qt::CaseInsensitive);
        // A filtered-out row loses its selection, so Insert never adds
        // something the list is not showing.
        if (!matches && row->isSelected()) row->setSelected(false);
        row->setHidden(!matches);
    }
}

QVector<ProgramItem> SteamGamePickerDialog::chosenItems() const {
    QVector<ProgramItem> items;
    for (QListWidgetItem *row : m_list->selectedItems()) {
        ProgramItem item;
        item.command = row->data(kCommandRole).toString();
        item.name = row->text();
        items.append(item);
    }
    return items;
}
