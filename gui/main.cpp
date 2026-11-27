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
    // --layout-report 1366x768,1920x1080 : audit every tab at those window sizes (exit 1 on problems)
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--layout-report") == 0) {
            ui::selfTestMode() = true;
            QList<QSize> sizes;
            for (const QString& s : QString(argv[i + 1]).split(',', Qt::SkipEmptyParts)) {
                QStringList wh = s.split('x');
                if (wh.size() == 2) sizes << QSize(wh[0].toInt(), wh[1].toInt());
            }
            return w.layoutReport(sizes).isEmpty() ? 0 : 1;
        }
    w.show();
    return app.exec();
}
