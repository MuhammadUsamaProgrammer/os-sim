// placeholder GUI entry point (Stage 1)
#include <QApplication>
#include <QLabel>
#include <cstring>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QLabel l("OS-Sim (skeleton)");
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--selftest") == 0) return 0;
    l.show();
    return app.exec();
}
