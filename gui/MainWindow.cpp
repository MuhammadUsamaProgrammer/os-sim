// MainWindow.cpp - assembles the three tabs and the shared log dock.
#include "MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QFontDatabase>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QToolButton>
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
#include "SyncTab.h"
#include "DiskTab.h"
#include "ProcessTab.h"
#include "IoTab.h"
#include "MemAdvTab.h"
#include "NativeRunner.h"
#include "ModuleKit.h"
#include "Theme.h"

// Creates the tabs, the log dock (read-only, monospace) and the menus.
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OS-Sim: Operating System Simulator");
    setMinimumSize(1100, 700);

    tabs_ = new QTabWidget;
    sched_ = new SchedulingTab;
    mem_ = new MemoryTab;
    dead_ = new DeadlockTab;
    sync_ = new SyncTab;
    disk_ = new DiskTab;
    proc_ = new ProcessTab;
    io_ = new IoTab;
    tabs_->addTab(sched_, "Scheduling");
    memArea_ = new MemAdvArea(mem_);              // page replacement + Phase 2 memory sub-tabs
    tabs_->addTab(memArea_, "Memory");
    tabs_->addTab(dead_, "Deadlock");
    tabs_->addTab(sync_, "Synchronization");
    tabs_->insertTab(1, proc_, "Processes");
    tabs_->addTab(io_, "I/O");
    tabs_->addTab(disk_, "Disk");
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
    connect(sync_, &SyncTab::logMessage, this, &MainWindow::appendLog);
    connect(disk_, &DiskTab::logMessage, this, &MainWindow::appendLog);
    connect(proc_, &ProcessTab::logMessage, this, &MainWindow::appendLog);
    connect(io_, &IoTab::logMessage, this, &MainWindow::appendLog);
    connect(memArea_, &MemAdvArea::logMessage, this, &MainWindow::appendLog);

    // One shared process list: the Memory tab reads the Scheduling tab's table.
    mem_->setProcessProvider([this] {
        std::vector<PCB> p;
        if (!sched_->readProcesses(p, true)) p.clear();    // invalid table -> no processes
        return p;
    });
    connect(sched_, &SchedulingTab::processesChanged, mem_, &MemoryTab::refreshPids);
    // Same pids in the Deadlock tab: on request, and automatically after an integrated-run deadlock.
    connect(dead_, &DeadlockTab::wantProcessPids, this, [this] {
        std::vector<PCB> p;
        if (sched_->readProcesses(p)) dead_->useProcessPids(p);
    });
    connect(sched_, &SchedulingTab::integratedDeadlock, this,
            [this](const std::vector<int>& pids, const DeadlockSnapshot& d) {
        dead_->loadState(pids, d.alloc, d.max, d.request, d.avail,
                         QString("State from the integrated run at t=%1 (first deadlock). Press Detect / Recover.").arg(d.tick));
        statusBar()->showMessage("Deadlock during the integrated run: its state was copied to the Deadlock tab", 8000);
    });

    buildMenus();
    statusBar()->showMessage("Ready. Load a sample from File > Load sample, or enter processes.");
    loadSample(ui::samplesDir() + "/processes_fcfs.csv");
}

MainWindow::~MainWindow() = default;

// Selects the Memory area and its "Page replacement" sub-tab (the original Memory tab).
void MainWindow::showMemoryTab() {
    tabs_->setCurrentWidget(memArea_);
    memArea_->setCurrentWidget(mem_);
}

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
    QMenu* view = menuBar()->addMenu("&View");
    darkAction_ = view->addAction("Dark theme");
    darkAction_->setCheckable(true);
    darkAction_->setShortcut(QKeySequence("Ctrl+T"));
    connect(darkAction_, &QAction::toggled, this, &MainWindow::setDarkTheme);

    // Light/dark switch button in the top-right corner of the menu bar.
    themeButton_ = new QToolButton(this);
    themeButton_->setAutoRaise(true);
    themeButton_->setToolTip("Switch between light and dark theme (Ctrl+T)");
    connect(themeButton_, &QToolButton::clicked, this, [this] { darkAction_->toggle(); });
    menuBar()->setCornerWidget(themeButton_, Qt::TopRightCorner);
    darkAction_->setChecked(ui::darkTheme());
    setDarkTheme(ui::darkTheme());                      // sets the button text

    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("About", this, &MainWindow::showAbout);
}

// Applies the theme, updates the switch button and saves the choice for the next start.
void MainWindow::setDarkTheme(bool dark) {
    if (dark != ui::darkTheme()) ui::applyTheme(dark);
    if (darkAction_->isChecked() != dark) darkAction_->setChecked(dark);
    themeButton_->setText(dark ? QString::fromUtf8("\u2600 Light mode") : QString::fromUtf8("\u263E Dark mode"));
    if (!ui::selfTestMode()) QSettings("OS-Sim", "OS-Sim").setValue("darkTheme", dark);
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
        showMemoryTab();
    } else if (name.startsWith("banker_") || name.startsWith("deadlock_")) {
        ok = dead_->loadFile(path);
        tabs_->setCurrentWidget(dead_);
    } else if (name.startsWith("lifecycle_")) {
        ok = proc_->loadFile(path);
        tabs_->setCurrentWidget(proc_);
    } else if (name.startsWith("disk_")) {
        ok = disk_->loadFile(path);
        tabs_->setCurrentWidget(disk_);
    } else if (name.startsWith("requests_")) {
        // The integrated sample is designed for processes_srtf.csv with Round Robin q=2.
        ok = sched_->loadCsv(ui::samplesDir() + "/processes_srtf.csv");
        sched_->setAlgorithm(Algo::RR);
        sched_->setQuantum(2);
        sched_->setIntegratedOptions(path, true, true, 6, 5);
        tabs_->setCurrentWidget(sched_);
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
        "<p>CPU Scheduling, Memory Management, Deadlock Detection and Process Synchronization with a GUI.</p>"
        "<p>Operating System Lab, BS (IT) semester 5, Bahria University<br>"
        "Instructor: Amarah Abdullah</p>"
        "<p><b>Group NBA</b><br>"
        "Aagha Ali Hassan (03-135242-007)<br>"
        "Hamza Malik (03-135242-021)<br>"
        "Muhammad Usama (03-135242-038)</p>");
}

// File -> Export report...: asks for a base name, writes <name>.txt and <name>.csv.
void MainWindow::exportReport() {
    QString f = QFileDialog::getSaveFileName(this, "Export report (base name)", "ossim_report",
                                             "Report (*.txt *.csv)");
    if (f.isEmpty()) return;
    QFileInfo fi(f);
    QString base = (fi.suffix() == "txt" || fi.suffix() == "csv") ? fi.path() + "/" + fi.completeBaseName() : f;
    QString err;
    if (writeReport(base, &err))
        ui::info(this, QString("Report saved:\n%1.txt\n%1.csv").arg(base));
    else
        ui::warn(this, "Export failed: " + err);
}

// Quotes a CSV field if needed.
static QString csvField(QString s) {
    if (s.contains(',') || s.contains('"')) s = '"' + s.replace('"', "\"\"") + '"';
    return s;
}

bool MainWindow::writeReport(const QString& base, QString* err) {
    QString txt, csv;
    QTextStream T(&txt), C(&csv);
    T << "OS-Sim report - " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
      << "Group NBA: Aagha Ali Hassan, Hamza Malik, Muhammad Usama\n\n";
    C << "## OS-Sim report\ngenerated," << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";

    // 1. Processes
    T << "=== Processes ===\nPID  Arrival  Burst  Priority  Pages\n";
    C << "## Processes\npid,arrival,burst,priority,pages\n";
    std::vector<PCB> procs;
    sched_->readProcesses(procs, true);
    for (const PCB& p : procs) {
        T << QString("P%1  %2  %3  %4  %5\n").arg(p.pid, -3).arg(p.arrival, 7).arg(p.burst, 5).arg(p.priority, 8).arg(p.pages, 5);
        C << p.pid << ',' << p.arrival << ',' << p.burst << ',' << p.priority << ',' << p.pages << '\n';
    }
    // 2. Scheduling result
    T << "\n=== Scheduling result ===\n";
    C << "\n## Scheduling result\n";
    if (const auto& r = sched_->lastResult()) {
        T << "Algorithm: " << algoName(r->algo) << (r->algo == Algo::RR ? QString(" (q=%1)").arg(r->quantum) : "") << "\nGantt:";
        C << "algorithm," << csvField(algoName(r->algo)) << "\nquantum," << r->quantum << "\ngantt";
        for (const Segment& s : r->timeline) {
            QString who = s.pid < 0 ? "idle" : QString("P%1").arg(s.pid);
            T << QString(" %1[%2-%3]").arg(who).arg(s.start).arg(s.end);
            C << ',' << who << ' ' << s.start << '-' << s.end;
        }
        T << "\nPID  Arrival  Burst  Completion  Waiting  Turnaround  Response\n";
        C << "\npid,arrival,burst,completion,waiting,turnaround,response\n";
        for (const ProcMetrics& m : r->metrics) {
            T << QString("P%1 %2 %3 %4 %5 %6 %7\n").arg(m.pid, -3).arg(m.arrival, 8).arg(m.burst, 6).arg(m.completion, 11)
                     .arg(m.waiting, 8).arg(m.turnaround, 11).arg(m.response, 9);
            C << m.pid << ',' << m.arrival << ',' << m.burst << ',' << m.completion << ',' << m.waiting << ','
              << m.turnaround << ',' << m.response << '\n';
        }
        T << QString("Averages: WT=%1 TAT=%2 RT=%3  context switches=%4  CPU utilisation=%5%\n")
                 .arg(r->avgWaiting, 0, 'f', 4).arg(r->avgTurnaround, 0, 'f', 4).arg(r->avgResponse, 0, 'f', 4)
                 .arg(r->contextSwitches).arg(cpuUtilization(*r), 0, 'f', 1);
        C << "averages,,,," << r->avgWaiting << ',' << r->avgTurnaround << ',' << r->avgResponse << '\n'
          << "context_switches," << r->contextSwitches << "\ncpu_utilisation," << cpuUtilization(*r) << '\n';
    } else {
        T << "(not run)\n";
        C << "not run\n";
    }
    // 3. Comparison
    T << "\n=== Algorithm comparison ===\n";
    C << "\n## Algorithm comparison\nalgorithm,avg_waiting,avg_turnaround,avg_response,context_switches\n";
    if (sched_->lastComparison().empty()) { T << "(not run)\n"; C << "not run\n"; }
    for (const SchedResult& r : sched_->lastComparison()) {
        T << QString("%1 WT=%2 TAT=%3 RT=%4 CS=%5\n").arg(algoName(r.algo), -15).arg(r.avgWaiting, 0, 'f', 2)
                 .arg(r.avgTurnaround, 0, 'f', 2).arg(r.avgResponse, 0, 'f', 2).arg(r.contextSwitches);
        C << csvField(algoName(r.algo)) << ',' << r.avgWaiting << ',' << r.avgTurnaround << ',' << r.avgResponse
          << ',' << r.contextSwitches << '\n';
    }
    // 4. Integrated run
    T << "\n=== Integrated run ===\n";
    C << "\n## Integrated run\n";
    if (const auto& r = sched_->lastIntegrated()) {
        QStringList killed;
        for (int p : r->killedPids) killed << QString("P%1").arg(p);
        T << QString("memory faults=%1 hits=%2 hit ratio=%3; requests granted=%4 denied=%5; deadlocks detected=%6; killed: %7\n")
                 .arg(r->memFaults).arg(r->memHits).arg(r->hitRatio, 0, 'f', 3).arg(r->requestsGranted)
                 .arg(r->requestsDenied).arg(r->deadlocksDetected).arg(killed.isEmpty() ? "none" : killed.join(", "));
        C << "mem_faults," << r->memFaults << "\nmem_hits," << r->memHits << "\nhit_ratio," << r->hitRatio
          << "\nrequests_granted," << r->requestsGranted << "\nrequests_denied," << r->requestsDenied
          << "\ndeadlocks_detected," << r->deadlocksDetected << "\nkilled," << killed.join(' ') << '\n'
          << "pid,completion,waiting,turnaround,response\n";
        for (const ProcMetrics& m : r->metrics) {
            T << QString("P%1 completion=%2 waiting=%3 turnaround=%4\n").arg(m.pid).arg(m.completion).arg(m.waiting).arg(m.turnaround);
            C << m.pid << ',' << m.completion << ',' << m.waiting << ',' << m.turnaround << ',' << m.response << '\n';
        }
    } else {
        T << "(not run)\n";
        C << "not run\n";
    }
    // 5. Memory
    T << "\n=== Memory (page replacement) ===\n";
    C << "\n## Memory summary\nalgorithm,frames,references,faults,hits,hit_ratio\n";
    if (mem_->lastResults().empty()) { T << "(not run)\n"; C << "not run\n"; }
    for (const MemResult& r : mem_->lastResults()) {
        T << QString("%1, %2 frames: faults=%3 hits=%4 hit ratio=%5\n").arg(pageAlgoName(r.algo)).arg(r.numFrames)
                 .arg(r.faults).arg(r.hits).arg(r.hitRatio, 0, 'f', 3);
        C << pageAlgoName(r.algo) << ',' << r.numFrames << ',' << r.steps.size() << ',' << r.faults << ',' << r.hits
          << ',' << r.hitRatio << '\n';
    }
    if (!mem_->beladyText().isEmpty()) T << mem_->beladyText() << "\n";
    C << "\n## Memory frame table\n";
    for (const MemResult& r : mem_->lastResults()) {
        T << "\n" << pageAlgoName(r.algo) << " frame table:\nref    ";
        C << "algorithm," << pageAlgoName(r.algo) << "\nref";
        for (const MemStep& s : r.steps) { T << QString("%1").arg(s.page, 3); C << ',' << s.page; }
        for (int f = 0; f < r.numFrames; ++f) {
            T << QString("\nframe%1 ").arg(f + 1);
            C << "\nframe" << f + 1;
            for (const MemStep& s : r.steps) {
                T << (s.frames[f] < 0 ? QString("  -") : QString("%1").arg(s.frames[f], 3));
                C << ',' << (s.frames[f] < 0 ? QString() : QString::number(s.frames[f]));
            }
        }
        T << "\nresult ";
        C << "\nresult";
        for (const MemStep& s : r.steps) { T << (s.fault ? "  F" : "  H"); C << ',' << (s.fault ? "FAULT" : "HIT"); }
        T << "\n";
        C << "\n";
    }
    // 6. Deadlock
    T << "\n=== Deadlock ===\n" << dead_->reportText();
    C << "\n" << dead_->csvSection();
    // 7. Process synchronization
    T << "\n=== Synchronization ===\n" << sync_->reportText();
    C << "\n" << sync_->csvSection();
    // 8. Disk scheduling
    T << "\n=== Disk scheduling ===\n" << disk_->reportText();
    C << "\n" << disk_->csvSection();
    // 9. Process lifecycle
    T << "\n=== Process lifecycle ===\n" << proc_->reportText();
    C << "\n" << proc_->csvSection();
    // 10. I/O management
    T << "\n=== I/O management ===\n" << io_->reportText();
    C << "\n" << io_->csvSection();
    // 11. Advanced memory
    T << "\n=== Advanced memory ===\n" << memArea_->reportText();
    C << "\n" << memArea_->csvSection();
    // 12. Event log
    QString logText = log_->toPlainText();
    T << "\n=== Event log ===\n" << logText << "\n";
    C << "\n## Event log\nline\n";
    for (const QString& l : logText.split('\n')) C << csvField(l) << '\n';
    T.flush();
    C.flush();

    for (const auto& [suffix, text] : {std::pair<QString, QString>{".txt", txt}, {".csv", csv}}) {
        QFile out(base + suffix);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (err) *err = "cannot write " + base + suffix;
            return false;
        }
        out.write(text.toUtf8());
    }
    emit appendLog("Report exported to " + base + ".txt and " + base + ".csv");
    return true;
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
    QElapsedTimer t;                      // let animations (sync chips) finish first
    t.start();
    while (t.elapsed() < 450) { QApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); }
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
    showMemoryTab();
    shot(this, "memory_pagetable");

    // Deadlock: Banker's sample, request, detection sample, RAG, recovery, bad input
    stCheck(loadSample(sd + "/banker_sample.txt"), "load banker_sample.txt");
    stCheck(dead_->checkSafety() && dead_->statusText() == "SAFE: <P1, P3, P4, P0, P2>", "Banker SAFE <P1, P3, P4, P0, P2>");
    shot(this, "deadlock_safe");
    stCheck(dead_->requestResources(1, "1 0 2"), "request P1 (1 0 2) granted");
    stCheck(!dead_->requestResources(4, "3 3 0"), "request P4 (3 3 0) denied");
    stCheck(!dead_->requestResources(0, "0 2 0"), "request P0 (0 2 0) denied (unsafe)");
    w = ui::warningCount();
    stCheck(!dead_->requestResources(0, "1 x") && ui::warningCount() == w + 1, "bad request text -> warning");
    stCheck(loadSample(sd + "/deadlock_sample.txt"), "load deadlock_sample.txt");
    stCheck(dead_->detect() && dead_->lastDetection() && dead_->lastDetection()->deadlocked &&
            dead_->lastDetection()->deadlocked_pids == std::vector<int>({1, 2, 3, 4}), "detect P1..P4 deadlocked");
    shot(this, "deadlock_detected");
    bool recDone = false;
    auto rc = connect(dead_, &DeadlockTab::recoveryFinished, this, [&] { recDone = true; });
    stCheck(dead_->recover(100), "start recovery");
    stCheck(waitFor([&] { return recDone; }, 5000), "recovery finishes");
    disconnect(rc);
    stCheck(dead_->lastDetection() && !dead_->lastDetection()->deadlocked, "recovery clears the deadlock");
    shot(this, "deadlock_recovered");
    dead_->setCell("alloc", 0, 0, 99);              // Allocation > Max
    w = ui::warningCount();
    stCheck(!dead_->checkSafety() && ui::warningCount() == w + 1, "allocation > max -> warning");
    dead_->clearAll();
    stCheck(dead_->detect(), "detect on cleared tables");

    // Integrated run: avoidance on -> no deadlock; off -> deadlock detected and recovered
    stCheck(loadSample(sd + "/requests_integrated.csv"), "load requests_integrated.csv");
    stCheck(sched_->runIntegrated() && sched_->lastIntegrated()->deadlocksDetected == 0, "integrated, avoidance ON: no deadlock");
    shot(this, "integrated_avoidance_on");
    sched_->setIntegratedOptions(sd + "/requests_integrated.csv", false, true, 6, 5);
    stCheck(sched_->runIntegrated() && sched_->lastIntegrated()->deadlocksDetected >= 1 &&
            !sched_->lastIntegrated()->killedPids.empty(), "integrated, avoidance OFF: deadlock detected + recovered");
    shot(this, "integrated_avoidance_off");
    stCheck(dead_->rowPids() == std::vector<int>({1, 2, 3, 4}), "Deadlock tab shows the integrated state with the same pids");
    stCheck(dead_->detect() && dead_->lastDetection()->deadlocked, "integrated state is deadlocked in the Deadlock tab");
    tabs_->setCurrentWidget(dead_);
    shot(this, "integrated_in_deadlock_tab");
    w = ui::warningCount();
    sched_->setIntegratedOptions(sd + "/does_not_exist.csv", true, true, 6, 5);
    stCheck(!sched_->runIntegrated() && ui::warningCount() == w + 1, "missing requests file -> warning");
    sched_->setIntegratedOptions(sd + "/requests_integrated.csv", true, true, 6, 5);

    // Synchronization: every algorithm and scenario, lost update, Peterson, controls, comparison, real threads
    tabs_->setCurrentWidget(sync_);
    for (int a = 0; a <= (int)SyncAlgo::MONITOR; ++a) {
        sync_->setAlgorithm((SyncAlgo)a);
        int n = (int)syncScenarios((SyncAlgo)a).size();
        for (int i = 0; i < n; ++i) {
            bool ok = sync_->loadScenario(i) && sync_->runToEnd() > 0 && sync_->sim()->stats().completed;
            if ((SyncAlgo)a != SyncAlgo::RACE) ok = ok && sync_->sim()->stats().csViolations == 0 && sync_->sim()->stats().lostUpdates == 0;
            stCheck(ok, QString("sync %1, scenario %2 completes correctly").arg(syncAlgoName((SyncAlgo)a)).arg(i + 1));
        }
    }
    sync_->setAlgorithm(SyncAlgo::RACE);
    stCheck(sync_->loadScenario(0) && sync_->runToEnd() == 6 && sync_->sim()->stats().actual == 1 &&
            sync_->sim()->stats().lostUpdates == 1, "race: lost update, counter 1 instead of 2");
    shot(this, "sync_race_lost_update");
    {   // manual interleaving through the process cards' Step action
        SyncConfig c;
        c.algo = SyncAlgo::RACE;
        c.processes = 2;
        c.iterations = 1;
        stCheck(sync_->applyConfig(c), "race: custom configuration");
        bool ok = true;
        for (int p : {0, 1, 0, 0, 1, 1}) ok = ok && sync_->stepProcess(p);
        stCheck(ok && sync_->sim()->counter() == 1 && !sync_->stepProcess(0), "race: manual interleaving P0 P1 P0 P0 P1 P1 loses an update");
    }
    sync_->setAlgorithm(SyncAlgo::PETERSON);
    stCheck(sync_->loadScenario(0), "Peterson: both want to enter");
    for (int i = 0; i < 9; ++i) sync_->stepOnce();
    stCheck(sync_->sim()->procs()[0].inCS && sync_->sim()->procs()[1].state == SyncProcState::Spinning,
            "Peterson: P0 in the critical section, P1 busy-waits");
    shot(this, "sync_peterson");
    sync_->runToEnd();
    stCheck(sync_->sim()->stats().completed && sync_->sim()->stats().csViolations == 0 && sync_->sim()->counter() == 2,
            "Peterson: mutual exclusion, counter 2");
    sync_->setAlgorithm(SyncAlgo::PRODUCER_CONSUMER);
    stCheck(sync_->loadScenario(0), "producer-consumer: fast producer");
    // The producer (pid 0) must end up asleep on the "empty" semaphore (object 0) when empty == 0 (no free slot).
    for (int i = 0; i < 200 && sync_->sim()->procs()[0].waitingOn != 0; ++i) sync_->stepOnce();
    stCheck(sync_->sim()->procs()[0].state == SyncProcState::Blocked && sync_->sim()->procs()[0].waitingOn == 0 &&
            sync_->sim()->objects()[0].value == 0, "producer-consumer: producer blocks on wait(empty) when empty = 0");
    for (int i = 0; i < 200 && !(sync_->sim()->procs()[0].waitingOn == 0 && sync_->sim()->bufferCount() == 3); ++i)
        sync_->stepOnce();
    stCheck(sync_->sim()->bufferCount() == 3 && sync_->sim()->procs()[0].state == SyncProcState::Blocked,
            "producer-consumer: buffer full (3 of 3), producer asleep");
    shot(this, "sync_producer_consumer");
    sync_->setAlgorithm(SyncAlgo::MONITOR);
    stCheck(sync_->loadScenario(1), "monitor: fast consumer");
    for (int i = 0; i < 10; ++i) sync_->stepOnce();
    shot(this, "sync_monitor");
    // Animated run: run, pause, resume, stop, reset - never hangs.
    sync_->setAlgorithm(SyncAlgo::MUTEX);
    sync_->setSpeed(50);
    sync_->run();
    stCheck(sync_->animating(), "sync: animated run starts");
    stCheck(waitFor([&] { return sync_->sim()->stats().steps >= 4; }, 5000), "sync: animation advances");
    sync_->pause();
    int stepsAtPause = sync_->sim()->stats().steps;
    waitFor([] { return false; }, 200);
    stCheck(!sync_->animating() && sync_->sim()->stats().steps == stepsAtPause, "sync: pause stops the animation");
    sync_->resume();
    stCheck(waitFor([&] { return sync_->sim()->stats().steps > stepsAtPause; }, 5000), "sync: resume continues");
    sync_->stop();
    stCheck(!sync_->animating(), "sync: stop");
    stCheck(sync_->resetSim() && sync_->sim()->stats().steps == 0, "sync: reset");
    sync_->run();
    stCheck(waitFor([&] { return sync_->sim()->finished(); }, 15000), "sync: animated run to the end");
    stCheck(sync_->sim()->stats().completed && sync_->sim()->stats().lostUpdates == 0, "sync: mutex result correct");
    // Bad input: more consumers than items -> warning, no crash
    w = ui::warningCount();
    {
        SyncConfig bad;
        bad.algo = SyncAlgo::PRODUCER_CONSUMER;
        bad.producers = 1; bad.consumers = 3; bad.iterations = 1;
        stCheck(!sync_->applyConfig(bad) && ui::warningCount() == w + 1, "sync: invalid settings -> warning");
    }
    // Comparison of the four mechanisms
    stCheck(sync_->compare() && sync_->lastComparison().size() == 4, "sync: comparison has 4 rows");
    {
        const auto& r = sync_->lastComparison();
        bool ok = r[0].stats.lostUpdates > 0;
        for (int i = 1; i < 4; ++i) ok = ok && r[i].stats.lostUpdates == 0 && r[i].stats.csViolations == 0 && r[i].stats.completed;
        stCheck(ok, "sync: unsynchronized loses updates, Peterson / mutex / semaphore do not");
    }
    shot(this, "sync_comparison");
    // Real POSIX threads (background thread + queued signals)
    sync_->setAlgorithm(SyncAlgo::MUTEX);
    stCheck(sync_->runRealDemo(), "sync: real pthread_mutex demo starts");
    stCheck(waitFor([&] { return !sync_->realDemoRunning(); }, 20000), "sync: real demo finishes");
    stCheck(sync_->lastDemo() && sync_->lastDemo()->ok, "sync: real mutex demo checks pass");
    sync_->setAlgorithm(SyncAlgo::PRODUCER_CONSUMER);
    stCheck(sync_->runRealDemo(), "sync: real producer-consumer demo starts");
    waitFor([] { return false; }, 150);
    sync_->stopRealDemo();
    stCheck(waitFor([&] { return !sync_->realDemoRunning(); }, 5000), "sync: real demo stops on request");
    sync_->setAlgorithm(SyncAlgo::MONITOR);
    stCheck(sync_->runRealDemo() && waitFor([&] { return !sync_->realDemoRunning(); }, 20000) &&
            sync_->lastDemo()->ok, "sync: real monitor demo (pthread_cond) checks pass");
    shot(this, "sync_posix_demo");

    // ---- Phase 2, M1: Disk scheduling
    tabs_->setCurrentWidget(disk_);
    stCheck(loadSample(sd + "/disk_reference.txt"), "load disk_reference.txt");
    {
        const int expect[6] = {640, 236, 331, 382, 299, 322};
        bool ok = disk_->comparison().size() == 6;
        for (int i = 0; ok && i < 6; ++i) ok = disk_->comparison()[i].total == expect[i];
        stCheck(ok, "disk: FCFS 640, SSTF 236, SCAN 331, C-SCAN 382, LOOK 299, C-LOOK 322");
    }
    disk_->setAlgorithm(DiskAlgo::SCAN);
    stCheck(disk_->result() && disk_->result()->algo == DiskAlgo::SCAN && disk_->playback()->length() == 9,
            "disk: SCAN trace has 9 moves (8 services + 1 boundary transit)");
    for (int i = 0; i < 4; ++i) disk_->playback()->next();
    disk_->playback()->prev();
    stCheck(disk_->playback()->position() == 3 && disk_->statusText().contains("step 3"), "disk: next x4, prev -> step 3");
    shot(this, "disk_scan");
    disk_->playback()->setIntervalMs(60);
    disk_->playback()->run();
    stCheck(waitFor([&] { return disk_->playback()->position() == disk_->playback()->length(); }, 5000) &&
            disk_->statusText().contains("total 331"), "disk: playback runs to the end, total 331");
    disk_->playback()->stop();
    stCheck(disk_->playback()->position() == 0, "disk: stop returns to step 0");
    for (int p = 0; p < 7; ++p) stCheck(disk_->loadPreset(p), QString("disk preset %1").arg(p + 1));
    {
        DiskConfig bad;
        bad.requests = {10, 500};
        stCheck(!disk_->setConfig(bad) && disk_->statusText().contains("outside"), "disk: request outside the range -> message");
    }
    disk_->loadPreset(0);
    shot(this, "disk_reference");

    // ---- Phase 2, M2: Process management
    tabs_->setCurrentWidget(proc_);
    stCheck(loadSample(sd + "/lifecycle_reference.csv"), "load lifecycle_reference.csv");
    {
        std::vector<QString> seq;
        for (const LcSnapshot& s : proc_->sim()->trace())
            if (s.event != LcEvent::Execute && s.event != LcEvent::Idle) seq.push_back(QString("%1 P%2").arg(lcEventName(s.event)).arg(s.pid));
        QStringList got(seq.begin(), seq.end());
        stCheck(got.join(", ") == "create P2, admit P2, create P3, admit P3, dispatch P2, block on I/O P2, dispatch P3, "
                                  "I/O complete P2, exit P3, dispatch P2, exit P2", "process: reference lifecycle sequence " + got.join(", "));
    }
    proc_->playback()->setPosition(8);
    shot(this, "process_lifecycle");
    proc_->playback()->setPosition(proc_->playback()->length());
    stCheck(proc_->statusText().contains("all processes terminated"), "process: last step, all terminated");
    for (int p = 0; p < 4; ++p) stCheck(proc_->loadPreset(p) && proc_->sim()->allTerminated(), QString("process preset %1 completes").arg(p + 1));
    proc_->loadPreset(0);
    proc_->setManual(true);
    stCheck(proc_->command("Admit", 2) == "" && proc_->command("Admit", 3) == "" && proc_->command("Dispatch", 2) == "",
            "process manual: admit, admit, dispatch");
    stCheck(proc_->command("Dispatch", 3) != "", "process manual: second dispatch refused (one CPU)");
    stCheck(proc_->command("Block I/O", 2, 2) == "" && proc_->command("Dispatch", 2) != "",
            "process manual: block on I/O, Waiting -> Running refused");
    int len = proc_->playback()->length();
    proc_->playback()->prev();
    stCheck(proc_->command("Kill", 3) == "" && proc_->playback()->length() == len,
            "process manual: command after Prev discards the future (new branch)");
    shot(this, "process_manual");
    proc_->setManual(false);
    if (NativeRunner::available()) {
        for (const char* d : {"fork", "exec", "wait", "zombie", "orphan", "signal", "sleep"}) {
            bool started = proc_->startDemo(d);
            bool done = started && waitFor([&] { return !proc_->demoRunning(); }, 10000);
            stCheck(done && proc_->demoExitCode() == 0, QString("process live demo %1 exits 0").arg(d));
        }
        shot(this, "process_live_demo");
        stCheck(proc_->startDemo("sleep"), "process live demo: start sleep");
        waitFor([] { return false; }, 200);
        proc_->stopDemo();
        stCheck(waitFor([&] { return !proc_->demoRunning(); }, 4000), "process live demo: Stop ends it");
    }

    // ---- Phase 2, M5: I/O management
    tabs_->setCurrentWidget(io_);
    stCheck(io_->loadPreset(5), "io: hand-traced preset");
    {
        auto seq = [](const IoResult& r) {
            QString s;
            for (const IoTick& k : r.ticks) s += QString(cpuActName(k.cpu)).left(1);
            return s;
        };
        const auto& c = io_->comparison();
        stCheck(c.size() == 3 && c[0].m.completion == 8 && c[1].m.completion == 8 && c[2].m.completion == 8,
                "io: all three modes complete at t=8");
        stCheck(c.size() == 3 && c[0].m.polls == 4 && c[1].m.interrupts == 2 && c[2].m.interrupts == 1 && c[2].m.copy == 0,
                "io: 4 polls / 2 interrupts / 1 DMA completion interrupt, DMA copies nothing");
        io_->setMode(IoMode::Interrupt);
        stCheck(io_->result() && seq(*io_->result()) == "UUUICUIC", "io: interrupt CPU sequence U U U I C U I C");
    }
    for (int i = 0; i < 4; ++i) io_->playback()->next();
    shot(this, "io_interrupt");
    io_->setMode(IoMode::DMA);
    io_->playback()->setIntervalMs(60);
    io_->playback()->run();
    stCheck(waitFor([&] { return io_->playback()->position() == io_->playback()->length(); }, 5000) &&
            io_->statusText().contains("DONE: latency 8"), "io: DMA playback to the end, latency 8");
    io_->playback()->prev();
    stCheck(io_->playback()->position() == io_->playback()->length() - 1, "io: prev after the end");
    for (int p = 0; p < 6; ++p) stCheck(io_->loadPreset(p), QString("io preset %1").arg(p + 1));
    io_->loadPreset(2);
    stCheck(io_->comparison().size() == 3 && io_->comparison()[2].m.useful > io_->comparison()[1].m.useful,
            "io: bulk transfer, DMA leaves more useful CPU work than interrupts");
    shot(this, "io_bulk_dma");

    // ---- Phase 2, M4: Advanced memory
    tabs_->setCurrentWidget(memArea_);
    memArea_->setCurrentIndex(1);
    stCheck(memArea_->loadAllocPreset(0) && memArea_->allocResults().size() == 3, "memory: allocation preset");
    {
        const auto& a = memArea_->allocResults();
        stCheck(!a[0].steps[3].ok && a[1].steps[3].ok && !a[2].steps[3].ok,
                "memory: 426 fails with First and Worst Fit, fits with Best Fit");
        stCheck(a[0].steps[3].externalFragFailure, "memory: First Fit failure is external fragmentation");
    }
    for (int i = 0; i < 4; ++i) memArea_->allocBar()->next();
    shot(this, "memory_allocation");
    for (int p = 1; p < 4; ++p) stCheck(memArea_->loadAllocPreset(p), QString("memory allocation preset %1").arg(p + 1));
    memArea_->setCurrentIndex(2);
    stCheck(memArea_->loadPagingPreset(0) && memArea_->pagingResult()->ok() && memArea_->pagingResult()->page == 2 &&
            memArea_->pagingResult()->offset == 452 && memArea_->pagingResult()->physical == 5572, "paging: 2500 -> page 2, offset 452, physical 5572");
    memArea_->pagingBar()->setPosition(memArea_->pagingBar()->length());
    shot(this, "memory_paging");
    stCheck(memArea_->loadPagingPreset(4) && memArea_->pagingResult()->outcome == XlateOutcome::PageFault, "paging: non-resident page -> page fault");
    stCheck(memArea_->loadPagingPreset(6) && memArea_->pagingResult()->outcome == XlateOutcome::OutOfRange, "paging: out of range");
    stCheck(memArea_->loadPagingPreset(7) && memArea_->pagingResult()->outcome == XlateOutcome::Protection, "paging: write to read-only page");
    memArea_->setCurrentIndex(3);
    stCheck(memArea_->loadSegPreset(0) && memArea_->segResult()->physical == 4250, "segmentation: (2, 250) -> 4250");
    stCheck(memArea_->loadSegPreset(1) && memArea_->segResult()->outcome == XlateOutcome::Protection, "segmentation: offset 600 -> protection fault");
    memArea_->setCurrentIndex(4);
    stCheck(memArea_->loadDemandPreset(0) && memArea_->demandResults().size() == 3 && memArea_->demandResults()[0].faults == 10 &&
            memArea_->demandResults()[1].faults == 9 && memArea_->demandResults()[2].faults == 7, "demand paging: FIFO 10, LRU 9, Optimal 7");
    for (int i = 0; i < 8; ++i) memArea_->demandBar()->next();
    shot(this, "memory_demand_paging");
    stCheck(memArea_->loadDemandPreset(1) && memArea_->demandResults()[0].writeBacks > 0, "demand paging: dirty write-backs");
    memArea_->setCurrentIndex(5);
    stCheck(memArea_->rebuildSwap() && memArea_->swapResult() && memArea_->swapResult()->swapOuts > 0, "swapping: whole-process swap-outs");
    memArea_->setCurrentIndex(6);
    stCheck(memArea_->loadThrashPreset(0) && memArea_->thrashA()->faults == 20 && memArea_->thrashB()->faults == 4,
            "thrashing: cyclic 0-3 x5, LRU 3 frames -> 20 faults, 4 frames -> 4");
    shot(this, "memory_thrashing");
    showMemoryTab();
    stCheck(mem_->isVisible(), "memory: page-replacement sub-tab still available");

    // Export: both files, every section
    sched_->runOnce();
    sched_->compareAll();
    QString base = QDir::temp().filePath(QString("ossim_selftest_%1").arg(QCoreApplication::applicationPid()));
    QString err;
    // Theme switch: dark and back to light must work and keep the window alive
    setDarkTheme(true);
    stCheck(ui::darkTheme() && darkAction_->isChecked(), "switch to dark theme");
    setDarkTheme(false);
    stCheck(!ui::darkTheme() && !darkAction_->isChecked(), "switch back to light theme");

    stCheck(writeReport(base, &err), "export report " + err);
    QFile tf(base + ".txt"), cf(base + ".csv");
    bool opened = tf.open(QIODevice::ReadOnly) && cf.open(QIODevice::ReadOnly);
    QString t = opened ? QString::fromUtf8(tf.readAll()) : QString(), c = opened ? QString::fromUtf8(cf.readAll()) : QString();
    bool allTxt = true, allCsv = true;
    for (const char* sec : {"=== Processes", "=== Scheduling result", "=== Algorithm comparison", "=== Integrated run",
                            "=== Memory", "=== Deadlock", "=== Synchronization", "=== Disk scheduling", "=== Process lifecycle", "=== I/O management", "=== Advanced memory", "=== Event log"}) allTxt = allTxt && t.contains(sec);
    for (const char* sec : {"## Processes", "## Scheduling result", "## Algorithm comparison", "## Integrated run",
                            "## Memory summary", "## Memory frame table", "## Deadlock state", "## Deadlock result",
                            "## Synchronization", "## Synchronization comparison", "## Synchronization POSIX demo", "## Disk scheduling", "## Disk trace", "## Process lifecycle", "## I/O management", "## Memory allocation", "## Address translation", "## Demand paging",
                            "## Event log"}) allCsv = allCsv && c.contains(sec);
    stCheck(opened && allTxt, "report .txt has all sections");
    stCheck(opened && allCsv, "report .csv has all sections");
    tf.close(); cf.close();
    QFile::remove(base + ".txt");
    QFile::remove(base + ".csv");

    std::printf("[selftest] %s (%d failure(s))\n", g_stFail ? "FAILED" : "ALL PASSED", g_stFail);
    return g_stFail ? 1 : 0;
}
