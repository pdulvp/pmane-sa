#include "LaunchNotices.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("LaunchNotices", text);
}

// How long a program is given to prove it has started. Past this it is
// running as far as anyone here is concerned, and nothing more is said about
// it: a long-running program is the ordinary case and must not be reported.
constexpr int kWatchMs = 5000;
constexpr int kPollMs = 400;

// At most this much of the output is read back, from the end: a program that
// failed after printing a megabyte of wine diagnostics has the interesting
// part last.
constexpr qint64 kTailBytes = 64 * 1024;

// How much of a line goes in the notice. The rest is in the file, which the
// notice offers to open.
constexpr int kLineChars = 160;

// Whether a program is still there. A detached child is reparented to init,
// which reaps it, so its directory under /proc goes as it exits.
bool stillRunning(qint64 pid) {
    return pid > 0 && QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
}

// The last couple of lines worth reading. Wine says "fixme:" about something
// on nearly every run, and those lines are not why a program failed, so they
// give way to anything else the output holds.
QString interestingTail(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (file.size() > kTailBytes) file.seek(file.size() - kTailBytes);
    const QString text = QString::fromLocal8Bit(file.readAll());

    QStringList plain;
    QStringList noise;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        // wine's own running commentary, which is not news.
        if (trimmed.startsWith(QLatin1String("fixme:")) ||
            trimmed.startsWith(QLatin1String("wine: Call from"))) {
            noise << trimmed;
        } else {
            plain << trimmed;
        }
    }

    QStringList &lines = plain.isEmpty() ? noise : plain;
    if (lines.isEmpty()) return {};
    while (lines.size() > 2) lines.removeFirst();

    QStringList shown;
    for (QString line : lines) {
        if (line.size() > kLineChars) line = line.left(kLineChars - 1) + QChar(0x2026);
        shown << line;
    }
    return shown.join(QLatin1Char('\n'));
}

} // namespace

LaunchNotices *LaunchNotices::instance() {
    static LaunchNotices notices;
    return &notices;
}

void LaunchNotices::report(const QString &title, const QString &detail) {
    emit posted(title, detail, QString());
}

void LaunchNotices::watch(const QString &itemName, qint64 pid, const QString &logPath) {
    if (pid <= 0 || logPath.isEmpty()) return;

    auto *timer = new QTimer(this);
    timer->setInterval(kPollMs);
    // So the watch cannot outlive its welcome if the poll is somehow never
    // satisfied.
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kWatchMs;

    connect(timer, &QTimer::timeout, this, [this, timer, itemName, pid, logPath, deadline] {
        if (stillRunning(pid)) {
            // Up and running: nothing to say, and nothing more to watch for.
            if (QDateTime::currentMSecsSinceEpoch() >= deadline) timer->deleteLater();
            return;
        }
        timer->deleteLater();

        // Gone already. Its output is the whole of what there is to go on --
        // and a program that printed nothing may simply have handed off to
        // another and exited, which is not a failure to report.
        const QString tail = interestingTail(logPath);
        if (tail.isEmpty()) return;

        emit posted(tr("'%1' stopped straight away").arg(itemName), tail, logPath);
    });
    timer->start();
}

QString launchLogPath(const QString &itemName) {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/launches");
    if (!QDir().mkpath(dir)) return {};

    // A file name from the item's own, so that whoever opens the folder can
    // see which log is which.
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]+"));
    QString name = itemName.simplified().replace(unsafe, QStringLiteral("-"));
    while (name.startsWith(QLatin1Char('-'))) name.remove(0, 1);
    if (name.isEmpty()) name = QStringLiteral("item");

    return dir + QLatin1Char('/') + name + QStringLiteral(".log");
}
