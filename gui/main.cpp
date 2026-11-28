// main.cpp - starts the OS-Sim GUI. "--selftest" runs an automatic smoke test
// (use with QT_QPA_PLATFORM=offscreen) and exits with 0 on success.
#include <QApplication>
#include <QSettings>
#include <cstdio>
#include <cstring>
#include "MainWindow.h"
#include "Style.h"
#include "Theme.h"
#include "ModuleManager.h"
#include "workspace.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("OS-Sim");
    bool selftest = false, layoutReport = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
        if (std::strcmp(argv[i], "--layout-report") == 0) layoutReport = true;
    }
    // Workspace (Modules menu): the self-test and the layout report use the default workspace and
    // never write the user's settings.
    if (selftest || layoutReport) MainWindow::workspaceStore().clear();
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
    // --workspace-set "memory.heap=0,processes=0" : change the saved workspace and exit
    // --workspace-report : print the saved workspace (module tree with check marks) and exit
    // (used by the restart test in tests/CMakeLists.txt; both use the normal settings store)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--workspace-set") == 0 && i + 1 < argc) {
            VisibilityMap m = w.modules()->workspace().snapshot(), req;
            int bad = parseVisibility(argv[i + 1], req);
            int refused = 0;
            for (const auto& [id, on] : req) {
                QString why;
                if (!w.modules()->setVisible(QString::fromStdString(id), on, &why)) {
                    std::printf("refused %s=%d: %s\n", id.c_str(), on ? 1 : 0, qPrintable(why.isEmpty() ? QString("unknown module") : why));
                    ++refused;
                }
            }
            std::printf("%s", qPrintable(w.modules()->report()));
            return bad || refused ? 2 : 0;
        }
        if (std::strcmp(argv[i], "--workspace-report") == 0) {
            for (const QString& warn : w.modules()->loadWarnings()) std::printf("warning: %s\n", qPrintable(warn));
            std::printf("%s", qPrintable(w.modules()->report()));
            return 0;
        }
    }
    w.show();
    return app.exec();
}
