#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include "MainWindow.h"
#include "NoCloseBoxStyle.h"

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
    QApplication::setOrganizationName("progman-linux");
    QApplication::setApplicationName("progman-linux");
    // Program groups have no close box. The only captions this application
    // draws itself are its group windows', so taking the box out at the
    // style is enough -- and it reaches the buttons a maximized group hands
    // up to the menu bar, which belong to the menu bar rather than to the
    // group.
    QApplication::setStyle(new NoCloseBoxStyle);
    ensureIconTheme();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "A Progman-style launcher for Windows programs."));
    parser.addHelpOption();
    QCommandLineOption profileOption(
        {QStringLiteral("p"), QStringLiteral("profile")},
        QCoreApplication::translate("main", "Open <profile> instead of the one last used."),
        QStringLiteral("profile"));
    parser.addOption(profileOption);
    parser.process(app);
    const QString profile = parser.value(profileOption);

    // Only one Program Manager at a time. The name is per-user so separate
    // logins do not collide over the socket in the temp directory.
    const QString serverName =
        QStringLiteral("progman-linux-") + qEnvironmentVariable("USER", QStringLiteral("user"));

    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            // Someone answered, so an instance is live: ask it to come
            // forward -- switching profile on the way, if one was asked for
            // -- and leave rather than opening a second window.
            probe.write(profile.isEmpty() ? QByteArray("raise")
                                          : QByteArray("profile:") + profile.toUtf8());
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    // Nothing answered, so any socket file still lying around was left by an
    // instance that did not exit cleanly.
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    MainWindow window(profile);
    window.show();

    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
        QLocalSocket *client = server.nextPendingConnection();
        if (!client) return;
        client->waitForReadyRead(300);
        const QByteArray request = client->readAll();
        client->deleteLater();

        static const QByteArray prefix("profile:");
        if (request.startsWith(prefix)) {
            window.switchToProfile(QString::fromUtf8(request.mid(prefix.size())));
        }
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window.raise();
        window.activateWindow();
    });

    return app.exec();
}
