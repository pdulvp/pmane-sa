#pragma once

#include <QDialog>
#include <QHash>

#include "DefaultIcons.h"

class QLabel;
class QPushButton;
class QTableWidget;

// "Default Icons": one icon per file type, for the files that carry none of
// their own. A ROM or a disc image holds no artwork, so without this every one
// of them is drawn with the same generic icon.
//
// The types listed are the ones the launchers claim, so a file type added to a
// launcher appears here by itself.
class DefaultIconsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DefaultIconsDialog(QWidget *parent = nullptr);

    // The icons as edited. Only meaningful after OK.
    QHash<QString, DefaultIcon> icons() const { return m_icons; }

private:
    // The "Change Icon" listing of the properties dialog, opened on what the
    // selected type has now.
    void changeIcon();
    // Back to no icon, which leaves the type to the generic fallbacks.
    void clearIcon();
    // Fills one row from `m_icons`: the artwork, and the file it comes from.
    void showRow(int row);
    QString typeAt(int row) const;
    void updateButtons();

    QTableWidget *m_table;
    QPushButton *m_change;
    QPushButton *m_clear;
    QLabel *m_hint;

    QHash<QString, DefaultIcon> m_icons;
};
