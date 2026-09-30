#pragma once

#include <QString>

class QAbstractButton;
class QDialog;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

// The look the dialogs share: a strip of caption colour down the left-hand
// edge with the dialog's business written up it, the fields on the scheme's
// window colour, and the buttons in a band of their own along the bottom.
//
// A dialog is a window of its own, so it inherits none of the palette the
// desktop hands its group windows: the colours come from currentTheme()
// instead, which is what keeps these dialogs following Options > Colors.

// Room the fields breathe in: a dialog is a form to be read, not a control
// panel to be packed.
inline constexpr int kDialogMargin = 18;
inline constexpr int kDialogRowSpacing = 10;
inline constexpr int kDialogLabelGap = 14;

// Where a dialog puts its own widgets: its fields go in the one, its buttons
// in the other. Both layouts are already laid out and coloured.
struct DialogChrome {
    QVBoxLayout *fields;
    QHBoxLayout *footer;
};

// Dresses `dialog` and returns the two layouts to fill. Takes over the
// dialog's top-level layout, so call it before anything else is added.
DialogChrome applyDialogChrome(QDialog *dialog, const QString &bannerText);

// Puts the scheme that is current now on a dialog already dressed, which is
// what the Colors dialog needs as it previews one scheme after another on
// itself along with everything else.
void refreshDialogChrome(QDialog *dialog);

// Draws the eye to the button that finishes the dialog. The chrome's style
// sheet does the work; this is what marks the button for it.
void markDefaultButton(QAbstractButton *button);

// A line of explanation rather than a label on something: set in the
// scheme's ink, lightened, so it reads as an aside.
void markHintLabel(QWidget *label);
