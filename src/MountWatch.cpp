#include "MountWatch.h"

#include <QFile>
#include <QSocketNotifier>
#include <QTimer>

#include <fcntl.h>
#include <unistd.h>

namespace {

const char *kMountInfo = "/proc/self/mountinfo";

// The mount points in the kernel's mount table, as raw strings. One line per
// mount; the mount point is the fifth space-separated field, with spaces and
// other special characters octal-escaped (\040 etc.), so splitting on spaces
// is safe. The escaped form is kept as-is: it is only ever compared with
// itself.
QSet<QString> readMountPoints() {
    QSet<QString> points;
    QFile file(QString::fromLatin1(kMountInfo));
    if (!file.open(QIODevice::ReadOnly)) return points;
    // readAll() rather than a readLine() loop: a proc file reports a size of
    // zero, so atEnd() is true from the start and the loop would read nothing.
    const QByteArray all = file.readAll();
    for (const QByteArray &line : all.split('\n')) {
        int field = 0;
        int start = 0;
        for (int i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line.at(i) == ' ') {
                if (++field == 5) {
                    points.insert(QString::fromUtf8(line.mid(start, i - start)));
                    break;
                }
                start = i + 1;
            }
        }
    }
    return points;
}

} // namespace

MountWatch::MountWatch(QObject *parent) : QObject(parent) {
    m_mounts = readMountPoints();

    // The mount table is a seq file whose poll() blocks until a mount or
    // unmount and then reports the change. The Exception notifier asks for
    // POLLPRI, which keeps poll() blocking: asking for POLLIN instead would
    // wake at once and forever, since a seq file always reports readable.
    // Were the file missing, there is simply nothing to watch and no signal
    // is ever sent.
    m_fd = ::open(kMountInfo, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) return;

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Exception, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &MountWatch::onWake);

    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    m_settle->setInterval(400);
    connect(m_settle, &QTimer::timeout, this, [this] {
        const QSet<QString> now = readMountPoints();
        const bool added = !(now - m_mounts).isEmpty();
        m_mounts = now;
        if (added) emit mounted();
    });
}

MountWatch::~MountWatch() {
    if (m_fd >= 0) ::close(m_fd);
}

void MountWatch::onWake() {
    if (m_fd < 0) return;
    // Disabled while the descriptor is drained, so the read cannot re-enter
    // the handler. Reading the seq file to its end is also what rearms the
    // kernel's poll, so the wake-up is not delivered over and over.
    if (m_notifier) m_notifier->setEnabled(false);
    char buf[4096];
    while (::read(m_fd, buf, sizeof(buf)) > 0) {}
    if (m_notifier) m_notifier->setEnabled(true);
    if (m_settle) m_settle->start();
}
