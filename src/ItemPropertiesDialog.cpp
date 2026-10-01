#include "ItemPropertiesDialog.h"

#include "CompatTools.h"
#include "WindowScale.h"

#include <QDir>

#include <QCoreApplication>
#include <QProcess>
#include <QSignalBlocker>

#include "IconEffect.h"
#include "IconPickerDialog.h"
#include "ItemDetails.h"
#include "PeIconExtractor.h"
#include "Launchers.h"
#include "DialogChrome.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// The size the group windows show their icons at.
constexpr int kPreviewSize = 32;
} // namespace

ItemPropertiesDialog::ItemPropertiesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Program Item Properties"));

    // The banner, the coloured surfaces and the field styling the dialogs
    // share; what is left here is this dialog's own form.
    const DialogChrome chrome = applyDialogChrome(this, tr("Properties"));

    m_nameEdit = new QLineEdit(this);
    m_commandEdit = new QLineEdit(this);
    m_argsEdit = new QLineEdit(this);
    m_workDirEdit = new QLineEdit(this);
    m_launcherCombo = new QComboBox(this);
    populateLaunchers(QString());

    m_nameEdit->setPlaceholderText(tr("The name under the icon"));
    m_commandEdit->setPlaceholderText(tr("Program, document or folder to open"));
    m_argsEdit->setPlaceholderText(tr("Passed to the program as it starts"));
    m_workDirEdit->setPlaceholderText(tr("Where the program starts out"));

    auto *commandBrowse = new QPushButton(tr("Browse..."), this);
    auto *commandRow = new QHBoxLayout;
    commandRow->setSpacing(8);
    commandRow->addWidget(m_commandEdit);
    commandRow->addWidget(commandBrowse);

    auto *workDirBrowse = new QPushButton(tr("Browse..."), this);
    auto *workDirRow = new QHBoxLayout;
    workDirRow->setSpacing(8);
    workDirRow->addWidget(m_workDirEdit);
    workDirRow->addWidget(workDirBrowse);

    // The icon in a well of its own, so it reads as the thing being chosen
    // rather than as a picture dropped beside a button.
    m_iconPreview = new QLabel(this);
    m_iconPreview->setObjectName(QStringLiteral("iconWell"));
    m_iconPreview->setFixedSize(kPreviewSize + 14, kPreviewSize + 14);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    auto *changeIconButton = new QPushButton(tr("Change Icon..."), this);
    m_xpStyleCheck = new QCheckBox(tr("XP style"), this);
    m_xpStyleCheck->setToolTip(tr("Tip the icon into a shallow perspective and lift it off "
                                  "the desktop with a shadow, the way Windows XP drew its "
                                  "own. Suits the flat artwork of 16-bit programs and .ico "
                                  "files best."));
    auto *iconRow = new QHBoxLayout;
    iconRow->setSpacing(12);
    iconRow->addWidget(m_iconPreview);
    iconRow->addWidget(changeIconButton);
    iconRow->addWidget(m_xpStyleCheck);
    iconRow->addStretch();

    m_form = new QFormLayout;
    // Labels down the right of their column and fields down the left of
    // theirs, so the two make a single line each rather than a ragged pair.
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setHorizontalSpacing(kDialogLabelGap);
    m_form->setVerticalSpacing(kDialogRowSpacing);
    m_form->addRow(tr("Description:"), m_nameEdit);
    m_form->addRow(tr("Command Line:"), commandRow);
    m_form->addRow(tr("Program arguments:"), m_argsEdit);
    m_form->addRow(tr("Working Directory:"), workDirRow);
    m_form->addRow(tr("Run with:"), m_launcherCombo);

    // How big to run it. A program written for a 640x480 screen is a postage
    // stamp on a modern one, and nothing in wine will scale it: the program
    // is run inside gamescope instead, which draws it at its own size and
    // scales what comes out.
    m_scaleCombo = new QComboBox(this);
    // Filled in by updateScaleRow(), which knows whether what the item is
    // run with can do wine's own scaling.

    m_measureButton = new QPushButton(tr("Measure"), this);
    m_measureButton->setAutoDefault(false);
    m_measureButton->setToolTip(
        tr("Runs the program once, at its own size, to see how big a window it puts up. "
           "Close it when it appears -- or it will close itself once measured."));

    m_nativeSizeLabel = new QLabel(this);
    markHintLabel(m_nativeSizeLabel);

    m_scaleRow = new QWidget(this);
    auto *scaleLayout = new QHBoxLayout(m_scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleLayout->setSpacing(8);
    scaleLayout->addWidget(m_scaleCombo);
    scaleLayout->addWidget(m_measureButton);
    scaleLayout->addWidget(m_nativeSizeLabel, 1);
    m_form->addRow(tr("Window size:"), m_scaleRow);

    connect(m_measureButton, &QPushButton::clicked, this,
            &ItemPropertiesDialog::measureWindow);
    connect(m_scaleCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_scalePercent = m_scaleCombo->currentData().toInt();
        m_scaleMode = m_scaleCombo->currentData(kScaleModeRole).toInt() == 1
                          ? WindowScaleMode::WineDpi
                          : WindowScaleMode::Gamescope;
        refreshNativeSizeLabel();
    });
    m_form->addRow(tr("Icon:"), iconRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // OK is what the form is filled in for.
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(ok);
        ok->setDefault(true);
    }

    chrome.fields->addLayout(m_form);
    chrome.fields->addStretch();
    chrome.footer->addWidget(buttons);

    connect(m_commandEdit, &QLineEdit::textChanged, this,
            &ItemPropertiesDialog::updateLauncherVisibility);
    // Only the scale row follows the choice. Going through
    // updateLauncherVisibility() here would refill the list, and a list that
    // reacts to its own refilling never stops.
    connect(m_launcherCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updateScaleRow(); });
    updateLauncherVisibility();

    connect(commandBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseCommand);
    connect(changeIconButton, &QPushButton::clicked, this, &ItemPropertiesDialog::changeIcon);
    connect(m_xpStyleCheck, &QCheckBox::toggled, this, &ItemPropertiesDialog::refreshIconPreview);
    connect(workDirBrowse, &QPushButton::clicked, this, &ItemPropertiesDialog::browseWorkingDir);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(520, sizeHint().height());
}

void ItemPropertiesDialog::setItem(const ProgramItem &item) {
    m_item = item;
    m_nameEdit->setText(item.name);
    m_commandEdit->setText(item.command);
    m_argsEdit->setText(item.arguments);
    m_nativeSize = item.nativeSize;
    m_scalePercent = qMax(item.windowScalePercent, 100);
    m_scaleMode = item.windowScaleMode;
    m_workDirEdit->setText(item.workingDir);
    populateLaunchers(item.launcher);
    m_xpStyleCheck->setChecked(item.xpStyleIcon);
    updateLauncherVisibility();
    refreshIconPreview();
}

void ItemPropertiesDialog::refreshIconPreview() {
    // The icon the item will actually end up with, worked out on a copy: the
    // one picked by hand if there is one, otherwise whatever the command it
    // now names leads to. Never stored back on m_item, so an icon nobody
    // chose stays a preview and is derived again for real on OK.
    ProgramItem probe = m_item;
    probe.command = m_commandEdit->text().trimmed();
    if (!probe.command.isEmpty()) completeProgramItem(probe);

    const QIcon shown =
        m_xpStyleCheck->isChecked() ? xpStyleIcon(probe.icon) : probe.icon;
    m_iconPreview->setPixmap(shown.isNull() ? QPixmap()
                                            : shown.pixmap(kPreviewSize, kPreviewSize));
}

void ItemPropertiesDialog::changeIcon() {
    ProgramItem probe = m_item;
    probe.command = m_commandEdit->text().trimmed();

    IconPickerDialog dlg(this);
    dlg.setSource(iconSourceFile(probe), m_item.iconIndex);
    if (dlg.exec() != QDialog::Accepted) return;

    if (dlg.isReset()) {
        // Back to whatever the item would find on its own, worked out again
        // once the dialog is closed.
        m_item.iconIndex = -1;
        m_item.iconSource.clear();
        m_item.icon = QIcon();
        m_item.legacyIcon = false;
    } else {
        if (dlg.selectedIcon().isNull()) return;
        m_item.iconIndex = dlg.selectedIndex();
        m_item.iconSource = dlg.sourcePath();
        m_item.icon = dlg.selectedIcon();
        m_item.legacyIcon = dlg.selectedIsLegacy();
    }
    refreshIconPreview();
}

void ItemPropertiesDialog::populateLaunchers(const QString &selected) {
    m_launcherCombo->clear();
    // Every launcher profile that takes this kind of file. There is always at
    // least one, since a launcher keeps the profile it shipped with.
    const QVector<LauncherChoice> choices = launchersFor(m_commandEdit->text().trimmed());
    for (const LauncherChoice &choice : choices) {
        m_launcherCombo->addItem(choice.label, choice.id);
    }

    if (selected.isEmpty()) {
        // Nothing chosen yet: the one its launcher leads with, which is what
        // would have started it anyway.
        m_launcherCombo->setCurrentIndex(0);
        return;
    }

    const int index = m_launcherCombo->findData(selected);
    if (index >= 0) {
        m_launcherCombo->setCurrentIndex(index);
        return;
    }
    // The profile this item was set up with has since been deleted, or the
    // command now points at a file it does not take. Offered anyway, so that
    // saving the dialog does not quietly repoint the item.
    m_launcherCombo->addItem(tr("%1 (no longer set up)").arg(selected), selected);
    m_launcherCombo->setCurrentIndex(m_launcherCombo->count() - 1);
}

void ItemPropertiesDialog::updateScaleRow() {
    // Scaling belongs to the launchers that run Windows programs, and wine's
    // own scaling only to wine, so both the row and the list it offers follow
    // what has just been picked.
    bool scalable = false;
    bool wineDpi = false;
    const QString selected = m_launcherCombo->currentData().toString();
    for (const LauncherChoice &choice : launchersFor(m_commandEdit->text().trimmed())) {
        if (choice.id == selected) {
            scalable = choice.usesWindowScale;
            wineDpi = choice.usesDpiScale;
            break;
        }
    }

    // Rebuilt rather than filtered, and the choice put back where it still
    // exists: moving a wine item to Proton has to drop wine's own scaling.
    const QSignalBlocker blocker(m_scaleCombo);
    m_scaleCombo->clear();
    int selectedIndex = 0;
    const QVector<WindowScaleOption> options = windowScaleOptions(wineDpi);
    for (int i = 0; i < options.size(); ++i) {
        const WindowScaleOption &option = options[i];
        m_scaleCombo->addItem(windowScaleLabel(option), option.percent);
        m_scaleCombo->setItemData(i, option.mode == WindowScaleMode::WineDpi ? 1 : 0,
                                  kScaleModeRole);
        if (option.percent == m_scalePercent && option.mode == m_scaleMode) selectedIndex = i;
    }
    m_scaleCombo->setCurrentIndex(selectedIndex);
    refreshNativeSizeLabel();

    if (scalable != m_scaleRowShown) {
        m_scaleRowShown = scalable;
        m_form->setRowVisible(m_scaleRow, scalable);
        resize(width(), sizeHint().height());
    }
}

void ItemPropertiesDialog::measureWindow() {
    const QString command = m_commandEdit->text().trimmed();
    if (command.isEmpty()) return;

    const Launcher *launcher = nullptr;
    const LauncherRunner *runner = nullptr;
    launcherForItem(command, m_launcherCombo->currentData().toString(), &launcher, &runner);
    if (!launcher || !runner) {
        m_nativeSizeLabel->setText(tr("Nothing is set up to run this"));
        return;
    }

    // Started here rather than through launchProgramItem(), for two reasons:
    // it must run outside gamescope -- inside one it has an X server of its
    // own, where its window cannot be seen -- and it has to be ours to close
    // again once it has been measured.
    const LauncherCommand call = launcherCommandFor(*launcher, *runner, command, {});

    // In the prefix it will be run in, and at the size it will be run at.
    // Without the first it would be measured in whatever wine's default
    // prefix happens to be set to; without the second, a program left scaled
    // by wine's own DPI would measure as the bigger window it is drawing,
    // and that figure would then be handed to gamescope as its own size.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (launcher->usesPrefixes) {
        const QString prefix = protonPrefixFor(command, launcher->prefixesDir);
        if (!QDir().mkpath(prefix)) {
            m_nativeSizeLabel->setText(tr("Could not create its prefix"));
            return;
        }
        env.insert(QStringLiteral("WINEPREFIX"), prefix);

        // A prefix that does not exist yet is built by the first program
        // started in it, and wine puts up windows of its own while doing so
        // -- one of them most of a screen wide. Built here instead, and
        // waited for, so that none of that is still on screen to be measured
        // instead of the program.
        if (!prefixExists(prefix)) {
            m_measureButton->setEnabled(false);
            m_nativeSizeLabel->setText(tr("Setting up its wine prefix, the first time only..."));
            QCoreApplication::processEvents();
            initialisePrefix(runner->command, prefix);
            m_measureButton->setEnabled(true);
        }
        if (launcher->usesDpiScale) {
            m_nativeSizeLabel->setText(tr("Putting its scale back first..."));
            QCoreApplication::processEvents();
            setPrefixDpi(runner->command, prefix, kDefaultLogPixels);
        }
    }

    // What is already on the display, so that whatever the program puts up
    // can be told apart from it.
    const QStringList before = windowSnapshot();

    QProcess program;
    program.setProgram(call.program);
    program.setArguments(call.args);
    program.setWorkingDirectory(QFileInfo(command).absolutePath());
    program.setProcessEnvironment(env);
    program.start();
    if (!program.waitForStarted(5000)) {
        m_nativeSizeLabel->setText(tr("Could not start it"));
        return;
    }

    m_measureButton->setEnabled(false);
    m_nativeSizeLabel->setText(tr("Waiting for its window..."));
    // So the message is on screen while the program is starting up, the wait
    // itself being a blocking one.
    QCoreApplication::processEvents();

    const QSize measured = measureNewWindow(before, wineWindowClass(command), 30000);

    program.terminate();
    if (!program.waitForFinished(3000)) program.kill();

    m_measureButton->setEnabled(true);
    if (measured.isValid() && !measured.isEmpty()) {
        m_nativeSize = measured;
        // Something was learned, so offer to use it: at its own size there
        // would have been nothing to measure for.
        if (m_scaleCombo->currentData().toInt() == 100) m_scaleCombo->setCurrentIndex(1);
    }
    refreshNativeSizeLabel();
    if (!measured.isValid() || measured.isEmpty()) {
        m_nativeSizeLabel->setText(tr("No window appeared -- try running it first"));
    }
}

void ItemPropertiesDialog::refreshNativeSizeLabel() {
    const int percent = m_scaleCombo->currentData().toInt();
    const bool measured = m_nativeSize.isValid() && !m_nativeSize.isEmpty();
    const bool wineDpi = m_scaleCombo->currentData(kScaleModeRole).toInt() == 1;

    // Wine is simply told to draw bigger, so there is nothing to measure --
    // and nothing fixed about the result, which is the point: a window the
    // program lets you resize stays resizable.
    m_measureButton->setEnabled(!wineDpi);
    if (wineDpi) {
        m_nativeSizeLabel->setText(
            tr("The program draws itself bigger, at %1 dpi. Nothing to measure, and a "
               "resizable window stays resizable -- but a program that ignores DPI will "
               "ignore this.")
                .arg(wineLogPixels(percent)));
        return;
    }

    if (!measured) {
        m_nativeSizeLabel->setText(percent > 100 ? tr("Measure it first") : QString());
    } else if (percent > 100) {
        const QSize out = scaledSize(m_nativeSize, percent);
        m_nativeSizeLabel->setText(tr("%1x%2 shown at %3x%4")
                                       .arg(m_nativeSize.width())
                                       .arg(m_nativeSize.height())
                                       .arg(out.width())
                                       .arg(out.height()));
    } else {
        m_nativeSizeLabel->setText(
            tr("%1x%2").arg(m_nativeSize.width()).arg(m_nativeSize.height()));
    }
    // Nothing to scale without gamescope, so say so rather than offering it.
    if (!gamescopeAvailable()) {
        m_scaleCombo->setEnabled(false);
        m_nativeSizeLabel->setText(tr("Scaling needs gamescope, which is not installed"));
    }
}

void ItemPropertiesDialog::updateLauncherVisibility() {
    // The row is there whenever some launcher takes this kind of file, so
    // that what starts an item is readable even where there is only the one
    // profile to start it with.
    const QString chosen = m_launcherCombo->currentData().toString();
    const bool show = !launchersFor(m_commandEdit->text().trimmed()).isEmpty() ||
                      !chosen.isEmpty();

    // The list follows the command, which may now name a different kind of
    // file altogether.
    populateLaunchers(chosen);

    // Read after the list was refilled, which may have moved the selection.
    updateScaleRow();

    if (show == m_launcherRowShown) return;
    m_launcherRowShown = show;
    m_form->setRowVisible(m_launcherCombo, show);
    // The dialog is a row shorter or taller now.
    resize(width(), sizeHint().height());
}

ProgramItem ItemPropertiesDialog::item() const {
    // Everything the dialog does not show -- the icon and the file it came
    // from -- is carried over from the item being edited.
    ProgramItem it = m_item;
    it.name = m_nameEdit->text().trimmed();
    it.command = m_commandEdit->text().trimmed();
    it.arguments = m_argsEdit->text().trimmed();
    it.windowScalePercent = m_scaleCombo->currentData().toInt();
    it.windowScaleMode = m_scaleCombo->currentData(kScaleModeRole).toInt() == 1
                             ? WindowScaleMode::WineDpi
                             : WindowScaleMode::Gamescope;
    it.nativeSize = m_nativeSize;
    it.workingDir = m_workDirEdit->text().trimmed();
    it.launcher = m_launcherCombo->currentData().toString();
    it.xpStyleIcon = m_xpStyleCheck->isChecked();
    return it;
}

void ItemPropertiesDialog::browseCommand() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select Program"), m_commandEdit->text(),
        tr("Programs and documents (*.exe *.EXE *.desktop *.conf *.pdf *.PDF *.cbz *.CBZ);;"
           "Programs (*.exe *.EXE);;Applications (*.desktop);;DOSBox configs (*.conf);;"
           "Documents (*.pdf *.PDF *.cbz *.CBZ);;All Files (*)"));
    if (path.isEmpty()) return;

    m_commandEdit->setText(path);
    QFileInfo info(path);
    if (m_nameEdit->text().isEmpty()) m_nameEdit->setText(info.completeBaseName());
    if (m_workDirEdit->text().isEmpty()) m_workDirEdit->setText(info.absolutePath());

    // The item now points somewhere else, so it wears that file's icon --
    // unless one was picked by hand, which outlives a change of command.
    if (m_item.iconIndex < 0) {
        m_item.icon = QIcon();
        m_item.iconSource.clear();
        m_item.legacyIcon = false;
    }
    refreshIconPreview();
}

void ItemPropertiesDialog::browseWorkingDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"),
                                                      m_workDirEdit->text());
    if (!dir.isEmpty()) m_workDirEdit->setText(dir);
}
