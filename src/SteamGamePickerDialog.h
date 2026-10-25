#pragma once

#include <QDialog>
#include <QVector>

#include "ProgramItem.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Every Steam game installed across the library folders, to create program
// items from. Steam writes one manifest per installed game and an item points
// at that manifest, so nothing here needs to know where the game was installed.
class SteamGamePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SteamGamePickerDialog(QWidget *parent = nullptr);

    // The games selected, ready to be added to a group. More than one can be
    // chosen at a time.
    QVector<ProgramItem> chosenItems() const;

private slots:
    // Narrows the list to the games whose name matches the text typed.
    void applyFilter(const QString &text);

private:
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QLabel *m_summary;
    QPushButton *m_okButton;
};
