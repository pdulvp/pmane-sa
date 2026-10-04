#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QVBoxLayout;

// The corner of the desktop where the Program Manager says what happened
// without stopping anyone: a program that would not start, a prefix that
// could not be built. A message box demands an answer to something nobody
// asked a question about, and a launch that fails is not a question.
//
// Lives as a child of the window it reports in and keeps itself in the
// bottom right of it, above the group windows. It wears the current scheme,
// like the dialogs do.
class NotificationArea : public QWidget {
    Q_OBJECT
public:
    explicit NotificationArea(QWidget *parent);

    // One notice: a line saying what happened, a line or two of detail
    // under it, and -- where there is one -- a file holding whatever the
    // program had to say for itself, which the notice offers to open.
    void post(const QString &title, const QString &detail, const QString &logPath = QString());

    // Puts the scheme that is current now on the notices on show, as the
    // dialogs are redressed when Options > Colors changes.
    void applyTheme();

    // Back into the corner. Needed from outside when the desktop scrolls:
    // scrolling moves everything the viewport holds, and this corner is not
    // part of what is being scrolled.
    void updatePosition();

protected:
    // Repositioned from its parent's resizing as well as its own growing:
    // the corner it sits in moves with both.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // One notice as a widget, so that it can be dismissed on its own.
    QWidget *buildNotice(const QString &title, const QString &detail, const QString &logPath);
    void dress(QWidget *notice);
    void dismiss(QWidget *notice);

    QVBoxLayout *m_stack;
    // Newest last, which is nearest the corner. Only a few are kept: a
    // column of notices up the side of the desktop is a log, and a log is
    // not what a corner is for.
    QVector<QWidget *> m_notices;
};
