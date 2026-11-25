// main.cpp - starts the OS-Sim GUI. "--selftest" runs an automatic smoke test
// (use with QT_QPA_PLATFORM=offscreen) and exits with 0 on success.
#include <QApplication>
#include <cstring>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("OS-Sim");
    bool selftest = false;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
    MainWindow w;
    if (selftest) return w.runSelfTest();
    w.show();
    return app.exec();
}
