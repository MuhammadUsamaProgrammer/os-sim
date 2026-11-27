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
#include <QRegularExpression>
#include <QScreen>
#include <QSplitterHandle>
#include <QMouseEvent>
#include <QScrollBar>
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
#include "IpcTab.h"
#include "ThreadsTab.h"
#include "StorageTab.h"
#include "HeapLab.h"
#include "SysMonTab.h"
#include "JobControlTab.h"
#include "RtSchedTab.h"
#include "MlqTab.h"
#include "LayoutAudit.h"
#include "PanelLayout.h"
#include "NativeRunner.h"
#include "ModuleKit.h"
#include "Theme.h"

// Creates the tabs, the log dock (read-only, monospace) and the menus.
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OS-Sim: Operating System Simulator");
    // No hard minimum size: the layouts are responsive (see PanelLayout.h). Start at a size that
    // fits the available screen area (logical pixels, so display scaling is respected).
    if (QScreen* scr = QGuiApplication::primaryScreen()) {
        QRect a = scr->availableGeometry();
        resize(std::min(a.width() * 9 / 10, 1600), std::min(a.height() * 9 / 10, 1000));
    }

    tabs_ = new QTabWidget;
    sched_ = new SchedulingTab;
    mem_ = new MemoryTab;
    dead_ = new DeadlockTab;
    sync_ = new SyncTab;
    disk_ = new DiskTab;
    proc_ = new ProcessTab;
    io_ = new IoTab;
    ipc_ = new IpcTab;
    threads_ = new ThreadsTab;
    schedArea_ = new QTabWidget;                  // Scheduling: the original page + Phase 3 child views
    schedArea_->addTab(sched_, "CPU Scheduling");
    mlq_ = new MlqPage;                           // confirmed gap: MLQ / MLFQ (course outline)
    schedArea_->addTab(mlq_, "Multilevel Queues");
    rt_ = new RtSchedPage;
    schedArea_->addTab(rt_, "Real-Time Scheduling");
    tabs_->addTab(new PageScroll(schedArea_), "Scheduling");
    memArea_ = new MemAdvArea(mem_);              // page replacement + Phase 2 memory sub-tabs
    tabs_->addTab(new PageScroll(memArea_), "Memory");
    tabs_->addTab(new PageScroll(dead_), "Deadlock");
    tabs_->addTab(new PageScroll(sync_), "Synchronization");
    procArea_ = new QTabWidget;                   // Processes: lifecycle simulation + Phase 3 child views
    procArea_->addTab(proc_, "Process Lifecycle");
    sysmon_ = new SystemMonitorPage;
    procArea_->addTab(sysmon_, "System Monitor");
    jobs_ = new JobControlPage;
    procArea_->addTab(jobs_, "Job Control");
    tabs_->insertTab(1, new PageScroll(procArea_), "Processes");
    tabs_->addTab(new PageScroll(threads_), "Threads");
    tabs_->addTab(new PageScroll(ipc_), "IPC");
    tabs_->addTab(new PageScroll(io_), "I/O");
    tabs_->addTab(new PageScroll(disk_), "Disk");
    storage_ = new StorageArea;                   // Phase 3: the only new parent tab
    tabs_->addTab(new PageScroll(storage_), "Storage && Files");
    setCentralWidget(tabs_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMaximumBlockCount(20000);
    auto* dock = new QDockWidget("Event log", this);
    logDock_ = dock;
    dock->setObjectName("logDock");
    dock->setWidget(log_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    // The shared event log starts small (about 12% of the window height); it can be resized or floated.
    log_->setMinimumHeight(fontMetrics().height() * 3);
    resizeDocks({dock}, {std::max(fontMetrics().height() * 5, height() * 12 / 100)}, Qt::Vertical);

    connect(sched_, &SchedulingTab::logMessage, this, &MainWindow::appendLog);
    connect(mem_, &MemoryTab::logMessage, this, &MainWindow::appendLog);
    connect(dead_, &DeadlockTab::logMessage, this, &MainWindow::appendLog);
    connect(sync_, &SyncTab::logMessage, this, &MainWindow::appendLog);
    connect(disk_, &DiskTab::logMessage, this, &MainWindow::appendLog);
    connect(proc_, &ProcessTab::logMessage, this, &MainWindow::appendLog);
    connect(io_, &IoTab::logMessage, this, &MainWindow::appendLog);
    connect(memArea_, &MemAdvArea::logMessage, this, &MainWindow::appendLog);
    connect(ipc_, &IpcTab::logMessage, this, &MainWindow::appendLog);
    connect(threads_, &ThreadsTab::logMessage, this, &MainWindow::appendLog);
    connect(storage_, &StorageArea::logMessage, this, &MainWindow::appendLog);
    connect(sysmon_, &SystemMonitorPage::logMessage, this, &MainWindow::appendLog);
    connect(jobs_, &JobControlPage::logMessage, this, &MainWindow::appendLog);
    connect(rt_, &RtSchedPage::logMessage, this, &MainWindow::appendLog);
    connect(mlq_, &MlqPage::logMessage, this, &MainWindow::appendLog);
    mlq_->setProcessProvider([this] {
        std::vector<PCB> p;
        if (!sched_->readProcesses(p, true)) p.clear();
        return p;
    });

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

// Children may still emit signals while they are destroyed (e.g. a live demo helper that
// finishes during shutdown): cut every connection to this window first.
MainWindow::~MainWindow() {
    const auto kids = findChildren<QObject*>();
    for (QObject* o : kids) o->disconnect(this);
}

// Visible PanelSplitters of the page that is shown now (hidden sub-tab pages are skipped).
QList<PanelSplitter*> MainWindow::currentSplitters() const {
    QList<PanelSplitter*> v;
    for (PanelSplitter* s : ui::panelSplitters(tabs_->currentWidget()))
        if (s->isVisibleTo(tabs_->currentWidget()) && (!s->parentWidget() || s->parentWidget()->isVisibleTo(tabs_->currentWidget()))) v << s;
    return v;
}

void MainWindow::resetPanelLayout() {
    setFocusView(false);
    for (PanelSplitter* s : currentSplitters()) s->resetLayout();
    syncViewActions();
    statusBar()->showMessage("Panel layout restored to the defaults", 3000);
}

void MainWindow::setConfigPanelsVisible(bool on) {
    for (PanelSplitter* s : currentSplitters()) if (s->hasPanel(PanelSplitter::Config)) s->setPanelVisible(PanelSplitter::Config, on);
    syncViewActions();
}

void MainWindow::setInspectorPanelsVisible(bool on) {
    for (PanelSplitter* s : currentSplitters()) if (s->hasPanel(PanelSplitter::Inspector)) s->setPanelVisible(PanelSplitter::Inspector, on);
    syncViewActions();
}

// Focus mode: only the visualization remains; the previous layout comes back when it ends.
void MainWindow::setFocusView(bool on) {
    if (on == focusView()) { if (focusAction_) focusAction_->setChecked(on); return; }
    if (on) {
        focused_ = currentSplitters();
        for (PanelSplitter* s : focused_) s->setFocusView(true);
        logWasVisible_ = logDock_->isVisible();
        logDock_->hide();
    } else {
        for (PanelSplitter* s : focused_) s->setFocusView(false);
        focused_.clear();
        if (logWasVisible_) logDock_->show();
    }
    if (focusAction_) focusAction_->setChecked(on);
}

// Check marks / enabled state of the View menu follow the current tab.
void MainWindow::syncViewActions() {
    if (!configAction_) return;
    bool hasC = false, visC = false, hasI = false, visI = false;
    for (PanelSplitter* s : currentSplitters()) {
        if (s->hasPanel(PanelSplitter::Config)) { hasC = true; visC = visC || s->panelVisible(PanelSplitter::Config); }
        if (s->hasPanel(PanelSplitter::Inspector)) { hasI = true; visI = visI || s->panelVisible(PanelSplitter::Inspector); }
    }
    configAction_->setEnabled(hasC && !focusView());
    configAction_->setChecked(visC);
    inspectorAction_->setEnabled(hasI && !focusView());
    inspectorAction_->setChecked(visI);
    focusAction_->setChecked(focusView());
}

// Shows the page: selects its top-level tab and, for a page inside a sub-tab area (Memory,
// Scheduling, Processes, Storage & Files), every enclosing sub-tab.
void MainWindow::showTab(QWidget* page) {
    for (QWidget* w = page; w; w = w->parentWidget()) {
        QWidget* stack = w->parentWidget();
        if (auto* tw = stack ? qobject_cast<QTabWidget*>(stack->parentWidget()) : nullptr)
            if (tw->indexOf(w) >= 0) tw->setCurrentWidget(w);
        if (w == tabs_) break;
    }
}

// The module widget of tab i (unwrapping the PageScroll container).
QWidget* MainWindow::pageOf(int i) const {
    auto* ps = qobject_cast<PageScroll*>(tabs_->widget(i));
    return ps ? ps->widget() : tabs_->widget(i);
}

// Selects the Memory area and its "Page replacement" sub-tab (the original Memory tab).
void MainWindow::showMemoryTab() {
    showTab(memArea_);
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
    view->addSeparator();
    // Panel layout of the current tab (shared PanelSplitter, see PanelLayout.h)
    QAction* reset = view->addAction("Reset Panel Layout", QKeySequence("Ctrl+Shift+R"), this, &MainWindow::resetPanelLayout);
    reset->setToolTip("Restore the default panel sizes of this tab (also: double-click a splitter handle)");
    configAction_ = view->addAction("Show Configuration Panel");
    configAction_->setCheckable(true);
    configAction_->setShortcut(QKeySequence("Ctrl+Shift+C"));
    connect(configAction_, &QAction::triggered, this, &MainWindow::setConfigPanelsVisible);
    inspectorAction_ = view->addAction("Show Inspector Panel");
    inspectorAction_->setCheckable(true);
    inspectorAction_->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(inspectorAction_, &QAction::triggered, this, &MainWindow::setInspectorPanelsVisible);
    focusAction_ = view->addAction("Focus Simulation View");
    focusAction_->setCheckable(true);
    focusAction_->setShortcut(QKeySequence("Ctrl+Shift+F"));
    focusAction_->setToolTip("Hide the side panels, the bottom area and the event log; toggle again to restore");
    connect(focusAction_, &QAction::triggered, this, &MainWindow::setFocusView);
    view->addAction(logDock_->toggleViewAction());
    connect(view, &QMenu::aboutToShow, this, &MainWindow::syncViewActions);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { setFocusView(false); syncViewActions(); });
    connect(memArea_, &QTabWidget::currentChanged, this, [this] { setFocusView(false); syncViewActions(); });

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
        showTab(sched_);
    } else if (name.startsWith("refstring_")) {
        ok = mem_->loadRefFile(path);
        showMemoryTab();
    } else if (name.startsWith("banker_") || name.startsWith("deadlock_")) {
        ok = dead_->loadFile(path);
        showTab(dead_);
    } else if (name.startsWith("lifecycle_")) {
        ok = proc_->loadFile(path);
        showTab(proc_);
    } else if (name.startsWith("disk_")) {
        ok = disk_->loadFile(path);
        showTab(disk_);
    } else if (name.startsWith("requests_")) {
        // The integrated sample is designed for processes_srtf.csv with Round Robin q=2.
        ok = sched_->loadCsv(ui::samplesDir() + "/processes_srtf.csv");
        sched_->setAlgorithm(Algo::RR);
        sched_->setQuantum(2);
        sched_->setIntegratedOptions(path, true, true, 6, 5);
        showTab(sched_);
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
    // 12. IPC
    T << "\n=== IPC ===\n" << ipc_->reportText();
    C << "\n" << ipc_->csvSection();
    // 13. Threading models
    T << "\n=== Threading models ===\n" << threads_->reportText();
    C << "\n" << threads_->csvSection();
    // 14. Storage and files
    T << "\n=== Multilevel queues ===\n" << mlq_->reportText();
    T << "\n=== Real-time scheduling ===\n" << rt_->reportText();
    T << "\n=== Job control (simulated shell) ===\n" << jobs_->reportText();
    T << "\n=== Storage and files (virtual disk) ===\n" << storage_->reportText();
    C << "\n" << storage_->csvSection();
    // 15. Event log
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

QStringList MainWindow::layoutReport(const QList<QSize>& sizes, bool print) {
    QStringList problems;
    show();
    for (const QSize& sz : sizes) {
        resize(sz);
        QApplication::processEvents();
        QApplication::processEvents();
        if (width() > sz.width() + 1 || height() > sz.height() + 1)
            problems << QString("%1x%2: the window cannot shrink to this size (it is %3x%4, minimum %5x%6)")
                            .arg(sz.width()).arg(sz.height()).arg(width()).arg(height())
                            .arg(minimumSizeHint().width()).arg(minimumSizeHint().height());
        auto check = [&](QWidget* page, const QString& name) {
            QApplication::processEvents();
            QApplication::processEvents();
            LayoutAuditResult a = auditControls(page, QString("%1x%2 %3").arg(sz.width()).arg(sz.height()).arg(name));
            int pageScrollH = 0, pageScrollV = 0;
            for (QWidget* p = page; p; p = p->parentWidget())
                if (auto* ps = qobject_cast<PageScroll*>(p)) {
                    pageScrollH = ps->horizontalScrollBar()->maximum();
                    pageScrollV = ps->verticalScrollBar()->maximum();
                }
            if (!qEnvironmentVariable("OSSIM_LAYOUT_DEBUG").isEmpty()) {
                // the deepest widgets that demand a large minimum width / height (layout culprits)
                int lim = qEnvironmentVariableIntValue("OSSIM_LAYOUT_DEBUG");
                for (QWidget* w : page->findChildren<QWidget*>()) {
                    if (!w->isVisible()) continue;
                    QSize m = w->minimumSizeHint().expandedTo(w->minimumSize());
                    if (m.width() < lim && m.height() < lim * 3 / 4) continue;
                    bool childBig = false;
                    for (QWidget* c : w->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                        QSize cm = c->minimumSizeHint().expandedTo(c->minimumSize());
                        if (c->isVisible() && ((cm.width() >= m.width() - 30 && m.width() >= lim) || (cm.height() >= m.height() - 30 && m.height() >= lim * 3 / 4))) childBig = true;
                    }
                    if (!childBig) std::printf("[layout-debug] %s: %s '%s' min %dx%d\n", qPrintable(name), w->metaObject()->className(),
                                               qPrintable(w->objectName()), m.width(), m.height());
                }
            }
            problems << a.problems;
            if (print)
                std::printf("[layout] %4dx%-4d %-34s page min %4dx%-4d page scroll %3d/%3d | controls %3d, by scrolling %2d, INACCESSIBLE %d\n",
                            sz.width(), sz.height(), qPrintable(name), page->minimumSizeHint().width(), page->minimumSizeHint().height(),
                            pageScrollH, pageScrollV, a.checked, a.needScroll, (int)a.problems.size());
            if (!qEnvironmentVariable("OSSIM_SHOTS").isEmpty()) {
                QString dir = qEnvironmentVariable("OSSIM_SHOTS");
                QDir().mkpath(dir);
                QString f = QString("%1/layout_%2x%3_%4.png").arg(dir).arg(sz.width()).arg(sz.height()).arg(QString(name).replace(QRegularExpression("[^A-Za-z0-9]+"), "_"));
                grab().save(f);
            }
        };
        for (int i = 0; i < tabs_->count(); ++i) {
            tabs_->setCurrentIndex(i);
            if (auto* area = qobject_cast<QTabWidget*>(pageOf(i))) {      // sub-tab area: audit every child view
                for (int k = 0; k < area->count(); ++k) {
                    area->setCurrentIndex(k);
                    check(area->widget(k), tabs_->tabText(i).replace("&&", "&") + " > " + area->tabText(k).replace("&&", "&"));
                }
            } else {
                check(tabs_->widget(i), tabs_->tabText(i));   // audit through the page scroll container
            }
        }
    }
    if (print) {
        for (const QString& p : problems) std::printf("[layout] PROBLEM %s\n", qPrintable(p));
        std::printf("[layout] %d problem(s)\n", (int)problems.size());
    }
    return problems;
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
    showTab(dead_);
    shot(this, "integrated_in_deadlock_tab");
    w = ui::warningCount();
    sched_->setIntegratedOptions(sd + "/does_not_exist.csv", true, true, 6, 5);
    stCheck(!sched_->runIntegrated() && ui::warningCount() == w + 1, "missing requests file -> warning");
    sched_->setIntegratedOptions(sd + "/requests_integrated.csv", true, true, 6, 5);

    // Synchronization: every algorithm and scenario, lost update, Peterson, controls, comparison, real threads
    showTab(sync_);
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
    showTab(disk_);
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
    showTab(proc_);
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
    showTab(io_);
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
    showTab(memArea_);
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

    // ---- Phase 2, M3: IPC
    showTab(ipc_);
    stCheck(ipc_->loadPreset(0) && ipc_->sim()->trace().size() == 9, "ipc: shared-memory preset loaded (8 operations)");
    {
        std::string read;
        for (const IpcSnapshot& s : ipc_->sim()->trace()) if (!s.procs[1].lastData.empty()) read = s.procs[1].lastData;
        stCheck(read == "HELLO" && !ipc_->sim()->trace().back().shmExists, "ipc: client reads HELLO, segment removed at the end");
    }
    for (int i = 0; i < 5; ++i) ipc_->playback()->next();
    shot(this, "ipc_shared_memory");
    stCheck(ipc_->loadPreset(2), "ipc: mailbox preset");
    {
        int failed = 0;
        for (const IpcSnapshot& s : ipc_->sim()->trace()) failed += s.failed;
        stCheck(failed == 2, "ipc: non-blocking send on full and receive on empty fail (2)");
    }
    stCheck(ipc_->loadPreset(3) && ipc_->sim()->trace().back().wake, "ipc: blocked receiver wakes on send");
    stCheck(ipc_->loadPreset(5), "ipc: pipe preset");
    stCheck(ipc_->sim()->trace().back().event.find("END OF FILE") != std::string::npos, "ipc: pipe reaches EOF");
    ipc_->playback()->setPosition(4);
    shot(this, "ipc_pipe");
    {   // manual operation after stepping back = new branch
        ipc_->loadPreset(2);
        ipc_->playback()->setPosition(1);
        IpcOp r;
        r.kind = IpcOpKind::Receive;
        r.proc = 1;
        stCheck(ipc_->applyOp(r) == "" && ipc_->sim()->trace().back().procs[1].lastData == "first" && ipc_->playback()->length() == 2,
                "ipc: receive after Prev starts a new branch");
        IpcOp bad;
        bad.kind = IpcOpKind::Send;
        bad.proc = 0;
        stCheck(ipc_->applyOp(bad) != "", "ipc: empty message refused");
    }
    for (int p = 0; p < 8; ++p) stCheck(ipc_->loadPreset(p), QString("ipc preset %1").arg(p + 1));
    if (NativeRunner::available()) {
        for (const char* d : {"shm", "msg", "pipe", "fifo"}) {
            bool ok = ipc_->startDemo(d) && waitFor([&] { return !ipc_->demoRunning(); }, 10000) && ipc_->demoExitCode() == 0;
            stCheck(ok, QString("ipc live demo %1 exits 0").arg(d));
        }
        shot(this, "ipc_live_demo");
    }

    // ---- Phase 2, M6: Multithreading
    showTab(threads_);
    {
        const int expect[3] = {12, 4, 6};
        for (int p = 0; p < 3; ++p)
            stCheck(threads_->loadPreset(p) && threads_->result()->m.completion == expect[p],
                    QString("threads preset %1 completes in %2 ticks").arg(p + 1).arg(expect[p]));
    }
    threads_->playback()->setPosition(3);
    shot(this, "threads_many_to_many");
    threads_->playback()->setIntervalMs(60);
    threads_->playback()->run();
    stCheck(waitFor([&] { return threads_->playback()->position() == threads_->playback()->length(); }, 5000) &&
            threads_->statusText().contains("all threads completed in 6 ticks"), "threads: playback to the final state");
    stCheck(threads_->loadPreset(3) && threads_->result()->m.stalledTicks > 0, "threads: blocking call stalls Many-to-One");
    stCheck(threads_->loadPreset(4) && threads_->result()->m.stalledTicks == 0, "threads: blocking call does not stall One-to-One");
    stCheck(threads_->loadPreset(5) && threads_->result()->completionOrder == std::vector<int>({1, 2, 0}), "threads: join order A, B, main");
    stCheck(threads_->comparison().size() == 3, "threads: model comparison has 3 rows");
    if (NativeRunner::available()) {
        bool ok = threads_->startDemo(4, 10) && waitFor([&] { return !threads_->demoRunning(); }, 10000);
        stCheck(ok && threads_->demoExitCode() == 0 && threads_->demoCorrectResults() == 4, "threads live demo: 4 pthreads joined, results correct");
        shot(this, "threads_live_demo");
    }

    // ---- Phase 3 confirmed gaps: Multilevel queues, readers-writers, dining philosophers
    {
        MlqPage* mp = mlq_;
        showTab(mp);
        stCheck(mp->loadPreset(0) && mp->result() && std::fabs(mp->result()->sched.avgWaiting - 3.4) < 1e-9 && mp->result()->preemptions == 2,
                "multilevel queue: fixed queues, avg waiting 3.4, two preemptions by higher queues");
        stCheck(mp->loadPreset(1) && mp->result()->demotions == 3 && mp->result()->sched.metrics[0].completion == 19,
                "multilevel feedback queue: long job demoted twice to FCFS, finishes at 19");
        stCheck(mp->loadPreset(2) && mp->result()->boosts >= 1, "multilevel feedback queue: priority boost");
        loadSample(sd + "/processes_fcfs.csv");
        stCheck(mp->copyFromScheduling() && mp->result() && mp->result()->sched.metrics.size() == 3, "multilevel queues: copy the CPU Scheduling process list");
        showTab(mp);
        mp->loadPreset(1);
        mp->playback()->setPosition(10);
        shot(this, "mlfq");
        showTab(sync_);
        sync_->setAlgorithm(SyncAlgo::READERS_WRITERS);
        bool rwOk = true;
        for (int i = 0; i < 3; ++i)
            rwOk = rwOk && sync_->loadScenario(i) && sync_->runToEnd() > 0 && sync_->sim()->stats().completed && sync_->sim()->stats().csViolations == 0;
        stCheck(rwOk, "sync readers-writers: every scenario completes, a writer is never inside with anybody");
        sync_->loadScenario(0);
        for (int k = 0; k < 11; ++k) sync_->stepOnce();
        shot(this, "sync_readers_writers");
        sync_->setAlgorithm(SyncAlgo::DINING_PHILOSOPHERS);
        sync_->loadScenario(0);
        sync_->runToEnd();
        stCheck(sync_->sim()->stats().stuck && !sync_->sim()->stats().completed, "sync dining philosophers: naive order deadlocks (circular wait)");
        shot(this, "sync_philosophers_deadlock");
        sync_->loadScenario(1);
        sync_->runToEnd();
        stCheck(sync_->sim()->stats().completed && sync_->sim()->stats().csViolations == 0, "sync dining philosophers: asymmetric order eats without deadlock");
        sync_->setAlgorithm(SyncAlgo::MUTEX);
    }

    // ---- Phase 3: Scheduling > Real-Time Scheduling (RM / EDF)
    {
        RtSchedPage* rp = rt_;
        showTab(rp);
        stCheck(rp->loadPreset(0) && rp->result() && rp->compareRm()->misses > 0 && rp->compareRm()->firstMiss == 7 && rp->compareEdf()->misses == 0,
                "real-time: A(2,5) B(4,7) - RM misses at t=7, EDF meets every deadline over 35");
        stCheck(std::fabs(rp->result()->utilization - 34.0 / 35.0) < 1e-9, "real-time: utilization 34/35");
        rp->setAlgorithm(RtAlgo::EDF);
        stCheck(rp->result() && rp->result()->cfg.algo == RtAlgo::EDF && rp->result()->misses == 0, "real-time: switching to EDF rebuilds the trace");
        bool all = true;
        for (int i = 0; i < (int)rtPresets().size(); ++i) all = rp->loadPreset(i) && all;
        stCheck(all, "real-time: all presets (feasible, overload, fictional mission, constrained deadlines) run");
        rp->loadPreset(2);
        rp->setPolicy(MissPolicy::Abort);
        int dropped = 0;
        for (const RtTaskStats& s : rp->result()->stats) dropped += s.dropped;
        stCheck(rp->result()->misses > 0 && dropped == rp->result()->misses, "real-time: overload with the abort policy - every miss aborted, counted once");
        rp->setPolicy(MissPolicy::Continue);
        int w0 = ui::warningCount();
        const std::vector<RtTask> badTasks = {RtTask{"bad", 5, 4, 4, 0}};
        stCheck(!rp->setTasks(badTasks, 20) && !rp->result() && ui::warningCount() == w0, "real-time: invalid task set (C > D) rejected with a message");
        rp->loadPreset(0);
        rp->setAlgorithm(RtAlgo::RM);
        rp->playback()->setPosition(8);
        shot(this, "realtime_rm");
        rp->loadPreset(3);
    }

    // ---- Phase 3: Processes > Job Control (deterministic shell model + Live Linux process groups)
    {
        JobControlPage* jc = jobs_;
        showTab(jc);
        bool all = true;
        for (int i = 0; i < (int)jobScenarios().size(); ++i) all = jc->loadScenario(i) && all;
        stCheck(all, "job control: all scenarios replay");
        jc->loadScenario(0);
        jc->playback()->setPosition(jc->playback()->length());
        stCheck(jc->shell().job(1) && jc->shell().job(1)->state == JcJobState::Done && jc->shell().shellHasTerminal(),
                "job control: background job stopped, resumed, foregrounded and completed");
        jc->loadScenario(-1);
        stCheck(jc->command("compute 5 &") && jc->shell().job(1)->pgid == 101 && jc->shell().shellHasTerminal(), "job control: background job via the console (job [1], PGID 101)");
        stCheck(jc->command("kill -STOP %1") && jc->shell().job(1)->state == JcJobState::Stopped, "job control: kill -STOP stops the group");
        stCheck(!jc->command("kill -STOP 4242"), "job control: bare PID refused (no signals to unrelated processes)");
        stCheck(!jc->command("ls /home"), "job control: arbitrary commands are never executed");
        stCheck(jc->command("fg %1") && jc->shell().fgPgid() == 101, "job control: fg gives the job the terminal");
        stCheck(jc->command("ctrl-z") && jc->shell().shellHasTerminal(), "job control: Ctrl-Z returns the terminal to the shell");
        jc->playback()->setPosition(2);
        stCheck(jc->command("tick 2") && jc->shell().job(1)->state == JcJobState::Stopped, "job control: command after Previous starts a new branch");
        jc->loadScenario(4);
        jc->playback()->setPosition(6);
        shot(this, "job_control_pipeline");
        if (NativeRunner::available()) {
            bool ok = jc->startNativeDemo() && waitFor([&] { return !jc->nativeRunning(); }, 10000);
            stCheck(ok && jc->nativeExitCode() == 0 && jc->nativeEventCount() >= 10, "job control live demo: process groups, SIGSTOP/SIGCONT, pipeline, cleanup");
        }
    }

    // ---- Phase 3: Processes > System Monitor (live, sampled on a worker thread)
    {
        SystemMonitorPage* sm = sysmon_;
        sm->setIntervalMs(250);
        showTab(sm);                                   // showing the page starts monitoring
        stCheck(waitFor([&] { return sm->snapshots() >= 3; }, 8000), "system monitor: samples arrive asynchronously from the worker thread");
        const SystemSnapshot& s = sm->last();
#ifdef __linux__
        stCheck(!s.warmingUp && s.memValid && s.processCount > 0 && s.cpuValid, "system monitor (Linux /proc): CPU and memory valid after warm-up");
#endif
        sm->setFilter(QString::number(QCoreApplication::applicationPid()));
        stCheck(sm->visibleRows() == 1, "system monitor: filter by PID shows OS-Sim itself");
        sm->setFilter(QString());
        sm->setHistoryCapacity(10);
        waitFor([&] { return sm->snapshots() >= 14; }, 8000);
        stCheck(sm->historySize() <= 10 && sm->historySize() >= 3, QString("system monitor: history is bounded (%1 of 10)").arg(sm->historySize()));
        sm->pauseMonitoring();
        QApplication::processEvents();
        long n = sm->snapshots();
        waitFor([] { return false; }, 700);
        stCheck(!sm->monitoring() && sm->snapshots() <= n + 1, "system monitor: pause stops sampling");
        if (NativeRunner::available()) {
            stCheck(sm->startWorker(20, 8, 30), "system monitor: start the owned demo worker");
            qint64 wp = sm->workerPid();
            auto stateOf = [&]() -> std::string {
                for (const ProcView& p : sm->last().procs) if (p.pid == wp) return p.state;
                return "";
            };
            auto resample = [&](const std::string& want, bool equal) {
                return waitFor([&] { sm->sampleOnce(); return equal ? stateOf() == want : (!stateOf().empty() && stateOf() != want); }, 5000);
            };
            stCheck(resample("", false), "system monitor: the demo worker appears in the live process table");
            stCheck(sm->suspendWorker() && resample("stopped", true), "system monitor: SIGSTOP to the own worker -> state 'stopped'");
            stCheck(sm->resumeWorker() && resample("stopped", false), "system monitor: SIGCONT -> running again");
            stCheck(!sm->killWorker(false) && sm->workerRunning(), "system monitor: force kill without confirmation does nothing");
            stCheck(sm->stopWorker() && waitFor([&] { return !sm->workerRunning(); }, 5000), "system monitor: cooperative stop (SIGTERM) ends the worker");
            stCheck(sm->startWorker(20, 0, 5) && sm->killWorker(true) && waitFor([&] { return !sm->workerRunning(); }, 5000),
                    "system monitor: confirmed force kill of the own worker");
        }
        sm->startMonitoring();
        waitFor([&] { return sm->snapshots() >= n + 3; }, 5000);
        sm->setFilter("ossim");                        // screenshots show only OS-Sim's own processes (privacy)
        shot(this, "sysmon");
        sm->setFilter(QString());
        sm->stopMonitoring();
        stCheck(sm->historySize() == 0 && !sm->monitoring(), "system monitor: stop clears the history");
        sm->setIntervalMs(1000);
    }

    // ---- Phase 3: Memory > Heap Laboratory (bounded arena allocator)
    {
        HeapLabPage* hl = memArea_->heapLab();
        showTab(hl);
        bool all = true;
        for (int i = 0; i < (int)heapPresets().size(); ++i) all = hl->loadPreset(i) && all;
        stCheck(all, "heap lab: all presets load and replay");
        hl->loadPreset(1);
        int offs[3];
        for (int a = 0; a < 3; ++a) { hl->setFit((FitAlgo)a); offs[a] = hl->session().steps.back().result.offset; }
        stCheck(offs[0] == 16 && offs[1] == 352 && offs[2] == 720, QString("heap lab: first / best / worst fit choose @%1 / @%2 / @%3").arg(offs[0]).arg(offs[1]).arg(offs[2]));
        hl->loadPreset(2);
        const HeapStep& fs = hl->session().steps.back();
        stCheck(!fs.result.ok && fs.metrics.freeBytes == 768 && fs.metrics.largestFree == 320, "heap lab: fragmentation preset - 768 B free, request fails");
        hl->loadPreset(0);
        int before = (int)hl->session().steps.size();
        stCheck(hl->applyOp("q = malloc 40") && (int)hl->session().steps.size() == before + 1, "heap lab: manual malloc appended");
        stCheck(hl->applyOp("write q 0 OS-Sim") && hl->applyOp("read q 0 6") && hl->session().steps.back().result.data == "OS-Sim", "heap lab: write + read payload");
        stCheck(!hl->applyOp("write q 38 overflow!"), "heap lab: out-of-bounds write refused");
        hl->playback()->setPosition(3);
        stCheck(hl->applyOp("z = calloc 2 8") && (int)hl->session().steps.size() == 5, "heap lab: operation after Previous starts a new branch");
        stCheck(hl->undo() && (int)hl->session().steps.size() == 4, "heap lab: undo");
        hl->setFamily(HeapFamily::Buddy);
        stCheck(hl->session().cfg.family == HeapFamily::Buddy && hl->session().steps.back().integrity.empty(), "heap lab: family change resets and replays (buddy)");
        hl->loadPreset(6);
        hl->playback()->setPosition(2);
        shot(this, "heap_buddy");
        hl->loadPreset(7);
        stCheck(!hl->session().steps[4].integrity.empty() && !hl->session().steps.back().result.ok, "heap lab: modelled corruption detected, heap refuses further use");
        hl->loadPreset(3);
        hl->playback()->setPosition(6);
        shot(this, "heap_realloc");
        hl->loadPreset(0);
    }

    // ---- Phase 3: Storage & Files (one shared virtual filesystem behind four child views)
    {
        StorageArea* st = storage_;
        ExplorerPage* ex = st->explorer();
        showTab(ex);
        stCheck(st->newDisk(64, 64, AllocStrategy::Indexed, true), "storage: new indexed sample disk");
        ex->setCwd("/home/alice");
        stCheck(ex->cwd() == "/home/alice" && st->fs().currentUser() == 1000, "storage explorer: enter /home/alice as alice");
        stCheck(ex->apply("create", "/home/alice/selftest.txt", "hello").ok(), "storage explorer: create file");
        stCheck(ex->apply("append", "/home/alice/selftest.txt", " world").ok() && st->fs().readFile("/home/alice/selftest.txt").data == "hello world",
                "storage explorer: append and read back");
        stCheck(ex->apply("link", "/home/alice/selftest.txt", "/tmp/selftest-link").ok() &&
                st->fs().inode(st->fs().resolve("/tmp/selftest-link").value)->links == 2, "storage explorer: hard link shares the inode (link count 2)");
        stCheck(ex->apply("mkdir", "/home/alice/sub").ok() && ex->apply("rename", "/home/alice/sub", "/home/alice/sub2").ok(), "storage explorer: mkdir + rename");
        stCheck(ex->apply("read", "/tmp/loopA").err == FsErr::Loop, "storage explorer: symbolic link loop detected (ELOOP)");
        int pid = st->fs().shellPid(1000);
        FsResult o = ex->fdOp("open-rw", pid, "/home/alice/selftest.txt");
        FsResult rd = ex->fdOp("read", pid, "5", o.value);
        stCheck(o.ok() && rd.ok() && rd.data == "hello", "storage descriptors: open + read(5) = 'hello' (offset 5)");
        int ino = st->fs().resolve("/tmp/selftest-link").value;
        size_t delBefore = st->fs().deleted().size();
        ex->apply("delete", "/home/alice/selftest.txt");
        ex->apply("delete", "/tmp/selftest-link");
        stCheck(st->fs().inode(ino) && st->fs().deleted().size() == delBefore, "storage: unlinking both names of an open file keeps the inode");
        stCheck(ex->fdOp("close", pid, QString(), o.value).ok() && !st->fs().inode(ino) && st->fs().deleted().size() == delBefore + 1,
                "storage: the last close releases the inode (deleted record created)");
        shot(this, "storage_explorer");
        AccessPage* ac = st->access();
        showTab(ac);
        ac->setPath("/shared/plan.txt");
        stCheck(ac->matrixAllowed(1, 0) && !ac->matrixAllowed(2, 0) && ac->matrixAllowed(3, 1), "storage access: group file - alice reads, bob denied, carol writes");
        ac->setPath("/home/alice/todo.txt");
        stCheck(ac->applyPreset(2), "storage access: chmod 644 preset as the owner");
        stCheck(!ac->matrixAllowed(2, 0), "storage access: public-readable file still denied to bob (no search permission on /home/alice)");
        int w0 = ui::warningCount();
        stCheck(!ac->applyMode("98x") && ui::warningCount() == w0 + 1, "storage access: invalid mode -> warning");
        st->fs().setCurrentUser(1001);
        stCheck(!ac->applyMode("777"), "storage access: chmod by a non-owner is refused (EPERM)");
        st->fs().setCurrentUser(1000);
        shot(this, "storage_access");
        BlockPage* bp = st->blocks();
        showTab(bp);
        stCheck(bp->compare(0) && bp->comparison().size() == 3 && bp->comparison()[0].failures == 1 &&
                bp->comparison()[1].failures == 0 && bp->comparison()[2].failures == 0,
                "storage blocks: contiguous fails despite enough free blocks, linked and indexed succeed");
        st->fs().setCurrentUser(0);
        stCheck(bp->allocate("blocktest.bin", 300).ok() && bp->selectedFile() == st->fs().resolve("/blocktest.bin").value,
                "storage blocks: allocate on the shared disk, the file is selectable in the inspector");
        int fromExplorer = ex->apply("create", "/shared-state.txt", "x").value;
        bp->selectFile(fromExplorer);
        stCheck(bp->selectedFile() == fromExplorer, "storage: a file created in the Explorer appears in Block Allocation (one shared model)");
        stCheck(st->fs().checkInvariants().empty(), "storage: filesystem invariants hold after the GUI operations");
        shot(this, "storage_blocks");
        RecoveryPage* rp = st->recovery();
        showTab(rp);
        stCheck(rp->loadPreset(0) && rp->current().cls == RecoveryClass::Full && rp->current().verified, "storage recovery: intact deleted file fully recoverable (verified)");
        stCheck(rp->restore("/recovered/notes.txt").ok() && st->fs().resolveAs(0, "/recovered/notes.txt").ok(), "storage recovery: restore into a new file");
        stCheck(!rp->restore("/recovered/notes.txt").ok(), "storage recovery: restore never overwrites");
        stCheck(rp->loadPreset(1) && rp->current().cls == RecoveryClass::Partial, "storage recovery: partially overwritten");
        stCheck(rp->loadPreset(2) && rp->current().cls == RecoveryClass::Unrecoverable, "storage recovery: fully overwritten");
        rp->loadPreset(3);
        rp->selectCandidate(1);
        stCheck(rp->candidateCount() == 2 && rp->current().cls == RecoveryClass::Unknown, "storage recovery: carved signature without metadata = unknown integrity");
        rp->loadPreset(1);
        shot(this, "storage_recovery");
        if (NativeRunner::available()) {
            showTab(ex);
            bool ok = ex->startNativeDemo() && waitFor([&] { return !ex->nativeRunning(); }, 10000);
            stCheck(ok && ex->nativeExitCode() == 0 && ex->nativeEventCount() >= 10, "storage live demo: real file syscalls in a private temp dir");
        }
        rp->loadPreset(0);
    }

    // ---- Responsive layout regression (real geometry; see LayoutAudit.h and PanelLayout.h)
    {
        QStringList lp = layoutReport({QSize(1920, 1080), QSize(1366, 768), QSize(1024, 768), QSize(910, 480)}, false);
        stCheck(lp.isEmpty(), QString("layout: every control of all 9 modules (and Memory sub-tabs) reachable at 1920x1080, 1366x768, "
                                      "1024x768, 910x480 - %1 problem(s)%2").arg(lp.size()).arg(lp.isEmpty() ? QString() : ": " + lp.first()));
        resize(1366, 768);
        QApplication::processEvents();
        // Drags a splitter handle like a user would (press, move by dx/dy, release).
        auto drag = [](QSplitterHandle* h, int d) {
            bool horiz = h->orientation() == Qt::Horizontal;
            QPoint c = h->rect().center(), c2 = c + (horiz ? QPoint(d, 0) : QPoint(0, d));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), QPointF(h->mapToGlobal(c)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(h, &press);
            QMouseEvent move(QEvent::MouseMove, QPointF(c2), QPointF(h->mapToGlobal(c2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(h, &move);
            QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(c2), QPointF(h->mapToGlobal(c2)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(h, &rel);
            QApplication::processEvents();
            QApplication::processEvents();
        };
        auto horizontalWithConfig = [this]() -> PanelSplitter* {
            for (PanelSplitter* sp : currentSplitters())
                if (sp->orientation() == Qt::Horizontal && sp->hasPanel(PanelSplitter::Config)) return sp;
            return nullptr;
        };
        struct T { QWidget* page; int sub; const char* name; };
        std::vector<T> pages = {{sched_, -1, "Scheduling"}, {rt_, -1, "Scheduling > Real-Time"}, {mlq_, -1, "Scheduling > Multilevel Queues"}, {proc_, -1, "Processes"}, {memArea_, 0, "Memory > Page replacement"},
                                {memArea_, 1, "Memory > Allocation"}, {memArea_, 2, "Memory > Paging"}, {memArea_, 4, "Memory > Demand paging"},
                                {dead_, -1, "Deadlock"}, {sync_, -1, "Synchronization"}, {threads_, -1, "Threads"}, {ipc_, -1, "IPC"},
                                {io_, -1, "I/O"}, {disk_, -1, "Disk"},
                                {memArea_->heapLab(), -1, "Memory > Heap Laboratory"}, {sysmon_, -1, "Processes > System Monitor"},
                                {jobs_, -1, "Processes > Job Control"},
                                {storage_->explorer(), -1, "Storage > File Explorer"}, {storage_->access(), -1, "Storage > Access Control"},
                                {storage_->blocks(), -1, "Storage > Block Allocation"}, {storage_->recovery(), -1, "Storage > Recovery Lab"}};
        for (const T& t : pages) {
            showTab(t.page);
            if (t.sub >= 0) memArea_->setCurrentIndex(t.sub);
            QApplication::processEvents();
            PanelSplitter* sp = horizontalWithConfig();
            if (!sp) { stCheck(false, QString("layout %1: has a configuration panel splitter").arg(t.name)); continue; }
            auto* cfg = qobject_cast<QScrollArea*>(sp->panel(PanelSplitter::Config));
            QWidget* center = sp->panel(PanelSplitter::Center);
            int cfgIdx = sp->indexOf(cfg);
            int minViewport = cfg->fontMetrics().averageCharWidth() * 18;
            stCheck(cfg->viewport()->width() >= minViewport, QString("layout %1: configuration viewport %2 px usable").arg(t.name).arg(cfg->viewport()->width()));
            int w0 = cfg->width(), c0 = center->width();
            drag(sp->handle(cfgIdx + 1), 320);                                  // widen the configuration panel
            int w1 = cfg->width(), c1 = center->width();
            stCheck(w1 >= w0 + 200 && c1 < c0, QString("layout %1: dragging widens the configuration panel (%2 -> %3 px) and the view shrinks (%4 -> %5)")
                                                   .arg(t.name).arg(w0).arg(w1).arg(c0).arg(c1));
            stCheck(cfg->horizontalScrollBar()->maximum() == 0 || cfg->width() >= cfg->widget()->minimumSizeHint().width(),
                    QString("layout %1: widened panel shows its content without horizontal scrolling").arg(t.name));
            drag(sp->handle(cfgIdx + 1), -420);                                 // and narrower again
            int w2 = cfg->width();
            stCheck(w2 < w1 - 150, QString("layout %1: dragging back narrows it (%2 -> %3 px)").arg(t.name).arg(w1).arg(w2));
            LayoutAuditResult narrow = auditControls(tabs_->currentWidget(), t.name);
            stCheck(narrow.problems.isEmpty(), QString("layout %1: narrow configuration panel - every control still reachable (scroll bars)%2")
                                                   .arg(t.name).arg(narrow.problems.isEmpty() ? QString() : ": " + narrow.problems.first()));
            if (QWidget* ins = sp->panel(PanelSplitter::Inspector)) {
                int insIdx = sp->indexOf(ins), i0 = ins->width();
                drag(sp->handle(insIdx), -200);                                 // widen the inspector
                int i1 = ins->width();
                drag(sp->handle(insIdx), 260);                                  // narrow it
                int i2 = ins->width();
                stCheck(i1 > i0 + 100 && i2 < i1 - 100, QString("layout %1: inspector resizes both ways (%2 -> %3 -> %4 px)").arg(t.name).arg(i0).arg(i1).arg(i2));
            }
            resetPanelLayout();
            QApplication::processEvents();
            QList<int> now = sp->sizes(), def = sp->defaultSizes();
            bool same = now.size() == def.size();
            for (int i = 0; same && i < now.size(); ++i) same = std::abs(now[i] - def[i]) <= 4;
            stCheck(same, QString("layout %1: Reset Panel Layout restores the default sizes").arg(t.name));
        }
        shot(this, "layout_after_drag_reset");
        // View menu: show / hide panels and focus mode
        showTab(proc_);
        QApplication::processEvents();
        PanelSplitter* sp = horizontalWithConfig();
        int cw = sp ? sp->panel(PanelSplitter::Center)->width() : 0;
        setConfigPanelsVisible(false);
        setInspectorPanelsVisible(false);
        QApplication::processEvents();
        stCheck(sp && !sp->panelVisible(PanelSplitter::Config) && !sp->panelVisible(PanelSplitter::Inspector) &&
                sp->panel(PanelSplitter::Center)->width() > cw, "layout: View > hide configuration and inspector panels gives the view more space");
        setConfigPanelsVisible(true);
        setInspectorPanelsVisible(true);
        QApplication::processEvents();
        stCheck(sp && sp->panelVisible(PanelSplitter::Config) && sp->panelVisible(PanelSplitter::Inspector), "layout: View > show panels again");
        setFocusView(true);
        QApplication::processEvents();
        shot(this, "layout_focus_view");
        stCheck(sp && !sp->panelVisible(PanelSplitter::Config) && !logDock_->isVisible() && focusView(), "layout: Focus Simulation View hides panels and the log");
        setFocusView(false);
        QApplication::processEvents();
        stCheck(sp && sp->panelVisible(PanelSplitter::Config) && sp->panelVisible(PanelSplitter::Inspector) && logDock_->isVisible(),
                "layout: leaving focus view restores the layout");
        // Presets / algorithms / modes change the forms: layouts must stay accessible
        bool presetsOk = true;
        QString firstBad;
        auto auditNow = [&](const QString& what) {
            QApplication::processEvents();
            LayoutAuditResult a = auditControls(tabs_->currentWidget(), what);
            if (!a.problems.isEmpty()) { presetsOk = false; if (firstBad.isEmpty()) firstBad = a.problems.first(); }
        };
        for (int i = 0; i < 4; ++i) { proc_->loadPreset(i); auditNow(QString("Processes preset %1").arg(i)); }
        proc_->setManual(true); auditNow("Processes manual"); proc_->setManual(false);
        showTab(ipc_);
        for (int i = 0; i < 8; ++i) { ipc_->loadPreset(i); auditNow(QString("IPC preset %1").arg(i)); }
        showTab(io_);
        for (int i = 0; i < 6; ++i) { io_->loadPreset(i); auditNow(QString("I/O preset %1").arg(i)); }
        showTab(disk_);
        for (int i = 0; i < 7; ++i) { disk_->loadPreset(i); auditNow(QString("Disk preset %1").arg(i)); }
        showTab(threads_);
        for (int i = 0; i < 6; ++i) { threads_->loadPreset(i); auditNow(QString("Threads preset %1").arg(i)); }
        showTab(sync_);
        for (int a = 0; a <= (int)SyncAlgo::DINING_PHILOSOPHERS; ++a) { sync_->setAlgorithm((SyncAlgo)a); auditNow(QString("Sync algorithm %1").arg(a)); }
        stCheck(presetsOk, "layout: switching presets / algorithms / modes keeps every control reachable" + (firstBad.isEmpty() ? QString() : ": " + firstBad));
        // Dark theme and a window resized after start
        setDarkTheme(true);
        QStringList dk = layoutReport({QSize(1366, 768), QSize(1100, 650)}, false);
        stCheck(dk.isEmpty(), QString("layout: dark theme, resized windows - %1 problem(s)").arg(dk.size()));
        setDarkTheme(false);
        // Saved panel sizes: restored when valid, ignored when they do not fit (own settings name)
        PanelSplitter::setPersistence(true, "OS-Sim-selftest");
        auto mkSplit = [] {
            auto* p = new PanelSplitter(Qt::Horizontal, "selftest/persist");
            for (auto r : {PanelSplitter::Config, PanelSplitter::Center, PanelSplitter::Inspector}) {
                auto* w = new QWidget;
                w->setMinimumSize(60, 40);
                p->addPanel(w, r);
            }
            p->resize(900, 300);
            p->show();
            QApplication::processEvents();
            return p;
        };
        PanelSplitter* a = mkSplit();
        a->setSizes({250, 450, 186});
        QList<int> saved = a->sizes();
        a->saveNow();
        delete a;
        PanelSplitter* b = mkSplit();
        stCheck(b->sizes() == saved, "layout: saved panel sizes are restored");
        delete b;
        QSettings("OS-Sim-selftest", "OS-Sim").setValue("layout/selftest/persist", QVariantList{10, 20, 20});
        PanelSplitter* c = mkSplit();
        stCheck(c->sizes() != QList<int>({10, 20, 20}) && c->sizes()[1] >= 60, "layout: saved sizes that do not fit the window are rejected");
        delete c;
        QSettings("OS-Sim-selftest", "OS-Sim").clear();
        PanelSplitter::setPersistence(false);
        resize(1280, 800);
    }

    // Export: both files, every section
    sched_->runOnce();
    sched_->compareAll();
    QString base = QDir::temp().filePath(QString("ossim_selftest_%1").arg(QCoreApplication::applicationPid()));
    QString err;
    // Theme switch: dark and back to light must work and keep the window alive
    setDarkTheme(true);
    stCheck(ui::darkTheme() && darkAction_->isChecked(), "switch to dark theme");
    // Phase 2 tabs in the dark theme and at the minimum window size (screenshots for inspection)
    io_->loadPreset(0);
    io_->playback()->setPosition(4);
    showTab(io_);
    shot(this, "dark_io");
    showTab(proc_);
    proc_->loadPreset(0);
    proc_->playback()->setPosition(8);
    shot(this, "dark_process");
    showTab(memArea_);
    memArea_->setCurrentIndex(2);
    shot(this, "dark_paging");
    setDarkTheme(false);
    resize(1100, 700);
    for (QWidget* t : std::initializer_list<QWidget*>{disk_, proc_, io_, ipc_, threads_}) {
        showTab(t);
        shot(this, QString("small_%1").arg(tabs_->tabText(tabs_->currentIndex()).remove('/')));
    }
    memArea_->setCurrentIndex(1);
    showTab(memArea_);
    shot(this, "small_memory_allocation");
    resize(1280, 800);
    stCheck(!ui::darkTheme() && !darkAction_->isChecked(), "switch back to light theme");

    stCheck(writeReport(base, &err), "export report " + err);
    QFile tf(base + ".txt"), cf(base + ".csv");
    bool opened = tf.open(QIODevice::ReadOnly) && cf.open(QIODevice::ReadOnly);
    QString t = opened ? QString::fromUtf8(tf.readAll()) : QString(), c = opened ? QString::fromUtf8(cf.readAll()) : QString();
    bool allTxt = true, allCsv = true;
    for (const char* sec : {"=== Processes", "=== Scheduling result", "=== Algorithm comparison", "=== Integrated run",
                            "=== Memory", "=== Deadlock", "=== Synchronization", "=== Disk scheduling", "=== Process lifecycle", "=== I/O management", "=== Advanced memory", "=== IPC", "=== Threading models", "=== Storage and files", "=== Event log"}) allTxt = allTxt && t.contains(sec);
    for (const char* sec : {"## Processes", "## Scheduling result", "## Algorithm comparison", "## Integrated run",
                            "## Memory summary", "## Memory frame table", "## Deadlock state", "## Deadlock result",
                            "## Synchronization", "## Synchronization comparison", "## Synchronization POSIX demo", "## Disk scheduling", "## Disk trace", "## Process lifecycle", "## I/O management", "## Memory allocation", "## Address translation", "## Demand paging", "## IPC", "## Threading models", "## Storage and files",
                            "## Event log"}) allCsv = allCsv && c.contains(sec);
    stCheck(opened && allTxt, "report .txt has all sections");
    stCheck(opened && allCsv, "report .csv has all sections");
    tf.close(); cf.close();
    QFile::remove(base + ".txt");
    QFile::remove(base + ".csv");

    // Leave work running on purpose: closing the window must stop the animations and the live
    // demo helper cleanly (checked from outside: no ossim_helper process may remain).
    if (NativeRunner::available()) proc_->startDemo("sleep");
    disk_->playback()->run();
    sync_->run();
    std::printf("[selftest] %s (%d failure(s))\n", g_stFail ? "FAILED" : "ALL PASSED", g_stFail);
    return g_stFail ? 1 : 0;
}
