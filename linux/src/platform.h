// Operating system specifics of the Qt build (kept separate from the shared UI code).
#pragma once
class QApplication;

namespace platform {
// Key sequences ('|' separated) that start free transform.
const char *transformKeys();
// Application setup that depends on the OS: window icon, desktop integration.
void setup(QApplication &app);
}
