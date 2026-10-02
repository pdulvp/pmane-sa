#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

// The machine's own output volume, as the desktop's mixer sees it.
//
// Spoken to through wpctl, WirePlumber's command line, which is what a
// PipeWire desktop keeps its mixer behind. There is no library to link and
// no D-Bus interface to call: the session's sound is the session manager's,
// and this is the way it is asked.

// Whether there is a mixer to talk to at all. False on a machine without
// WirePlumber, where no volume control should be offered. Blocking, and so
// for asking once at startup rather than over and over.
bool systemVolumeAvailable();
double systemVolume();
bool systemVolumeMuted();

// The same question without waiting for the answer: `tell` is called with
// the volume and whether it is muted, once the mixer has replied. Nothing
// is called if it never does.
//
// Asked this way wherever the answer is wanted while the desktop is in use:
// running a program and waiting for it stops everything else for as long as
// it takes, which during a drag is long enough to lose the drag.
void readSystemVolume(QObject *context, std::function<void(double, bool)> tell);

// Sets them, without waiting.
void setSystemVolume(double volume);
void setSystemVolumeMuted(bool muted);

// Watches the mixer and says when something about it has changed -- the
// volume keys, the desktop's own slider, headphones going in.
//
// PipeWire reports its changes as they happen, so there is no need to ask
// every few seconds whether anything has. Where there is nothing to watch
// with, it falls back to asking on a timer, which is what this replaced.
class SystemVolumeWatch : public QObject {
    Q_OBJECT
public:
    explicit SystemVolumeWatch(QObject *parent = nullptr);
    // Stops the watcher. Without this it outlives the window that wanted
    // it, and Qt is right to complain about being torn down around a
    // process that is still running.
    ~SystemVolumeWatch() override;

    // Whether changes are being listened for rather than asked after.
    bool listening() const { return m_listening; }

signals:
    void changed();

private:
    QProcess *m_monitor = nullptr;
    QTimer *m_settle = nullptr;
    bool m_listening = false;
};
