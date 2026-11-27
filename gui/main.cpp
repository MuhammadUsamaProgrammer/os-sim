// main.cpp - starts the OS-Sim GUI. "--selftest" runs an automatic smoke test
// (use with QT_QPA_PLATFORM=offscreen) and exits with 0 on success.
#include <QApplication>
#include <QSettings>
#include <cstring>
#include "MainWindow.h"
#include "Style.h"
#include "Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("OS-Sim");
    bool selftest = false;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
    // Theme: light by default, or the one chosen last time (View > Dark theme).
    // The self-test always uses the light theme so it does not depend on saved settings.
    bool dark = !selftest && QSettings("OS-Sim", "OS-Sim").value("darkTheme", false).toBool();
    ui::applyTheme(dark);
    MainWindow w;
    if (selftest) return w.runSelfTest();
    w.show();
    return app.exec();
}
