#pragma once

#include <QDialog>
#include <QVector>

#include "ProgramItem.h"

class QLineEdit;
class QListWidget;
class QPushButton;

// Every application installed on the machine, to create program items from: the
// same list the desktop's own menu is built from, read from the .desktop files
// under the applications directories.
class ApplicationPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApplicationPickerDialog(QWidget *parent = nullptr);

    // The applications selected, ready to be added to a group. More than one
    // can be chosen at a time.
    QVector<ProgramItem> chosenItems() const;

private slots:
    // Narrows the list to the applications whose name or file matches the text
    // typed.
    void applyFilter(const QString &text);

private:
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QPushButton *m_okButton;
};
