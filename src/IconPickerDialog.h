#pragma once

#include <QDialog>
#include <QVector>

#include "PeIconExtractor.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Progman's "Change Icon": the icons one file holds, laid out to pick from. An
// .exe or .dll often embeds several, and an .ico carries one image per size and
// colour depth.
class IconPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit IconPickerDialog(QWidget *parent = nullptr);

    // Opens on the icons `path` holds, with `index` preselected (-1 for none, as
    // for an item still using its automatically derived icon).
    void setSource(const QString &path, int index);

    QString sourcePath() const;
    int selectedIndex() const;
    QIcon selectedIcon() const;
    // .ico and 16-bit (NE) artwork is the flat, low-colour kind the XP-style
    // rendering is meant for.
    bool selectedIsLegacy() const;
    // The user asked to go back to the item's automatically derived icon.
    bool isReset() const { return m_reset; }

private slots:
    void browse();
    // Re-reads the file named in the path field and lists what it holds.
    void reload();

private:
    int currentRow() const;

    QLineEdit *m_pathEdit;
    QListWidget *m_list;
    QLabel *m_status;
    QPushButton *m_okButton;
    QVector<PeIconExtractor::IconInfo> m_icons;
    PeIconExtractor::Source m_source = PeIconExtractor::Source::None;
    bool m_reset = false;
};
