#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QSocketNotifier;
class QTimer;

// Watches the kernel's mount table for a new mount, so that icons whose files
// live on a drive that was not mounted yet can be read once it is.
//
// Event-driven, not polled: /proc/self/mountinfo supports poll() and wakes
// once per mount or unmount, and a QSocketNotifier turns that wake-up into an
// event in the Qt event loop. Nothing runs unless the kernel reports a change.
class MountWatch : public QObject {
    Q_OBJECT
public:
    explicit MountWatch(QObject *parent = nullptr);
    ~MountWatch() override;

signals:
    // A mount point appeared that was not there before. Unmounts do not emit:
    // an icon already read from a drive stays until the next refresh.
    void mounted();

private:
    void onWake();

    int m_fd = -1;
    // Exception maps to POLLPRI, which is what the kernel's mount table
    // reports on a change; POLLIN would be set at all times on this seq file
    // and the notifier would spin.
    QSocketNotifier *m_notifier = nullptr;
    // Coalesces the burst of wake-ups one mount can produce into one signal.
    // Started only by a wake-up, never on its own.
    QTimer *m_settle = nullptr;
    QSet<QString> m_mounts;
};
