#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QVBoxLayout;

// The corner of the desktop where the Program Manager reports what happened
// without interrupting: a program that would not start, a prefix that could not
// be created. A message box would demand an answer, and a failed launch is not
// a question.
//
// A child of the window it reports in, keeping itself in the bottom right above
// the group windows. It follows the current scheme, as the dialogs do.
class NotificationArea : public QWidget {
    Q_OBJECT
public:
    explicit NotificationArea(QWidget *parent);

    // One notice: a line saying what happened, a line or two of detail below
    // it, and, where there is one, the file holding the program's output, which
    // the notice offers to open.
    void post(const QString &title, const QString &detail, const QString &logPath = QString());

    // Applies the current scheme to the notices on screen, as the dialogs are
    // re-themed when Options > Colors changes.
    void applyTheme();

    // Moves it back into the corner. Needed from outside when the desktop
    // scrolls: scrolling moves every child of the viewport, and this is not
    // part of what is being scrolled.
    void updatePosition();

protected:
    // Repositioned when the parent is resized as well as when this grows, since
    // the corner depends on both.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // One notice as a widget, so it can be dismissed on its own.
    QWidget *buildNotice(const QString &title, const QString &detail, const QString &logPath);
    void dress(QWidget *notice);
    void dismiss(QWidget *notice);

    QVBoxLayout *m_stack;
    // Newest last, which is nearest the corner. Only a few are kept: a column
    // of notices up the side of the desktop would be a log, which this is not.
    QVector<QWidget *> m_notices;
};
