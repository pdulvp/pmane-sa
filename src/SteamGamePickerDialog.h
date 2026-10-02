#pragma once

#include <QDialog>
#include <QVector>

#include "ProgramItem.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Every Steam game installed across the library folders, to pick program
// items from. Steam writes a manifest per installed game, and an item points
// at that manifest: the game itself is Steam's to find, so nothing here
// needs to know where it was installed.
class SteamGamePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SteamGamePickerDialog(QWidget *parent = nullptr);

    // The games picked out, ready to go into a group. More than one can be
    // chosen at a time.
    QVector<ProgramItem> chosenItems() const;

private slots:
    // Narrows the list to the games whose name matches what has been typed.
    void applyFilter(const QString &text);

private:
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QLabel *m_summary;
    QPushButton *m_okButton;
};
