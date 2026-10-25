#include "WindowScale.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QStandardPaths>

namespace {

// How often to poll for the window. A wine program takes a moment to open one,
// and longer the first time a prefix is used.
constexpr int kPollMs = 250;

// Once a window has been seen, how much longer to watch it. A window is created
// before it is sized, so the first sighting is often smaller than the size the
// program settles on.
constexpr int kSettleMs = 1500;

} // namespace

bool gamescopeAvailable() {
    return !QStandardPaths::findExecutable(QStringLiteral("gamescope")).isEmpty();
}

QVector<WindowScaleOption> windowScaleOptions(bool wineDpi) {
    static const QVector<int> steps = {110, 120, 150, 200};

    QVector<WindowScaleOption> options{{100, WindowScaleMode::Gamescope}};
    for (int percent : steps) options.append({percent, WindowScaleMode::Gamescope});

    // Listed after the scales that always work, these depending on the program
    // honouring the DPI it is given.
    if (wineDpi) {
        for (int percent : steps) options.append({percent, WindowScaleMode::WineDpi});
    }
    return options;
}

QString windowScaleLabel(int percent) {
    if (percent % 100 == 0) {
        return QCoreApplication::translate("WindowScale", "%1x").arg(percent / 100);
    }
    // One decimal place and no trailing zero: 1.5x rather than 1.50x.
    return QCoreApplication::translate("WindowScale", "%1x")
        .arg(QString::number(percent / 100.0, 'g', 2));
}

QString windowScaleLabel(const WindowScaleOption &option) {
    if (option.mode == WindowScaleMode::Gamescope) return windowScaleLabel(option.percent);
    // Labelled by the DPI it sets, which is the meaningful number: 144 is what
    // Windows calls 150%.
    return QCoreApplication::translate("WindowScale", "%1 (wine DPI %2)")
        .arg(windowScaleLabel(option.percent))
        .arg(wineLogPixels(option.percent));
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

    // Sections look like [Control Panel\\Desktop], one key per line. Only that
    // section is read: LogPixels appears under others too, meaning something
    // else.
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
    // drive_c exists as soon as the prefix is created. The registry files do
    // not: wine keeps the registry in the running wineserver and writes it out
    // only when that exits, so user.reg tests whether the registry has been
    // flushed, not whether the prefix exists.
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
    // Creating a prefix from scratch is slow, unpacking a Windows installation,
    // and there is no point measuring before it finishes.
    if (boot.waitForStarted(10000)) boot.waitForFinished(300000);

    // The server is then stopped, which writes the registry out: until it does,
    // there is nothing on disk to read the prefix's DPI from.
    stopWineserver(wineCommand, prefixDir);
}

void setPrefixDpi(const QString &wineCommand, const QString &prefixDir, int logPixels) {
    const int recorded = prefixLogPixels(prefixDir);
    if (recorded == logPixels) return;
    // Nothing recorded means the default, so there is nothing to reset.
    if (logPixels == kDefaultLogPixels && recorded == 0) return;

    // Best effort: if the registry write fails the program still runs, at the
    // wrong size, which is better than refusing to start it.
    QProcess reg;
    reg.setProgram(wineCommand);
    reg.setArguments(wineLogPixelsRegistryArgs(logPixels));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WINEPREFIX"), prefixDir);
    reg.setProcessEnvironment(env);
    reg.start();
    if (reg.waitForStarted(5000)) reg.waitForFinished(60000);

    // The server started to perform the write read the old value as it came up
    // and would pass it to the next program started, so it is stopped again.
    stopWineserver(wineCommand, prefixDir);
}

void stopWineserver(const QString &wineCommand, const QString &prefixDir) {
    // Run from the same wine that will be used, so a wine installed in its own
    // directory stops its own server rather than the system one.
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

QStringList availableShaders() {
    // The directories gamescope searches, in its documented order.
    const QStringList dirs = {
        QStringLiteral("/usr/share/gamescope/reshade/Shaders"),
        QDir::homePath() + QStringLiteral("/.local/share/gamescope/reshade/Shaders"),
    };

    QStringList names;
    for (const QString &dir : dirs) {
        for (const QFileInfo &file :
             QDir(dir).entryInfoList({QStringLiteral("*.fx")}, QDir::Files, QDir::Name)) {
            // gamescope names an effect by its file name without the extension.
            const QString name = file.completeBaseName();
            if (!names.contains(name)) names.append(name);
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList gamescopeCommand(const QSize &native, int percent, const QString &shader,
                             const QString &program, const QStringList &args) {
    // A shader alone is reason to use gamescope even unscaled: a CRT effect at
    // 1x is a reasonable thing to ask for.
    if (percent <= 100 && shader.isEmpty()) return {};
    if (!native.isValid() || native.isEmpty()) return {};
    if (!gamescopeAvailable()) return {};

    const QSize out = scaledSize(native, percent);

    // A whole multiple can repeat each pixel exactly; a fraction cannot, and
    // asking for integer scaling there would silently fall back to 1x and leave
    // the window inside a border.
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
               } +
           (shader.isEmpty() ? QStringList{}
                             : QStringList{QStringLiteral("--reshade-effect"), shader}) +
           QStringList{QStringLiteral("--"), program} + args;
}

bool isWin16Executable(const QString &exePath) {
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    // A DOS stub, then an offset at 0x3c to the real header, whose first two
    // bytes give its kind: "PE" for 32-bit and above, "NE" for 16-bit. Only
    // those few bytes are read.
    const QByteArray stub = file.read(0x40);
    if (stub.size() < 0x40 || stub[0] != 'M' || stub[1] != 'Z') return false;

    const uchar *d = reinterpret_cast<const uchar *>(stub.constData());
    const quint32 headerOffset = quint32(d[0x3c]) | (quint32(d[0x3d]) << 8) |
                                 (quint32(d[0x3e]) << 16) | (quint32(d[0x3f]) << 24);
    if (!file.seek(headerOffset)) return false;
    return file.read(2) == QByteArray("NE");
}

QString wineWindowClass(const QString &exePath) {
    // A 16-bit program does not run as itself: wine starts winevdm, which loads
    // the program, so the windows belong to winevdm.
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

// Wine's own windows rather than the program's: the desktop it keeps, the
// dialogs it opens while creating a prefix, and its background services. One of
// these can be nearly a screen wide, so it must never be mistaken for the
// program being measured. winevdm is deliberately absent: a 16-bit program's
// windows really do belong to it.
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

// The frames a reparenting window manager puts around other windows. Their size
// is the decoration, not the program.
bool isWindowManagerFrame(const WindowLine &window) {
    return window.windowClass.contains(QLatin1String("mutter"), Qt::CaseInsensitive) ||
           window.windowClass.contains(QLatin1String("frames"), Qt::CaseInsensitive);
}

// Anything smaller than this is a helper window rather than one being drawn in:
// wine keeps a couple of two-pixel windows per program.
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
    // A window is not its final size when it first appears: it is created, then
    // sized, then laid out, so reading it too early gives an intermediate size.
    // The first sighting therefore starts a short settling period, and the
    // largest size seen during it is taken as the program's.
    //
    // Only a window of the program's own class is conclusive. Anything else
    // that appears meanwhile, such as a video player rebuilding its output
    // window or a notification, is kept only in case the class never appears.
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
            // The fallback covers a program whose class was guessed wrongly,
            // not wine's own windows, one of which would otherwise win on size
            // alone.
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
            // Long enough for a window to finish being constructed, short
            // enough not to be a noticeable wait.
            if (timer.elapsed() - firstSeen >= kSettleMs) return best;
        }

        if (timer.elapsed() >= timeoutMs) break;
        QProcess::execute(QStringLiteral("sleep"), {QString::number(kPollMs / 1000.0)});
    } while (timer.elapsed() < timeoutMs);

    // No window of the program's class ever appeared, so whatever did appear is
    // used as the best available guess.
    return best.isEmpty() ? fallback : best;
}
