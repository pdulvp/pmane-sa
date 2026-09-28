#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("progman-linux");
    QApplication::setApplicationName("progman-linux");

    // Only one Program Manager at a time. The name is per-user so separate
    // logins do not collide over the socket in the temp directory.
    const QString serverName =
        QStringLiteral("progman-linux-") + qEnvironmentVariable("USER", QStringLiteral("user"));

    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            // Someone answered, so an instance is live: ask it to come
            // forward and leave rather than opening a second window.
            probe.write("raise");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    // Nothing answered, so any socket file still lying around was left by an
    // instance that did not exit cleanly.
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    MainWindow window;
    window.show();

    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
        if (QLocalSocket *client = server.nextPendingConnection()) client->deleteLater();
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window.raise();
        window.activateWindow();
    });

    return app.exec();
}
