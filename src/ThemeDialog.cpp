#include "ThemeDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

ThemePreview::ThemePreview(QWidget *parent) : QWidget(parent) {
    m_theme = builtinThemes().first();
    setMinimumSize(260, 150);
}

void ThemePreview::setTheme(const Theme &theme) {
    m_theme = theme;
    update();
}

void ThemePreview::paintEvent(QPaintEvent *) {
    QPainter p(this);

    QFont small = font();
    small.setPointSizeF(qMax(6.0, font().pointSizeF() - 2));
    p.setFont(small);
    const int lineHeight = QFontMetrics(small).height() + 2;

    // The desktop the group windows sit on.
    p.fillRect(rect(), m_theme.desktop);

    const QRect frame = rect().adjusted(10, 8, -10, -8);
    p.fillRect(frame, m_theme.buttonFace);
    p.setPen(m_theme.windowText);
    p.drawRect(frame.adjusted(0, 0, -1, -1));

    // Caption bar.
    const QRect caption(frame.x() + 2, frame.y() + 2, frame.width() - 4, lineHeight + 2);
    p.fillRect(caption, m_theme.titleActive);
    p.setPen(m_theme.titleActiveText);
    p.drawText(caption.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
               tr("Program Manager"));

    // Menu strip.
    const QRect menu(frame.x() + 2, caption.bottom() + 1, frame.width() - 4, lineHeight);
    p.fillRect(menu, m_theme.buttonFace);
    p.setPen(m_theme.buttonText);
    p.drawText(menu.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
               tr("File  Options  Window  Help"));

    // Group window interior, with an inactive child window behind it.
    const QRect client(frame.x() + 2, menu.bottom() + 1, frame.width() - 4,
                       frame.bottom() - menu.bottom() - 3);
    p.fillRect(client, m_theme.desktop.darker(105));

    const QRect inactive(client.x() + 10, client.y() + 6, client.width() - 60,
                         client.height() - 24);
    p.fillRect(inactive, m_theme.buttonFace);
    p.setPen(m_theme.windowText);
    p.drawRect(inactive.adjusted(0, 0, -1, -1));
    const QRect inactiveCaption(inactive.x() + 2, inactive.y() + 2, inactive.width() - 4,
                                lineHeight);
    p.fillRect(inactiveCaption, m_theme.titleInactive);
    p.setPen(m_theme.titleInactiveText);
    p.drawText(inactiveCaption.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
               tr("Accessories"));

    const QRect active(client.x() + 34, client.y() + 20, client.width() - 60,
                       client.height() - 26);
    p.fillRect(active, m_theme.buttonFace);
    p.setPen(m_theme.windowText);
    p.drawRect(active.adjusted(0, 0, -1, -1));
    const QRect activeCaption(active.x() + 2, active.y() + 2, active.width() - 4, lineHeight);
    p.fillRect(activeCaption, m_theme.titleActive);
    p.setPen(m_theme.titleActiveText);
    p.drawText(activeCaption.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
               tr("Main"));

    // Icon area, including one selected label so the highlight shows.
    const QRect inner(active.x() + 2, activeCaption.bottom() + 1, active.width() - 4,
                      active.bottom() - activeCaption.bottom() - 3);
    p.fillRect(inner, m_theme.windowBg);

    const int iconSize = 10;
    const int labelWidth = 40;
    const int centreA = inner.x() + 28;
    const int centreB = inner.x() + 76;

    const QRect iconA(centreA - iconSize / 2, inner.y() + 5, iconSize, iconSize);
    const QRect iconB(centreB - iconSize / 2, inner.y() + 5, iconSize, iconSize);
    p.fillRect(iconA, m_theme.titleActive);
    p.fillRect(iconB, m_theme.desktop);
    p.setPen(m_theme.windowText);
    p.drawRect(iconA);
    p.drawRect(iconB);

    // The selected label carries the highlight, hugging its text just as
    // the real icon grid does.
    const QRect labelA(centreA - labelWidth / 2, iconA.bottom() + 3, labelWidth, lineHeight);
    const QRect textA = p.fontMetrics().boundingRect(labelA, Qt::AlignCenter, tr("Setup"));
    p.fillRect(textA.adjusted(-2, -1, 2, 1), m_theme.highlight);
    p.setPen(m_theme.highlightText);
    p.drawText(labelA, Qt::AlignCenter, tr("Setup"));

    const QRect labelB(centreB - labelWidth / 2, iconB.bottom() + 3, labelWidth, lineHeight);
    p.setPen(m_theme.windowText);
    p.drawText(labelB, Qt::AlignCenter, tr("Games"));
}

ThemeDialog::ThemeDialog(const QString &currentTheme, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Colors"));

    m_combo = new QComboBox(this);
    for (const Theme &theme : builtinThemes()) {
        m_combo->addItem(theme.name);
    }
    const int index = m_combo->findText(currentTheme);
    if (index >= 0) m_combo->setCurrentIndex(index);

    m_preview = new ThemePreview(this);
    m_preview->setTheme(themeByName(m_combo->currentText()));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->addRow(tr("Color Scheme:"), m_combo);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Screen Element Preview:"), this));
    layout->addWidget(m_preview, 1);
    layout->addWidget(buttons);

    connect(m_combo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        m_preview->setTheme(themeByName(name));
        emit themePreviewed(name);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ThemeDialog::selectedTheme() const {
    return m_combo->currentText();
}
