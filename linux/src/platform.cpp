// Linux specifics.
#include "platform.h"
#include <QApplication>
#include <QIcon>

namespace platform {
const char *transformKeys() { return "Ctrl+T"; }
void setup(QApplication &app) {
    app.setWindowIcon(QIcon(":/r-painter.png"));
    // lets the desktop match the window to r-painter.desktop (task bar icon, Wayland app id)
    QGuiApplication::setDesktopFileName("r-painter");
}
}
