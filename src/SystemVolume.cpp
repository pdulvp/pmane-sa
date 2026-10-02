#include "SystemVolume.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>

namespace {

// Whatever wpctl calls the output everything plays through, rather than a
// particular sound card: the desktop may move it while this is running.
const char *kSink = "@DEFAULT_AUDIO_SINK@";

QString wpctl() {
    static const QString path = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    return path;
}

// wpctl answers "Volume: 0.36", and "Volume: 0.36 [MUTED]" when it is off.
QString readVolumeLine() {
    if (wpctl().isEmpty()) return {};

    QProcess ask;
    ask.setProgram(wpctl());
    ask.setArguments({QStringLiteral("get-volume"), QLatin1String(kSink)});
    ask.start();
    // Short: this is asked again every few seconds, and a mixer that does
    // not answer promptly is one worth giving up on.
    if (!ask.waitForFinished(2000)) {
        ask.kill();
        return {};
    }
    return QString::fromUtf8(ask.readAllStandardOutput());
}

void tell(const QStringList &arguments) {
    if (wpctl().isEmpty()) return;
    QProcess set;
    set.setProgram(wpctl());
    set.setArguments(arguments);
    set.start();
    set.waitForFinished(2000);
}

} // namespace

bool systemVolumeAvailable() {
    return !wpctl().isEmpty() && systemVolume() >= 0.0;
}

double systemVolume() {
    static const QRegularExpression level(QStringLiteral("Volume:\\s*([0-9]*\\.?[0-9]+)"));
    const QRegularExpressionMatch match = level.match(readVolumeLine());
    if (!match.hasMatch()) return -1.0;
    return match.captured(1).toDouble();
}

bool systemVolumeMuted() {
    return readVolumeLine().contains(QLatin1String("MUTED"));
}

void setSystemVolume(double volume) {
    const double clamped = qBound(0.0, volume, 1.0);
    tell({QStringLiteral("set-volume"), QLatin1String(kSink),
          QString::number(clamped, 'f', 2)});
}

void setSystemVolumeMuted(bool muted) {
    tell({QStringLiteral("set-mute"), QLatin1String(kSink),
          muted ? QStringLiteral("1") : QStringLiteral("0")});
}
