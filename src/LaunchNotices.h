#pragma once

#include <QObject>
#include <QString>

// What a launch has to say for itself, on its way to the corner of the
// desktop.
//
// Two things can go wrong with starting a program, and only one of them was
// ever reported: the item that cannot be started at all, which is known
// straight away, and the program that starts and is gone a second later --
// which, being started detached, left nothing anywhere. Its output is kept
// in a file while it runs and read back if it does not survive, so that
// "nothing happened" becomes whatever wine actually printed.
class LaunchNotices : public QObject {
    Q_OBJECT
public:
    static LaunchNotices *instance();

    // Said now: nothing could be started.
    void report(const QString &title, const QString &detail);

    // Watches a program that has just been started detached. Says nothing
    // for a program that is still running a few seconds later, and nothing
    // for one that is gone but printed nothing -- a program may legitimately
    // hand off to another and exit, and silence is how that looks.
    void watch(const QString &itemName, qint64 pid, const QString &logPath);

signals:
    // `logPath` is empty unless there is something in it worth opening.
    void posted(const QString &title, const QString &detail, const QString &logPath);
};

// Where one item's output is kept while it runs: a file of its own under the
// cache directory, written over at each launch. Empty if the directory
// cannot be made, which simply leaves the launch unwatched.
QString launchLogPath(const QString &itemName);
