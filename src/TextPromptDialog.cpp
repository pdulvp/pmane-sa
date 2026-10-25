#include "TextPromptDialog.h"

#include "DialogChrome.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Wide enough for a name with a little room to spare, and no wider: the dialog
// has one field.
constexpr int kPromptWidth = 440;
} // namespace

QString askForText(QWidget *parent, const QString &title, const QString &banner,
                   const QString &label, const QString &initial, bool *ok) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    const DialogChrome chrome = applyDialogChrome(&dialog, banner);

    auto *edit = new QLineEdit(initial, &dialog);
    edit->selectAll();

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(kDialogLabelGap);
    form->setVerticalSpacing(kDialogRowSpacing);
    form->addRow(label, edit);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok)) {
        markDefaultButton(okButton);
        okButton->setDefault(true);
    }

    chrome.fields->addLayout(form);
    chrome.footer->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Return accepts the dialog, as it did in the one this replaces.
    QObject::connect(edit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);

    dialog.resize(kPromptWidth, dialog.sizeHint().height());

    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? edit->text().trimmed() : QString();
}
