#pragma once

#include <QDialog>
#include <QVector>

#include "ProgramItem.h"

class QLineEdit;
class QListWidget;
class QPushButton;

// Every application installed on the machine, to pick program items from --
// the same list the desktop's own menu is built out of, read from the
// .desktop files under the applications directories.
class ApplicationPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApplicationPickerDialog(QWidget *parent = nullptr);

    // The applications picked out, ready to go into a group. More than one
    // can be chosen at a time.
    QVector<ProgramItem> chosenItems() const;

private slots:
    // Narrows the list to the applications whose name or file matches what
    // has been typed.
    void applyFilter(const QString &text);

private:
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QPushButton *m_okButton;
};
