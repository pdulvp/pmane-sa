#pragma once

#include <QString>

class QWidget;

// A one-field dialog: a label, a text field, and OK or Cancel. What
// QInputDialog::getText() provides, in the dialog chrome the rest of the
// application uses.
//
// `banner` is the text written up the coloured strip; `title` names the window.
// Returns the text trimmed of surrounding space, and sets `ok` to false when the
// dialog was cancelled.
QString askForText(QWidget *parent, const QString &title, const QString &banner,
                   const QString &label, const QString &initial, bool *ok);
