#include <QApplication>
#include <QMainWindow>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ECS PDF Converter");
    app.setOrganizationName("Entertainment Control Systems");
    app.setApplicationVersion("2.0.0");

    // High DPI support is automatic in Qt6
    // app.setAttribute(Qt::AA_UseHighDpiPixmaps); // deprecated, no-op in Qt6

    MainWindow window;
    window.show();

    return app.exec();
}