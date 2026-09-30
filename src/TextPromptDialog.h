#pragma once

#include <QString>

class QWidget;

// A one-field dialog: a label, a line to type on, and OK or Cancel. What
// QInputDialog::getText() does, in the dialog chrome the rest of the
// application wears.
//
// `banner` is the word written up the coloured strip; `title` names the
// window. Returns the text with the space trimmed off it, and sets `ok` to
// false when the dialog was cancelled.
QString askForText(QWidget *parent, const QString &title, const QString &banner,
                   const QString &label, const QString &initial, bool *ok);
