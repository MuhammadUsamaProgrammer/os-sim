// MainWindow.cpp - assembles the three tabs and the shared log dock.
#include "MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include "DeadlockTab.h"
#include "EngineBridge.h"
#include "MemoryTab.h"
#include "SchedulingTab.h"
#include "Style.h"

// Creates the tabs, the log dock (read-only, monospace) and the menus.
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OS-Sim: Operating System Simulator");
    setMinimumSize(1100, 700);

    tabs_ = new QTabWidget;
    sched_ = new SchedulingTab;
    mem_ = new MemoryTab;
    dead_ = new DeadlockTab;
    tabs_->addTab(sched_, "Scheduling");
    tabs_->addTab(mem_, "Memory");
    tabs_->addTab(dead_, "Deadlock");
    setCentralWidget(tabs_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMaximumBlockCount(20000);
    auto* dock = new QDockWidget("Event log", this);
    dock->setObjectName("logDock");
    dock->setWidget(log_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    resizeDocks({dock}, {170}, Qt::Vertical);

    connect(sched_, &SchedulingTab::logMessage, this, &MainWindow::appendLog);
    connect(mem_, &MemoryTab::logMessage, this, &MainWindow::appendLog);
    connect(dead_, &DeadlockTab::logMessage, this, &MainWindow::appendLog);

    // One shared process list: the Memory tab reads the Scheduling tab's table.
    mem_->setProcessProvider([this] {
        std::vector<PCB> p;
        if (!sched_->readProcesses(p, true)) p.clear();    // invalid table -> no processes
        return p;
    });
    connect(sched_, &SchedulingTab::processesChanged, mem_, &MemoryTab::refreshPids);

    buildMenus();
    statusBar()->showMessage("Ready. Load a sample from File > Load sample, or enter processes.");
    loadSample(ui::samplesDir() + "/processes_fcfs.csv");
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    QMenu* samples = file->addMenu("Load sample");
    QDir dir(ui::samplesDir());
    for (const QString& f : dir.entryList({"*.csv", "*.txt"}, QDir::Files, QDir::Name)) {
        QString path = dir.absoluteFilePath(f);
        samples->addAction(f, this, [this, path] { loadSample(path); });
    }
    file->addAction("Export report...", this, &MainWindow::exportReport);
    file->addSeparator();
    file->addAction("Exit", QKeySequence::Quit, this, &QWidget::close);
    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("About", this, &MainWindow::showAbout);
}

// Appends one line to the shared event log.
void MainWindow::appendLog(const QString& line) { log_->appendPlainText(line); }

// Picks the tab from the file name: processes_* -> Scheduling, etc.
bool MainWindow::loadSample(const QString& path) {
    QString name = QFileInfo(path).fileName();
    bool ok = false;
    if (name.startsWith("processes_")) {
        ok = sched_->loadCsv(path);
        tabs_->setCurrentWidget(sched_);
    } else if (name.startsWith("refstring_")) {
        ok = mem_->loadRefFile(path);
        tabs_->setCurrentWidget(mem_);
    } else {
        ui::info(this, "This sample is used by a tab that is not finished yet: " + name);
        return false;
    }
    if (ok) statusBar()->showMessage("Loaded sample " + name, 5000);
    return ok;
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About OS-Sim",
        "<h3>OS-Sim: An Integrated Operating System Simulator</h3>"
        "<p>CPU Scheduling, Memory Management and Deadlock Detection with a GUI.</p>"
        "<p>Operating System Lab, BS (IT) semester 5, Bahria University<br>"
        "Instructor: Amarah Abdullah</p>"
        "<p><b>Group NBA</b><br>"
        "Aagha Ali Hassan (03-135242-007)<br>"
        "Hamza Malik (03-135242-021)<br>"
        "Muhammad Usama (03-135242-038)</p>");
}

void MainWindow::exportReport() {
    ui::info(this, "Export is added in Stage 9.");
}

// ---------------------------------------------------------------- self-test
// Every check prints a line; any failure makes the exit code 1.
static int g_stFail = 0;
static void stCheck(bool ok, const QString& what) {
    std::printf("[selftest] %s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) ++g_stFail;
}

// If the environment variable OSSIM_SHOTS names a folder, saves a PNG of the window there.
static void shot(QWidget* w, const QString& name) {
    QString dir = qEnvironmentVariable("OSSIM_SHOTS");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    w->grab().save(dir + "/" + name + ".png");
}

// Waits (processing Qt events) until cond() is true or timeoutMs passes.
template <class F> static bool waitFor(F cond, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return cond();
}

int MainWindow::runSelfTest() {
    ui::selfTestMode() = true;
    g_stFail = 0;
    resize(1280, 800);
    show();                                // offscreen platform: nothing appears on screen
    QString sd = ui::samplesDir();

    // Scheduling: every process sample, every algorithm, compare, animated replay
    for (const char* f : {"processes_fcfs.csv", "processes_srtf.csv", "processes_priority.csv"}) {
        stCheck(loadSample(sd + "/" + f), QString("load ") + f);
        for (Algo a : allAlgos()) {
            sched_->setAlgorithm(a);
            stCheck(sched_->runOnce(), QString("run %1 on %2").arg(algoName(a), f));
        }
        stCheck(sched_->compareAll() && sched_->lastComparison().size() == 6, QString("compare all on ") + f);
    }
    loadSample(sd + "/processes_fcfs.csv");
    sched_->setAlgorithm(Algo::FCFS);
    sched_->runOnce();
    stCheck(sched_->lastResult() && std::fabs(sched_->lastResult()->avgWaiting - 17.0) < 1e-9, "FCFS avg WT = 17");
    shot(this, "sched_fcfs");
    sched_->setAlgorithm(Algo::RR);
    sched_->setQuantum(4);
    sched_->runOnce();
    stCheck(sched_->lastResult() && std::fabs(sched_->lastResult()->avgWaiting - 17.0 / 3) < 1e-9, "RR q=4 avg WT = 5.67");
    loadSample(sd + "/processes_srtf.csv");
    sched_->compareAll();
    shot(this, "sched_compare");
    loadSample(sd + "/processes_fcfs.csv");

    bool animDone = false;
    auto conn = connect(sched_, &SchedulingTab::animationFinished, this, [&] { animDone = true; });
    stCheck(sched_->runAnimated(0), "start animated run");
    stCheck(waitFor([&] { return animDone; }, 10000), "animated run finishes");
    shot(this, "sched_animated");
    std::vector<PCB> fin = sched_->bridge()->pcbs();
    bool match = !fin.empty();
    for (const PCB& p : fin)
        for (const ProcMetrics& m : sched_->lastResult()->metrics)
            if (m.pid == p.pid && m.completion != p.finish) match = false;
    stCheck(match, "engine finish times equal core completion times");
    // Stop in the middle of a slow animation must not hang
    animDone = false;
    sched_->runAnimated(50);
    QThread::msleep(120);
    sched_->stopAnimation();
    stCheck(!sched_->animationRunning(), "stop animated run");
    waitFor([&] { return animDone; }, 2000);
    disconnect(conn);

    // Bad input must give a warning, not a crash
    int w = ui::warningCount();
    sched_->setProcesses({});
    stCheck(!sched_->runOnce() && ui::warningCount() == w + 1, "empty table -> warning");
    PCB bad; bad.pid = 1; bad.burst = 0;
    sched_->setProcesses({bad});
    stCheck(!sched_->runOnce() && ui::warningCount() == w + 2, "burst 0 -> warning");
    stCheck(!sched_->loadCsv(sd + "/banker_sample.txt"), "bad CSV -> warning");

    // Memory: samples, every mode, the 15/12/9 numbers, Belady, bad input, page table
    stCheck(loadSample(sd + "/refstring_belady.txt"), "load refstring_belady.txt");
    stCheck(loadSample(sd + "/refstring_standard.txt"), "load refstring_standard.txt");
    mem_->setFrames(3);
    for (int m = 0; m < 3; ++m) { mem_->setMode(m); stCheck(mem_->run(), QString("memory mode %1").arg(m)); }
    mem_->setMode(3);
    stCheck(mem_->run() && mem_->lastResults().size() == 3, "memory compare all");
    const auto& mr = mem_->lastResults();
    stCheck(mr.size() == 3 && mr[0].faults == 15 && mr[1].faults == 12 && mr[2].faults == 9, "faults 15 / 12 / 9");
    shot(this, "memory_compare");
    mem_->beladyExample();
    stCheck(mem_->beladyText().contains("3 frames -> 9 faults") && mem_->beladyText().contains("4 frames -> 10 faults"),
            "Belady 9 vs 10");
    shot(this, "memory_belady");
    w = ui::warningCount();
    mem_->setRefString("1,a");
    stCheck(!mem_->run() && ui::warningCount() == w + 1, "bad reference string -> warning");
    mem_->setRefString("");
    stCheck(!mem_->run() && ui::warningCount() == w + 2, "empty reference string -> warning");
    mem_->setRefString("7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1");
    loadSample(sd + "/processes_srtf.csv");
    mem_->refreshPids();
    stCheck(true, "page table panel refresh");
    tabs_->setCurrentWidget(mem_);
    shot(this, "memory_pagetable");

    std::printf("[selftest] %s (%d failure(s))\n", g_stFail ? "FAILED" : "ALL PASSED", g_stFail);
    return g_stFail ? 1 : 0;
}
