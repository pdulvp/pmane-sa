#pragma once

#include <QObject>
#include <QString>

// What a launch reports, on its way to the corner of the desktop.
//
// Two things can go wrong when starting a program, and only one used to be
// reported: an item that cannot be started at all, which is known immediately,
// and a program that starts and exits a second later, which, being started
// detached, left no trace. Its output is kept in a file while it runs and read
// back if it does not survive, so "nothing happened" becomes whatever wine
// actually printed.
class LaunchNotices : public QObject {
    Q_OBJECT
public:
    static LaunchNotices *instance();

    // Reports immediately that nothing could be started.
    void report(const QString &title, const QString &detail);

    // Watches a program just started detached. Reports nothing for a program
    // still running a few seconds later, and nothing for one that exited
    // without printing anything: a program may legitimately hand off to another
    // and exit.
    void watch(const QString &itemName, qint64 pid, const QString &logPath);

signals:
    // `logPath` is empty unless the file holds something worth opening.
    void posted(const QString &title, const QString &detail, const QString &logPath);
};

// Where one item's output is kept while it runs: a file of its own in the cache
// directory, truncated at each launch. Empty when the directory cannot be
// created, which leaves the launch unwatched.
QString launchLogPath(const QString &itemName, const QString &itemId);
