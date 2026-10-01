#include "WindowScale.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

// How often to look for the window while waiting for it. A wine program
// takes a moment to put one up, and longer the first time a prefix is used.
constexpr int kPollMs = 250;

// Once a window has been seen, how much longer to watch it. A window is
// created before it is sized, so the first sighting is often smaller than
// what the program settles on.
constexpr int kSettleMs = 1500;

} // namespace

bool gamescopeAvailable() {
    return !QStandardPaths::findExecutable(QStringLiteral("gamescope")).isEmpty();
}

const QVector<int> &windowScales() {
    static const QVector<int> scales = {100, 110, 120, 150, 200};
    return scales;
}

QString windowScaleLabel(int percent) {
    if (percent % 100 == 0) {
        return QCoreApplication::translate("WindowScale", "%1x").arg(percent / 100);
    }
    // One decimal, and no trailing zero: 1.5x rather than 1.50x.
    return QCoreApplication::translate("WindowScale", "%1x")
        .arg(QString::number(percent / 100.0, 'g', 2));
}

QSize scaledSize(const QSize &native, int percent) {
    if (!native.isValid() || native.isEmpty() || percent <= 0) return {};
    return QSize(qRound(native.width() * percent / 100.0),
                 qRound(native.height() * percent / 100.0));
}

QStringList gamescopeCommand(const QSize &native, int percent, const QString &program,
                             const QStringList &args) {
    if (percent <= 100 || !native.isValid() || native.isEmpty()) return {};
    if (!gamescopeAvailable()) return {};

    const QSize out = scaledSize(native, percent);

    // A whole multiple can repeat each pixel exactly; a fraction cannot, and
    // asking for integer scaling there would quietly drop back to 1x and
    // leave the window in a border.
    const bool whole = percent % 100 == 0;

    return QStringList{
               QStringLiteral("-w"), QString::number(native.width()),
               QStringLiteral("-h"), QString::number(native.height()),
               QStringLiteral("-W"), QString::number(out.width()),
               QStringLiteral("-H"), QString::number(out.height()),
               QStringLiteral("-S"),
               whole ? QStringLiteral("integer") : QStringLiteral("fit"),
               QStringLiteral("-F"),
               whole ? QStringLiteral("pixel") : QStringLiteral("pixel"),
               QStringLiteral("--"), program} +
           args;
}

bool isWin16Executable(const QString &exePath) {
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    // A DOS stub, then an offset at 0x3c to the real header, whose first two
    // bytes say which kind it is: "PE" for 32-bit and up, "NE" for 16-bit.
    // Only the handful of bytes that answer the question are read.
    const QByteArray stub = file.read(0x40);
    if (stub.size() < 0x40 || stub[0] != 'M' || stub[1] != 'Z') return false;

    const uchar *d = reinterpret_cast<const uchar *>(stub.constData());
    const quint32 headerOffset = quint32(d[0x3c]) | (quint32(d[0x3d]) << 8) |
                                 (quint32(d[0x3e]) << 16) | (quint32(d[0x3f]) << 24);
    if (!file.seek(headerOffset)) return false;
    return file.read(2) == QByteArray("NE");
}

QString wineWindowClass(const QString &exePath) {
    // A 16-bit program does not run as itself: wine starts winevdm, which
    // loads the program, and the windows belong to winevdm.
    if (isWin16Executable(exePath)) return QStringLiteral("winevdm.exe");
    return QFileInfo(exePath).fileName();
}

namespace {

// One line of `xwininfo -root -tree`:
//   0x1e00003 "Pipe Dream": ("winevdm.exe" "winevdm.exe")  721x500+61+92  +5+38
struct WindowLine {
    QString id;
    QString windowClass;
    QSize size;
};

bool parseWindowLine(const QString &line, WindowLine *out) {
    static const QRegularExpression entry(
        QStringLiteral("^\\s*(0x[0-9a-f]+)\\s+.*?\\(\"([^\"]*)\"\\s+\"[^\"]*\"\\)"
                       "\\s+(\\d+)x(\\d+)[+-]"));
    const QRegularExpressionMatch match = entry.match(line);
    if (!match.hasMatch()) return false;
    out->id = match.captured(1);
    out->windowClass = match.captured(2);
    out->size = QSize(match.captured(3).toInt(), match.captured(4).toInt());
    return true;
}

// The frames a reparenting window manager puts round other people's windows.
// Their size is the decoration, not the program.
bool isWindowManagerFrame(const WindowLine &window) {
    return window.windowClass.contains(QLatin1String("mutter"), Qt::CaseInsensitive) ||
           window.windowClass.contains(QLatin1String("frames"), Qt::CaseInsensitive);
}

// Anything smaller than this is a helper rather than a window being drawn in:
// wine keeps a couple of two-pixel ones per program.
constexpr int kMinimumInteresting = 16;

QList<WindowLine> currentWindows() {
    QList<WindowLine> windows;
    const QString xwininfo = QStandardPaths::findExecutable(QStringLiteral("xwininfo"));
    if (xwininfo.isEmpty()) return windows;

    QProcess probe;
    probe.start(xwininfo, {QStringLiteral("-root"), QStringLiteral("-tree")});
    if (!probe.waitForFinished(5000)) return windows;

    const QString tree = QString::fromUtf8(probe.readAllStandardOutput());
    for (const QString &line : tree.split(QLatin1Char('\n'))) {
        WindowLine window;
        if (parseWindowLine(line, &window)) windows.append(window);
    }
    return windows;
}

} // namespace

QStringList windowSnapshot() {
    QStringList ids;
    for (const WindowLine &window : currentWindows()) ids << window.id;
    return ids;
}

QSize measureNewWindow(const QStringList &before, const QString &classHint, int timeoutMs) {
    // A window is not its final size the moment it appears: it is created,
    // then sized, then laid out, and catching it too early gives whatever it
    // measured on the way. So the first sighting starts a short settling
    // period rather than being the answer, and the largest size seen during
    // it is what the program asked for.
    //
    // Only a window of the program's own class settles the question. Anything
    // else that happens to appear while we are waiting -- a video player
    // rebuilding its output window, a notification -- is kept only against
    // the possibility that the class never shows up at all.
    QSize best;
    QSize fallback;
    QElapsedTimer timer;
    timer.start();
    qint64 firstSeen = -1;

    do {
        QSize matching;
        QSize anyNew;
        for (const WindowLine &window : currentWindows()) {
            if (isWindowManagerFrame(window)) continue;
            if (before.contains(window.id)) continue;
            if (window.size.width() <= kMinimumInteresting ||
                window.size.height() <= kMinimumInteresting) {
                continue;
            }

            const auto area = [](const QSize &size) { return size.width() * size.height(); };
            if (!classHint.isEmpty() &&
                window.windowClass.compare(classHint, Qt::CaseInsensitive) == 0) {
                if (area(window.size) > area(matching)) matching = window.size;
            }
            if (area(window.size) > area(anyNew)) anyNew = window.size;
        }

        if (anyNew.width() * anyNew.height() > fallback.width() * fallback.height()) {
            fallback = anyNew;
        }
        if (!matching.isEmpty()) {
            if (matching.width() * matching.height() > best.width() * best.height()) {
                best = matching;
            }
            if (firstSeen < 0) firstSeen = timer.elapsed();
            // Long enough for a window to finish being put together, short
            // enough not to be a wait anyone notices.
            if (timer.elapsed() - firstSeen >= kSettleMs) return best;
        }

        if (timer.elapsed() >= timeoutMs) break;
        QProcess::execute(QStringLiteral("sleep"), {QString::number(kPollMs / 1000.0)});
    } while (timer.elapsed() < timeoutMs);

    // Nothing of the program's class ever appeared: whatever did is a better
    // guess than nothing, but only because there is nothing better.
    return best.isEmpty() ? fallback : best;
}
