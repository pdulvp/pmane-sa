#include "WindowScale.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
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

QVector<WindowScaleOption> windowScaleOptions(bool wineDpi) {
    QVector<WindowScaleOption> options;
    for (int percent : {100, 110, 120, 150, 200}) {
        options.append({percent, WindowScaleMode::Gamescope});
    }
    // Last, after the scales that always work: this one depends on the
    // program playing along.
    if (wineDpi) options.append({200, WindowScaleMode::WineDpi});
    return options;
}

QString windowScaleLabel(int percent) {
    if (percent % 100 == 0) {
        return QCoreApplication::translate("WindowScale", "%1x").arg(percent / 100);
    }
    // One decimal, and no trailing zero: 1.5x rather than 1.50x.
    return QCoreApplication::translate("WindowScale", "%1x")
        .arg(QString::number(percent / 100.0, 'g', 2));
}

QString windowScaleLabel(const WindowScaleOption &option) {
    if (option.mode == WindowScaleMode::Gamescope) return windowScaleLabel(option.percent);
    return QCoreApplication::translate("WindowScale", "%1 (wine DPI)")
        .arg(windowScaleLabel(option.percent));
}

QString wineDpiScaleValue(int percent) {
    if (percent % 100 == 0) return QString::number(percent / 100);
    return QString::number(percent / 100.0, 'g', 2);
}

int wineLogPixels(int percent) {
    return qRound(double(kDefaultLogPixels) * percent / 100.0);
}

QStringList wineLogPixelsRegistryArgs(int logPixels) {
    return {QStringLiteral("reg"),
            QStringLiteral("add"),
            QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Desktop"),
            QStringLiteral("/v"),
            QStringLiteral("LogPixels"),
            QStringLiteral("/t"),
            QStringLiteral("REG_DWORD"),
            QStringLiteral("/d"),
            QString::number(logPixels),
            QStringLiteral("/f")};
}

QStringList wineDpiRegistryArgs(int percent) {
    return wineLogPixelsRegistryArgs(wineLogPixels(percent));
}

int prefixLogPixels(const QString &prefixDir) {
    QFile registry(prefixDir + QStringLiteral("/user.reg"));
    if (!registry.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;

    // Sections are [Control Panel\\Desktop] and the like, one key per line.
    // The value is wanted from that section alone: LogPixels appears under
    // others too, meaning something else there.
    static const QRegularExpression value(
        QStringLiteral("^\"LogPixels\"=dword:([0-9a-fA-F]+)"));
    bool inDesktop = false;
    QTextStream in(&registry);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1Char('['))) {
            inDesktop = line.startsWith(QLatin1String("[Control Panel\\\\Desktop]"));
            continue;
        }
        if (!inDesktop) continue;
        const QRegularExpressionMatch match = value.match(line);
        if (match.hasMatch()) return match.captured(1).toInt(nullptr, 16);
    }
    return 0;
}

bool prefixExists(const QString &prefixDir) {
    // drive_c is there as soon as the prefix is built. The registry files are
    // not: wine keeps the registry in the running wineserver and writes it
    // out only when that exits, so user.reg is no test of whether a prefix
    // exists -- only of whether one has been flushed.
    return QFileInfo(prefixDir + QStringLiteral("/drive_c")).isDir();
}

void initialisePrefix(const QString &wineCommand, const QString &prefixDir) {
    if (prefixExists(prefixDir)) return;

    QProcess boot;
    boot.setProgram(wineCommand);
    boot.setArguments({QStringLiteral("wineboot"), QStringLiteral("-u")});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WINEPREFIX"), prefixDir);
    boot.setProcessEnvironment(env);
    boot.start();
    // Building a prefix from nothing is slow -- it unpacks a Windows
    // installation -- and there is no use measuring before it is done.
    if (boot.waitForStarted(10000)) boot.waitForFinished(300000);

    // And then the server is stopped, which is what writes the registry out:
    // until it does, there is nothing on disk to read the prefix's DPI from.
    stopWineserver(wineCommand, prefixDir);
}

void setPrefixDpi(const QString &wineCommand, const QString &prefixDir, int logPixels) {
    const int recorded = prefixLogPixels(prefixDir);
    if (recorded == logPixels) return;
    // Nothing recorded means the default, so there is nothing to put back.
    if (logPixels == kDefaultLogPixels && recorded == 0) return;

    // Best effort: a program still runs, at the wrong size, if the registry
    // will not take it, and failing over a font size would be the wrong
    // trade.
    QProcess reg;
    reg.setProgram(wineCommand);
    reg.setArguments(wineLogPixelsRegistryArgs(logPixels));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WINEPREFIX"), prefixDir);
    reg.setProcessEnvironment(env);
    reg.start();
    if (reg.waitForStarted(5000)) reg.waitForFinished(60000);

    // The server started to do the writing read the old value as it came up
    // and would hand it to the program next started, so it is stopped again.
    stopWineserver(wineCommand, prefixDir);
}

void stopWineserver(const QString &wineCommand, const QString &prefixDir) {
    // Beside the wine that will be used, so that a wine kept in a directory
    // of its own stops its own server rather than the system one's.
    QString server;
    if (wineCommand.contains(QLatin1Char('/'))) {
        const QString beside =
            QFileInfo(wineCommand).absolutePath() + QStringLiteral("/wineserver");
        if (QFileInfo(beside).isExecutable()) server = beside;
    }
    if (server.isEmpty()) server = QStandardPaths::findExecutable(QStringLiteral("wineserver"));
    if (server.isEmpty()) return;

    QProcess kill;
    kill.setProgram(server);
    kill.setArguments({QStringLiteral("-k")});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WINEPREFIX"), prefixDir);
    kill.setProcessEnvironment(env);
    kill.start();
    if (kill.waitForStarted(5000)) kill.waitForFinished(10000);
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

// Wine's own furniture, which is not the program: the desktop it keeps, the
// dialogs it puts up while building a prefix, the services behind it. One of
// these can be most of a screen wide, so it must never be mistaken for the
// program being measured. winevdm is deliberately not here -- a 16-bit
// program's windows really do belong to it.
bool isWineInfrastructure(const WindowLine &window) {
    static const QStringList classes = {
        QStringLiteral("explorer.exe"), QStringLiteral("rundll32.exe"),
        QStringLiteral("wineboot.exe"), QStringLiteral("msiexec.exe"),
        QStringLiteral("services.exe"), QStringLiteral("winemenubuilder.exe"),
        QStringLiteral("wine"),         QStringLiteral("winedevice.exe"),
    };
    for (const QString &name : classes) {
        if (window.windowClass.compare(name, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
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
            // The fallback is for a program whose class we guessed wrongly,
            // not for wine's own windows: one of those would otherwise win
            // on size alone.
            if (!isWineInfrastructure(window) && area(window.size) > area(anyNew)) {
                anyNew = window.size;
            }
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
