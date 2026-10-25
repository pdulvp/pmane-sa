#include "DialogChrome.h"

#include "Theme.h"

#include <QAbstractButton>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

// The coloured strip down the left of a dialog: a band of caption colour with
// the dialog's title written up it, wide enough to stay legible at the type
// size used in the rest of the dialog.
constexpr int kBannerWidth = 36;

// Mixes `weight` of `over` into `under`, which gives a border colour taken from
// the scheme rather than a grey laid over it.
QColor blend(const QColor &under, const QColor &over, qreal weight) {
    const qreal keep = 1.0 - weight;
    return QColor(qRound(under.red() * keep + over.red() * weight),
                  qRound(under.green() * keep + over.green() * weight),
                  qRound(under.blue() * keep + over.blue() * weight));
}

// The strip itself: the caption colour, with its text rotated to read upwards,
// like a book spine.
class EdgeBanner : public QWidget {
public:
    EdgeBanner(const QString &text, QWidget *parent) : QWidget(parent), m_text(text) {
        setFixedWidth(kBannerWidth);
        // Not interactive: the strip is a label, and a drag on it should reach
        // the widget underneath.
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        // Colours come from its palette rather than being stored here, so a
        // dialog previewing a scheme can recolour the whole chrome by setting
        // new palettes on its parts.
        painter.fillRect(rect(), palette().color(QPalette::Window));

        // Small caps with extra letter spacing: the band is a heading, set
        // slightly smaller than the form beside it but still reading as one.
        QFont heading = font();
        if (heading.pointSizeF() > 0) {
            heading.setPointSizeF(std::max(7.0, heading.pointSizeF() * 0.92));
        } else {
            heading.setPixelSize(std::max(9, int(heading.pixelSize() * 0.92)));
        }
        heading.setBold(true);
        heading.setCapitalization(QFont::AllUppercase);
        heading.setLetterSpacing(QFont::PercentageSpacing, 140);

        painter.setFont(heading);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.rotate(-90);
        painter.drawText(QRectF(-height() / 2.0, -width() / 2.0, height(), width()),
                         Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
};

// Fields with a border and some padding, and the caption colour to mark the one
// with focus. Written with the scheme's colours so a dialog follows Options >
// Colors like everything else.
QString styleSheetFor(const Theme &theme, const QColor &edge) {
    return QStringLiteral(R"(
        QLineEdit, QComboBox, QListWidget, QAbstractScrollArea {
            background: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 3px;
            selection-background-color: %4;
            selection-color: %5;
        }
        QLineEdit, QComboBox { padding: 5px 7px; }
        QLineEdit:focus, QComboBox:focus, QListWidget:focus { border: 1px solid %4; }
        /* Cells with a picture and no caption: without this the selection is
           a block of colour under an icon rather than around it. */
        QListView::item { border: 1px solid transparent; border-radius: 3px; }
        QListView::item:selected { background: %4; border: 1px solid %4; }
        QListView::item:hover { border: 1px solid %3; }
        QLineEdit:disabled, QComboBox:disabled { color: %3; }
        QPushButton {
            background: %6;
            color: %7;
            border: 1px solid %3;
            border-radius: 3px;
            padding: 5px 14px;
        }
        QPushButton:hover { border: 1px solid %4; }
        QPushButton:pressed { background: %8; }
        QPushButton:disabled { color: %3; }
        QPushButton[dialogDefault="true"] { border: 2px solid %4; font-weight: bold; }
        QLabel[dialogHint="true"] { color: %9; }
        QWidget#previewWell { border: 1px solid %3; border-radius: 3px; }
        QLabel#iconWell {
            background: %1;
            border: 1px solid %3;
            border-radius: 3px;
        }
    )")
        .arg(theme.windowBg.name(), theme.windowText.name(), edge.name(),
             theme.titleActive.name(), theme.titleActiveText.name(), theme.buttonFace.name(),
             theme.buttonText.name(), blend(theme.buttonFace, theme.windowText, 0.12).name(),
             blend(theme.windowBg, theme.windowText, 0.55).name());
}

// The object names of the chrome's parts, so recolouring can find them again
// without the dialog keeping references.
const char *kBannerName = "dialogBanner";
const char *kBodyName = "dialogBody";
const char *kFooterName = "dialogFooter";
const char *kRuleName = "dialogRule";

// Applies the scheme to every part of the chrome: the two coloured surfaces,
// the rule between them, the strip, and the style sheet the fields and buttons
// are drawn with.
void colourChrome(QDialog *dialog, const Theme &theme) {
    // The line between a field and the surface it sits on, mixed from the
    // scheme's own text colour rather than a fixed grey: subtle on a pale theme
    // and clear on a strong one, but always a colour from the theme.
    const QColor edge = blend(theme.windowBg, theme.windowText, 0.30);

    // The style sheet is set first. Setting one again makes the style unpolish
    // the whole dialog and restore the palette every widget had at the first
    // polish, so palettes set before this call would be discarded and the
    // banner and footer would keep their original scheme.
    dialog->setStyleSheet(styleSheetFor(theme, edge));

    if (QWidget *banner = dialog->findChild<QWidget *>(QLatin1String(kBannerName))) {
        QPalette pal = banner->palette();
        pal.setColor(QPalette::Window, theme.titleActive);
        pal.setColor(QPalette::WindowText, theme.titleActiveText);
        banner->setPalette(pal);
        banner->update();
    }
    // The form sits on the scheme's window colour rather than the button grey
    // the platform would give a dialog.
    if (QWidget *body = dialog->findChild<QWidget *>(QLatin1String(kBodyName))) {
        QPalette pal = body->palette();
        pal.setColor(QPalette::Window, theme.windowBg);
        pal.setColor(QPalette::WindowText, theme.windowText);
        body->setPalette(pal);
    }
    if (QWidget *footer = dialog->findChild<QWidget *>(QLatin1String(kFooterName))) {
        QPalette pal = footer->palette();
        pal.setColor(QPalette::Window, theme.buttonFace);
        pal.setColor(QPalette::WindowText, theme.buttonText);
        footer->setPalette(pal);
    }
    if (QWidget *rule = dialog->findChild<QWidget *>(QLatin1String(kRuleName))) {
        rule->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(edge.name()));
    }
}

} // namespace

DialogChrome applyDialogChrome(QDialog *dialog, const QString &bannerText) {
    auto *fields = new QWidget(dialog);
    auto *fieldsLayout = new QVBoxLayout(fields);
    fieldsLayout->setContentsMargins(kDialogMargin, kDialogMargin, kDialogMargin, kDialogMargin);
    fieldsLayout->setSpacing(kDialogRowSpacing);

    // A rule, then the buttons in a band of the button colour, separating the
    // form from the actions.
    auto *rule = new QFrame(dialog);
    rule->setObjectName(QLatin1String(kRuleName));
    rule->setFrameShape(QFrame::HLine);
    rule->setFixedHeight(1);

    auto *footer = new QWidget(dialog);
    footer->setObjectName(QLatin1String(kFooterName));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(kDialogMargin, 12, kDialogMargin, 12);
    footer->setAutoFillBackground(true);

    auto *body = new QWidget(dialog);
    body->setObjectName(QLatin1String(kBodyName));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(fields);
    bodyLayout->addWidget(rule);
    bodyLayout->addWidget(footer);
    body->setAutoFillBackground(true);

    auto *banner = new EdgeBanner(bannerText, dialog);
    banner->setObjectName(QLatin1String(kBannerName));

    auto *outer = new QHBoxLayout(dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(banner);
    outer->addWidget(body);

    colourChrome(dialog, currentTheme());

    return DialogChrome{fieldsLayout, footerLayout};
}

void refreshDialogChrome(QDialog *dialog) {
    if (dialog) colourChrome(dialog, currentTheme());
}

void markDefaultButton(QAbstractButton *button) {
    if (!button) return;
    button->setProperty("dialogDefault", true);
}

void markHintLabel(QWidget *label) {
    if (!label) return;
    label->setProperty("dialogHint", true);
}
