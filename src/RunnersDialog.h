#pragma once

#include <QDialog>
#include <QVector>

#include "Runners.h"

class QCheckBox;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTableWidget;

// "Runners": what each kind of file is started with. One runner to a row on
// the left, its settings on the right -- the program, what is handed to it,
// and which file types it takes.
class RunnersDialog : public QDialog {
    Q_OBJECT
public:
    explicit RunnersDialog(QWidget *parent = nullptr);

    // The runners as the dialog leaves them. Only meaningful after OK.
    QVector<Runner> runners() const;

private:
    // The right-hand side for one runner, and the fields on it that are
    // read back when the dialog is accepted.
    struct Page {
        QLineEdit *command;
        QLineEdit *arguments;
        QLineEdit *coreArguments;
        QCheckBox *useShell;
        QLineEdit *extensions;
        QLineEdit *coresDir;
        QTableWidget *cores;
        QLineEdit *prefixesDir;
    };

    QWidget *buildPage(const Runner &runner, Page &page);
    // Puts one runner back the way it shipped, the row being the one on
    // show: the settings are a place to experiment from.
    void restoreDefaults();

    QListWidget *m_list;
    QStackedWidget *m_pages;
    QVector<Runner> m_runners;
    QVector<Page> m_fields;
};
