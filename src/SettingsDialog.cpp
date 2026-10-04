#include "SettingsDialog.h"

#include "DialogChrome.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(bool useDesktopWallpaper, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));

    const DialogChrome chrome = applyDialogChrome(this, tr("Settings"));

    m_wallpaper = new QCheckBox(tr("Use the &desktop's wallpaper as the background"), this);
    m_wallpaper->setChecked(useDesktopWallpaper);

    auto *hint = new QLabel(tr("Shows the picture GNOME is using behind the group windows, "
                               "and follows it whenever the desktop's wallpaper changes. "
                               "Unchecked, the background is the color scheme's."),
                            this);
    hint->setWordWrap(true);
    markHintLabel(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(ok);
        ok->setDefault(true);
    }

    chrome.fields->addWidget(m_wallpaper);
    chrome.fields->addWidget(hint);
    chrome.fields->addStretch(1);
    chrome.footer->addWidget(buttons);

    connect(m_wallpaper, &QCheckBox::toggled, this,
            &SettingsDialog::desktopWallpaperPreviewed);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool SettingsDialog::useDesktopWallpaper() const {
    return m_wallpaper->isChecked();
}
