#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QListWidget;

// Evaluates an arithmetic expression. Supports + - * / and brackets, a leading
// minus, and a trailing % meaning "per hundred", so "20%" is 0.2 and
// "80 * 20%" is 16.
//
// Exposed so the parsing can be tested without a window.
struct CalculationResult {
    bool ok = false;
    double value = 0.0;
    QString problem; // what was wrong, when ok is false
};
CalculationResult calculate(const QString &expression);

// Formats a result: enough decimal places to be useful, with no trailing zeroes
// that would make a round answer look approximate.
QString formatNumber(double value);

// The calculator pane that drops out of the strip, beside the clock's month.
// Type an expression, press Enter, and the answer is added to the list above.
// That is all it does; anything more belongs in a calculator application.
class CalculatorPane : public QWidget {
    Q_OBJECT
public:
    explicit CalculatorPane(QWidget *parent = nullptr);

    // Opens it under `anchor`, with the cursor in the entry field.
    void popUpAt(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void evaluateEntry();
    // Builds the keypad. The keys do nothing the keyboard cannot, but a
    // calculator is expected to have them.
    QWidget *buildKeys();
    // Inserts `what` at the cursor, which is what all but three of the keys do.
    void type(const QString &what);
    // The list and the entry paint their own surfaces, and a popup inherits no
    // palette from the dialog chrome, so the scheme is applied explicitly
    // rather than left to the platform default.
    void wearTheScheme();

    // The keys, kept so the scheme can be applied to them when it changes.
    QVector<QPushButton *> m_keys;
    QLineEdit *m_entry;
    QListWidget *m_history;
};
