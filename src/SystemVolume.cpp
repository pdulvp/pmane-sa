#include "SystemVolume.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

// Whatever wpctl calls the output everything plays through, rather than a
// particular sound card: the desktop may move it while this is running.
const char *kSink = "@DEFAULT_AUDIO_SINK@";

// PipeWire's own change reporter. It prints as things happen, which is the
// point: nothing here has to ask.
const char *kMonitor = "pw-mon";

// A burst of changes arrives as one thing is done -- a key held down, a
// slider dragged -- so the mixer is asked once the burst has stopped.
constexpr int kSettleMs = 120;
// What the watch falls back to where there is nothing to watch with.
constexpr int kPollMs = 3000;

QString wpctl() {
    static const QString path = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    return path;
}

// wpctl answers "Volume: 0.36", and "Volume: 0.36 [MUTED]" when it is off.
double volumeIn(const QString &answer) {
    static const QRegularExpression level(QStringLiteral("Volume:\\s*([0-9]*\\.?[0-9]+)"));
    const QRegularExpressionMatch match = level.match(answer);
    return match.hasMatch() ? match.captured(1).toDouble() : -1.0;
}

QString ask() {
    if (wpctl().isEmpty()) return {};
    QProcess probe;
    probe.setProgram(wpctl());
    probe.setArguments({QStringLiteral("get-volume"), QLatin1String(kSink)});
    probe.start();
    if (!probe.waitForFinished(2000)) {
        probe.kill();
        return {};
    }
    return QString::fromUtf8(probe.readAllStandardOutput());
}

// Started and left to finish on its own: nothing here needs to know when a
// one-word instruction to the mixer has been carried out.
void tellMixer(const QStringList &arguments) {
    if (wpctl().isEmpty()) return;
    auto *set = new QProcess;
    QObject::connect(set, &QProcess::finished, set, &QProcess::deleteLater);
    QObject::connect(set, &QProcess::errorOccurred, set, &QProcess::deleteLater);
    set->start(wpctl(), arguments);
}

} // namespace

bool systemVolumeAvailable() {
    return !wpctl().isEmpty() && systemVolume() >= 0.0;
}

double systemVolume() {
    return volumeIn(ask());
}

bool systemVolumeMuted() {
    return ask().contains(QLatin1String("MUTED"));
}

void readSystemVolume(QObject *context, std::function<void(double, bool)> tell) {
    if (wpctl().isEmpty() || !context) return;

    auto *probe = new QProcess(context);
    QObject::connect(
        probe, &QProcess::finished, context,
        [probe, tell = std::move(tell)](int, QProcess::ExitStatus) {
            const QString answer = QString::fromUtf8(probe->readAllStandardOutput());
            probe->deleteLater();
            const double volume = volumeIn(answer);
            if (volume >= 0.0) tell(volume, answer.contains(QLatin1String("MUTED")));
        });
    QObject::connect(probe, &QProcess::errorOccurred, probe, &QProcess::deleteLater);
    probe->start(wpctl(), {QStringLiteral("get-volume"), QLatin1String(kSink)});
}

void setSystemVolume(double volume) {
    const double clamped = qBound(0.0, volume, 1.0);
    tellMixer({QStringLiteral("set-volume"), QLatin1String(kSink),
               QString::number(clamped, 'f', 2)});
}

void setSystemVolumeMuted(bool muted) {
    tellMixer({QStringLiteral("set-mute"), QLatin1String(kSink),
               muted ? QStringLiteral("1") : QStringLiteral("0")});
}

SystemVolumeWatch::~SystemVolumeWatch() {
    if (!m_monitor) return;
    m_monitor->disconnect(this);
    m_monitor->terminate();
    if (!m_monitor->waitForFinished(1000)) m_monitor->kill();
}

SystemVolumeWatch::SystemVolumeWatch(QObject *parent) : QObject(parent) {
    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    m_settle->setInterval(kSettleMs);
    connect(m_settle, &QTimer::timeout, this, &SystemVolumeWatch::changed);

    const QString monitor = QStandardPaths::findExecutable(QLatin1String(kMonitor));
    if (!monitor.isEmpty()) {
        m_monitor = new QProcess(this);
        // Everything it says means the same thing here: something about the
        // sound has moved. What moved is read from the mixer afterwards,
        // which is simpler than parsing a running commentary.
        connect(m_monitor, &QProcess::readyReadStandardOutput, this, [this] {
            m_monitor->readAllStandardOutput();
            m_settle->start();
        });
        m_monitor->setProcessChannelMode(QProcess::MergedChannels);
        m_monitor->start(monitor, {});
        m_listening = m_monitor->waitForStarted(2000);
        if (!m_listening) {
            m_monitor->deleteLater();
            m_monitor = nullptr;
        }
    }

    if (m_listening) return;

    // Nothing to listen with: back to asking every few seconds.
    auto *poll = new QTimer(this);
    poll->setInterval(kPollMs);
    connect(poll, &QTimer::timeout, this, &SystemVolumeWatch::changed);
    poll->start();
}
