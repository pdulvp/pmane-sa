#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
#include "MainWindow.h"
#include "ScreenManager.h"
#include "Version.h"
#include "GroupCaptionStyle.h"

namespace {

// Qt settles on the bare "hicolor" theme when the desktop has not told it
// which icon theme to use, and hicolor carries next to nothing -- which
// leaves file-type icons, the ones documents are shown with, empty. Point
// the fallback at an icon theme that is actually installed.
void ensureIconTheme() {
    const QString configured = QIcon::themeName();
    if (!configured.isEmpty() && configured != QLatin1String("hicolor")) return;

    const QStringList candidates = {QStringLiteral("Adwaita"), QStringLiteral("breeze"),
                                    QStringLiteral("oxygen"), QStringLiteral("Papirus"),
                                    QStringLiteral("gnome")};
    for (const QString &name : candidates) {
        for (const QString &dir : QIcon::themeSearchPaths()) {
            if (!QFileInfo::exists(dir + QLatin1Char('/') + name +
                                   QLatin1String("/index.theme"))) {
                continue;
            }
            QIcon::setFallbackThemeName(name);
            return;
        }
    }
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("pmane-sans-ame");
    QApplication::setApplicationName("pmane-sans-ame");
    // The version and the build it was made in: --version says both, and so
    // does Help > About.
    QApplication::setApplicationVersion(fullVersion());
    // What the desktop knows this application by. On Wayland it becomes the
    // window's app_id, and a shell matches that against the .desktop file of
    // the same name to work out which application a window belongs to --
    // which is what lets it be pinned. Without it the app_id falls back to
    // the executable's filename, so it would change with the binary's name.
    QApplication::setDesktopFileName(QStringLiteral("pmane-sans-ame"));
    // Program groups have no close box. The only captions this application
    // draws itself are its group windows', so taking the box out at the
    // style is enough -- and it reaches the buttons a maximized group hands
    // up to the menu bar, which belong to the menu bar rather than to the
    // group.
    QApplication::setStyle(new GroupCaptionStyle);
    ensureIconTheme();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Another Sans-Ame Program Manager for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    // Screens were called profiles once, and shortcuts written then still
    // pass --profile: both names reach the same option.
    QCommandLineOption screenOption(
        {QStringLiteral("s"), QStringLiteral("screen"), QStringLiteral("p"),
         QStringLiteral("profile")},
        QCoreApplication::translate("main", "Open <screen> instead of the one last used."),
        QStringLiteral("screen"));
    parser.addOption(screenOption);
    // Screens kept somewhere other than ~/.config: the ones the
    // documentation's pictures are taken of live in docs/, and opening them
    // this way neither reads nor writes the desktop in use.
    QCommandLineOption configOption(
        {QStringLiteral("c"), QStringLiteral("configPath"), QStringLiteral("config-path")},
        QCoreApplication::translate("main",
                                    "Keep screens in <dir> instead of the config "
                                    "directory. Created if it does not exist."),
        QStringLiteral("dir"));
    parser.addOption(configOption);
    parser.process(app);
    const QString screen = parser.value(screenOption);
    const QString configPath = parser.value(configOption);
    ScreenManager::useConfigPath(configPath);

    // Only one Program Manager at a time. The name is per-user so separate
    // logins do not collide over the socket in the temp directory.
    // Screens opened from --configPath are their own instance: taking pictures
    // of them should not raise, or switch, the Program Manager already
    // running on the real desktop. The path is hashed rather than used, the
    // name being a socket in the temp directory.
    QString serverName =
        QStringLiteral("pmane-sans-ame-") + qEnvironmentVariable("USER", QStringLiteral("user"));
    if (!configPath.isEmpty()) {
        serverName +=
            QLatin1Char('-') +
            QString::fromLatin1(
                QCryptographicHash::hash(QFileInfo(configPath).absoluteFilePath().toUtf8(),
                                         QCryptographicHash::Sha1)
                    .toHex()
                    .left(12));
    }

    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            // Someone answered, so an instance is live: ask it to come
            // forward -- switching screen on the way, if one was asked for
            // -- and leave rather than opening a second window.
            probe.write(screen.isEmpty() ? QByteArray("raise")
                                         : QByteArray("screen:") + screen.toUtf8());
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    // Nothing answered, so any socket file still lying around was left by an
    // instance that did not exit cleanly.
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    MainWindow window(screen);
    window.show();

    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
        QLocalSocket *client = server.nextPendingConnection();
        if (!client) return;
        client->waitForReadyRead(300);
        const QByteArray request = client->readAll();
        client->deleteLater();

        // "profile:" is what an instance from before the rename would send,
        // which is worth answering while one is still running.
        for (const QByteArray &prefix : {QByteArray("screen:"), QByteArray("profile:")}) {
            if (!request.startsWith(prefix)) continue;
            window.switchToScreen(QString::fromUtf8(request.mid(prefix.size())));
            break;
        }
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window.raise();
        window.activateWindow();
    });

    return app.exec();
}
