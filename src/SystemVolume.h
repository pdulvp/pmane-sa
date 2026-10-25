#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

// The machine's output volume, as the desktop's mixer reports it.
//
// Accessed through wpctl, WirePlumber's command line tool, which is how a
// PipeWire desktop exposes its mixer. There is no library to link and no D-Bus
// interface to call: the session's audio belongs to the session manager, and
// this is how it is queried.

// Whether a mixer is available at all. False on a machine without WirePlumber,
// where no volume control should be offered. Blocking, so it is meant to be
// called once at startup.
bool systemVolumeAvailable();
double systemVolume();
bool systemVolumeMuted();

// The same query without blocking: `tell` is called with the volume and the
// mute state once the mixer replies, and not at all if it never does.
//
// Used wherever the answer is wanted while the desktop is in use: running a
// program and waiting for it blocks everything else for as long as it takes,
// which during a drag is long enough to lose the drag.
void readSystemVolume(QObject *context, std::function<void(double, bool)> tell);

// Set them, without blocking.
void setSystemVolume(double volume);
void setSystemVolumeMuted(bool muted);

// Watches the mixer and reports when anything about it changes: the volume
// keys, the desktop's own slider, headphones being plugged in.
//
// PipeWire reports changes as they happen, so there is no need to poll. Where
// no watcher is available it falls back to polling on a timer, which is what
// this replaced.
class SystemVolumeWatch : public QObject {
    Q_OBJECT
public:
    explicit SystemVolumeWatch(QObject *parent = nullptr);
    // Stops the watcher. Without this it outlives the window that created it,
    // and Qt warns about being torn down around a running process.
    ~SystemVolumeWatch() override;

    // Whether changes are being listened for rather than polled.
    bool listening() const { return m_listening; }

signals:
    void changed();

private:
    QProcess *m_monitor = nullptr;
    QTimer *m_settle = nullptr;
    bool m_listening = false;
};
