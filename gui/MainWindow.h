// MainWindow.h - main window: tabs, shared event-log dock, menus, status bar, self-test.
#pragma once
#include <QMainWindow>

class QTabWidget; class QPlainTextEdit;
class SchedulingTab; class MemoryTab; class DeadlockTab;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    // Loads a sample file into the right tab (chosen by file name). Returns false on error.
    bool loadSample(const QString& path);
    // --selftest: exercises every tab and action without a display. Returns the exit code.
    int runSelfTest();
    // Writes <base>.txt and <base>.csv with every section. Returns false and sets *err on failure.
    bool writeReport(const QString& base, QString* err);

public slots:
    void appendLog(const QString& line);

private slots:
    void showAbout();
    void exportReport();

private:
    void buildMenus();
    QTabWidget* tabs_;
    SchedulingTab* sched_;
    MemoryTab* mem_;
    DeadlockTab* dead_;
    QPlainTextEdit* log_;
};
