#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("progman-linux");
    QApplication::setApplicationName("progman-linux");

    MainWindow window;
    window.show();

    return app.exec();
}
