#include "mainwindow.h"
#include "platform.h"
#include <QApplication>
#include <QLocale>
#include <QStyleFactory>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("R Painter");
    app.setApplicationVersion("1.0.0");
    applyTheme(app);
    // interface language: RPAINTER_LANG if set, otherwise the system locale
    const QByteArray envLang = qgetenv("RPAINTER_LANG");
    setLanguage(envLang.isEmpty() ? QLocale::system().name().toStdString() : envLang.toStdString());
    platform::setup(app);

    MainWindow w;
    w.show();
    if (argc > 1) w.openPath(QString::fromLocal8Bit(argv[1]), false);
    return app.exec();
}
