#pragma once

#include <QString>

class QAbstractButton;
class QDialog;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

// The look the dialogs share: a strip of caption colour down the left edge with
// the dialog's title written up it, the fields on the scheme's window colour,
// and the buttons in a band along the bottom.
//
// A dialog is a top-level window, so it inherits none of the palette the
// desktop gives its group windows; the colours come from currentTheme()
// instead, which is what keeps these dialogs following Options > Colors.

// Spacing around the fields, a dialog being a form to read rather than a dense
// control panel.
inline constexpr int kDialogMargin = 18;
inline constexpr int kDialogRowSpacing = 10;
inline constexpr int kDialogLabelGap = 14;

// Where a dialog puts its own widgets: fields in one layout, buttons in the
// other. Both are already positioned and coloured.
struct DialogChrome {
    QVBoxLayout *fields;
    QHBoxLayout *footer;
};

// Styles `dialog` and returns the two layouts to fill. It takes over the
// dialog's top-level layout, so call it before adding anything else.
DialogChrome applyDialogChrome(QDialog *dialog, const QString &bannerText);

// Applies the current scheme to a dialog already styled, which the Colors
// dialog needs as it previews one scheme after another on itself.
void refreshDialogChrome(QDialog *dialog);

// Highlights the button that completes the dialog. The chrome's style sheet
// does the drawing; this only marks the button for it.
void markDefaultButton(QAbstractButton *button);

// Marks a label as explanatory rather than a field label: the scheme's text
// colour, lightened, so it reads as an aside.
void markHintLabel(QWidget *label);
