#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QListWidget;

// Works out an arithmetic expression. Supports + - * / and brackets, a
// leading minus, and a trailing % meaning "of a hundred" -- so "20%" is
// 0.2 and "80 * 20%" is 16.
//
// Exposed so it can be tested without a window: the parsing is the part
// worth being sure about.
struct CalculationResult {
    bool ok = false;
    double value = 0.0;
    QString problem; // what was wrong, when it was not ok
};
CalculationResult calculate(const QString &expression);

// How a result is written out: enough places to be useful, none of the
// trailing zeroes that make a round answer look approximate.
QString formatNumber(double value);

// The sums pane that drops out of the strip, beside the clock's month.
// Type an expression, press Enter, and the answer joins the list above --
// which is the whole of it. Anything more belongs in a calculator.
class CalculatorPane : public QWidget {
    Q_OBJECT
public:
    explicit CalculatorPane(QWidget *parent = nullptr);

    // Opens it under `anchor`, with the cursor already in the entry.
    void popUpAt(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void evaluateEntry();
    // Builds the keypad. The keys do nothing the keyboard cannot, but a
    // calculator with no buttons is a text box wearing a hat.
    QWidget *buildKeys();
    // Puts `what` where the cursor is, which is what every key but the
    // three that do something else amounts to.
    void type(const QString &what);
    // The list and the entry paint their own surfaces, and a popup inherits
    // no palette from a dialog's chrome -- so they are told the scheme
    // rather than left to whatever the platform hands a stray window.
    void wearTheScheme();

    // The keys, so the scheme can be put on them when it changes.
    QVector<QPushButton *> m_keys;
    QLineEdit *m_entry;
    QListWidget *m_history;
};
