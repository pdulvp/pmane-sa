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

// Qt falls back to the bare "hicolor" theme when the desktop does not say which
// icon theme to use, and hicolor contains almost nothing, which leaves the
// file-type icons documents are shown with empty. This points the fallback at
// an icon theme that is actually installed.
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
    QApplication::setOrganizationName("pmane-sa");
    QApplication::setApplicationName("pmane-sa");
    // The version and its build stamp, reported by --version and Help > About.
    QApplication::setApplicationVersion(fullVersion());
    // How the desktop identifies this application. On Wayland it becomes the
    // window's app_id, which the shell matches against the .desktop file of the
    // same name to tell which application a window belongs to, and that is what
    // allows it to be pinned. Without it the app_id falls back to the
    // executable's file name and would change with the binary's name.
    QApplication::setDesktopFileName(QStringLiteral("pmane-sa"));
    // Program groups have no close box. The only captions this application
    // draws itself are its group windows', so removing the box in the style is
    // enough, and doing it there also covers the buttons a maximized group
    // moves up to the menu bar, which belong to the menu bar rather than the
    // group.
    QApplication::setStyle(new GroupCaptionStyle);
    ensureIconTheme();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Another Sans-Ame Program Manager for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    // Screens were once called profiles, and shortcuts written then still pass
    // --profile, so both names map to the same option.
    QCommandLineOption screenOption(
        {QStringLiteral("s"), QStringLiteral("screen"), QStringLiteral("p"),
         QStringLiteral("profile")},
        QCoreApplication::translate("main", "Open <screen> instead of the one last used."),
        QStringLiteral("screen"));
    parser.addOption(screenOption);
    // Screens kept outside ~/.config: the ones the documentation's screenshots
    // are taken from live in docs/, and opening them this way neither reads nor
    // writes the desktop in use.
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
    // logins do not collide over the socket in the temp directory. Screens
    // opened from --configPath get their own instance, so taking screenshots of
    // them does not raise or switch the Program Manager already running on the
    // real desktop. The path is hashed rather than used directly, the name
    // being a socket in the temp directory.
    QString serverName =
        QStringLiteral("pmane-sa-") + qEnvironmentVariable("USER", QStringLiteral("user"));
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
            // Something answered, so an instance is already running: ask it to
            // come forward, switching screen if one was requested, and exit
            // rather than opening a second window.
            probe.write(screen.isEmpty() ? QByteArray("raise")
                                         : QByteArray("screen:") + screen.toUtf8());
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    // Nothing answered, so any remaining socket file was left by an instance
    // that did not exit cleanly.
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

        // "profile:" is what an instance from before the rename sends, which is
        // still worth accepting while one may be running.
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
