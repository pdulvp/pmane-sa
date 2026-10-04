#include "SettingsDialog.h"

#include "DialogChrome.h"
#include "Theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// A tile is square-ish and roomy: it is a thing to recognise at a glance,
// not a word to read.
constexpr int kTileWidth = 92;
constexpr int kTileHeight = 76;
constexpr int kTileIcon = 24;

// The air between a heading and what it heads, and between one section and
// the next.
constexpr int kHeadingGap = 6;
constexpr int kSectionGap = 20;

} // namespace

SettingsDialog::SettingsDialog(bool useDesktopWallpaper, bool autostart,
                               const KioskSettings &kiosk, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));

    const DialogChrome chrome = applyDialogChrome(this, tr("Settings"));

    m_wallpaper = switchRow(tr("Use the desktop's wallpaper as the background"),
                            useDesktopWallpaper);
    // A machine that boots into this is a machine that starts it without
    // being asked, which is why this sits with the lock rather than with the
    // wallpaper.
    m_autostart = switchRow(tr("Start when you log in"), autostart);
    m_locked = switchRow(tr("Keep the window full screen"), kiosk.locked);

    m_calendar = tile({QStringLiteral("office-calendar-symbolic"),
                       QStringLiteral("x-office-calendar-symbolic"),
                       QStringLiteral("x-office-calendar")},
                      tr("Calendar"), kiosk.showCalendar);
    m_volume = tile({QStringLiteral("audio-volume-high-symbolic"),
                     QStringLiteral("audio-volume-high")},
                    tr("Sound"), kiosk.showVolume);
    m_calculator = tile({QStringLiteral("accessories-calculator-symbolic"),
                         QStringLiteral("accessories-calculator")},
                        tr("Calculator"), kiosk.showCalculator);
    m_search = tile({QStringLiteral("system-search-symbolic"),
                     QStringLiteral("edit-find-symbolic"), QStringLiteral("system-search")},
                    tr("Search"), kiosk.showSearch);

    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(10);
    tiles->addWidget(m_calendar);
    tiles->addWidget(m_volume);
    tiles->addWidget(m_calculator);
    tiles->addWidget(m_search);
    tiles->addStretch();

    chrome.fields->addWidget(sectionHeading(tr("Desktop")));
    chrome.fields->addSpacing(kHeadingGap);
    chrome.fields->addWidget(m_wallpaper);

    chrome.fields->addSpacing(kSectionGap);
    chrome.fields->addWidget(sectionHeading(tr("Kiosk")));
    chrome.fields->addSpacing(kHeadingGap);
    chrome.fields->addWidget(m_autostart);
    chrome.fields->addWidget(m_locked);
    chrome.fields->addSpacing(kSectionGap - kHeadingGap);
    chrome.fields->addWidget(sectionHeading(tr("Shown along the top")));
    chrome.fields->addSpacing(kHeadingGap);
    chrome.fields->addLayout(tiles);
    chrome.fields->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(ok);
        ok->setDefault(true);
    }
    chrome.footer->addWidget(buttons);

    connect(m_wallpaper, &QCheckBox::toggled, this,
            &SettingsDialog::desktopWallpaperPreviewed);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(480, sizeHint().height());
}

QWidget *SettingsDialog::sectionHeading(const QString &text) {
    auto *heading = new QWidget(this);
    auto *rows = new QVBoxLayout(heading);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(4);

    auto *label = new QLabel(text, heading);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    QPalette ink = label->palette();
    // The caption colour, which is what marks the dialog as this
    // application's: the banner down the side is the same colour.
    ink.setColor(QPalette::WindowText, currentTheme().titleActive);
    label->setPalette(ink);
    rows->addWidget(label);

    // A rule the width of the form, which is what separates one group of
    // switches from the next without a word being spent on it.
    auto *rule = new QFrame(heading);
    rule->setFrameShape(QFrame::HLine);
    rule->setFixedHeight(1);
    QPalette pal = rule->palette();
    const QColor face = currentTheme().buttonFace;
    pal.setColor(QPalette::WindowText, darkMode() ? face.lighter(140) : face.darker(118));
    rule->setPalette(pal);
    rows->addWidget(rule);
    return heading;
}

QCheckBox *SettingsDialog::switchRow(const QString &text, bool on) {
    auto *box = new QCheckBox(text, this);
    box->setChecked(on);
    // Told the scheme's ink rather than left to inherit it: a platform theme
    // hands a checkbox a palette of its own, which outranks the one the
    // dialog's body is wearing.
    QPalette ink = box->palette();
    ink.setColor(QPalette::WindowText, currentTheme().windowText);
    ink.setColor(QPalette::Text, currentTheme().windowText);
    box->setPalette(ink);
    return box;
}

QToolButton *SettingsDialog::tile(const QStringList &iconNames, const QString &text, bool on) {
    auto *button = new QToolButton(this);
    button->setCheckable(true);
    button->setChecked(on);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::TabFocus);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setText(text);
    button->setFixedSize(kTileWidth, kTileHeight);
    button->setIconSize(QSize(kTileIcon, kTileIcon));
    button->setCursor(Qt::PointingHandCursor);

    // Drawn here rather than left to the style: a checkable tool button
    // pressed in reads as a button being held down, where what is wanted is
    // a thing that is either on the strip or not. Set on the button itself
    // rather than added to the dialog's sheet, which the chrome owns.
    const Theme &theme = currentTheme();
    const QColor face = theme.buttonFace;
    const QColor edge = darkMode() ? face.lighter(140) : face.darker(125);
    const QColor on_ = theme.titleActive;
    button->setStyleSheet(
        QStringLiteral("QToolButton { border: 1px solid %1; border-radius: 8px;"
                       " padding: 6px 4px; color: %2; background: %3; }"
                       "QToolButton:hover { border-color: %4; }"
                       "QToolButton:checked { border: 2px solid %4; color: %2;"
                       " background: rgba(%5, %6, %7, 40); }")
            .arg(edge.name(), theme.windowText.name(), theme.windowBg.name(), on_.name())
            .arg(on_.red())
            .arg(on_.green())
            .arg(on_.blue()));

    // In the dialog's own ink, as the strip's icons are in the strip's.
    const QIcon icon = tintedIcon(iconNames, theme.windowText);
    if (!icon.isNull()) button->setIcon(icon);
    return button;
}

bool SettingsDialog::useDesktopWallpaper() const { return m_wallpaper->isChecked(); }

bool SettingsDialog::autostart() const { return m_autostart->isChecked(); }

KioskSettings SettingsDialog::kiosk() const {
    KioskSettings settings;
    settings.locked = m_locked->isChecked();
    settings.showCalendar = m_calendar->isChecked();
    settings.showVolume = m_volume->isChecked();
    settings.showCalculator = m_calculator->isChecked();
    settings.showSearch = m_search->isChecked();
    return settings;
}
