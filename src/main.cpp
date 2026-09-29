#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("progman-linux");
    QApplication::setApplicationName("progman-linux");

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
