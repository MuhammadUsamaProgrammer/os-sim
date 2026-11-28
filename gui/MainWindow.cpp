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
#include "ModuleManager.h"
#include "WorkspaceUi.h"
#include "SampleRegistry.h"
#include <QWidgetAction>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QLineEdit>
#include <QKeyEvent>

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

    registerModules();
    registerSamples();                   // central sample catalogue (gui/SampleCatalog.cpp)
    installSampleButtons();              // one Load Sample button per module page
    buildMenus();
    statusBar()->showMessage("Ready. Use the Load Sample button of any module (or File > Load Built-in Sample), or enter your own input.");
    loadSample(ui::samplesDir() + "/processes_fcfs.csv");
    modules_->restoreCurrentFromStore();          // the module selected when OS-Sim was closed
    for (QTabWidget* a : std::initializer_list<QTabWidget*>{tabs_, schedArea_, procArea_, memArea_, storage_})
        connect(a, &QTabWidget::currentChanged, modules_, [this] { modules_->saveCurrentToStore(); });
}

QString& MainWindow::workspaceStore() { static QString org = "OS-Sim"; return org; }

// Module Visibility Manager registry: every parent tab and every child view of the sub-tab
// areas, with stable ids, in navigation order. Display names are the tab texts.
void MainWindow::registerModules() {
    modules_ = new ModuleManager(this);
    auto text = [](QTabWidget* a, int i) { return a->tabText(i).replace("&&", "&"); };
    const std::vector<std::pair<QWidget*, QString>> tops = {
        {schedArea_, "scheduling"}, {procArea_, "processes"}, {memArea_, "memory"}, {dead_, "deadlock"},
        {sync_, "synchronization"}, {threads_, "threads"}, {ipc_, "ipc"}, {io_, "io"}, {disk_, "disk"}, {storage_, "storage"}};
    for (int i = 0; i < tabs_->count(); ++i)
        for (const auto& [w, id] : tops)
            if (pageOf(i) == w) modules_->add(id, text(tabs_, i), "", tabs_, tabs_->widget(i));
    // Child views. Ids follow the tab order; an area with an unexpected number of tabs still gets
    // every tab registered (generic ids), so nothing can become unreachable.
    auto children = [&](const QString& parent, QTabWidget* area, const QStringList& ids) {
        for (int k = 0; k < area->count(); ++k)
            modules_->add(parent + "." + (k < ids.size() && area->count() == ids.size() ? ids[k] : QString("view%1").arg(k + 1)),
                          text(area, k), parent, area, area->widget(k));
    };
    children("scheduling", schedArea_, {"cpu", "mlq", "realtime"});
    children("processes", procArea_, {"lifecycle", "monitor", "jobs"});
    children("memory", memArea_, {"replacement", "allocation", "paging", "segmentation", "demand", "swapping", "thrashing", "heap"});
    children("storage", storage_, {"explorer", "access", "blocks", "recovery"});
    // Pages whose playback is not a PlaybackBar
    modules_->setPauseHook("scheduling.cpu", [this] { sched_->pauseAnimation(); });
    modules_->setPauseHook("synchronization", [this] { sync_->pause(); });
    connect(modules_, &ModuleManager::changed, this, [this] { syncViewActions(); });
    modules_->setStore(workspaceStore());
    modules_->loadFromStore();
    for (const QString& w : modules_->loadWarnings()) appendLog("[workspace] " + w);
}

// Manage Workspace dialog (non-modal, created once).
void MainWindow::openWorkspaceDialog() {
    if (!wsDialog_) wsDialog_ = new WorkspaceDialog(modules_, this);
    wsDialog_->show();
    wsDialog_->raise();
    wsDialog_->activateWindow();
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
bool MainWindow::showTab(QWidget* page) {
    // A destination hidden in the Modules menu is not forced open: the action still runs (e.g. a
    // sample is loaded), the workspace stays as the user chose and the status bar says so.
    lastShowBlocked_ = modules_ && !modules_->reachable(page);
    if (lastShowBlocked_) {
        statusBar()->showMessage(QString("\"%1\" is hidden in this workspace - Modules > Show All Modules (or tick it) to see it")
                                     .arg(modules_->name(modules_->idOfWidget(page))), 8000);
        return false;
    }
    for (QWidget* w = page; w; w = w->parentWidget()) {
        QWidget* stack = w->parentWidget();
        if (auto* tw = stack ? qobject_cast<QTabWidget*>(stack->parentWidget()) : nullptr)
            if (tw->indexOf(w) >= 0) tw->setCurrentWidget(w);
        if (w == tabs_) break;
    }
    return true;
}

// The module widget of tab i (unwrapping the PageScroll container).
QWidget* MainWindow::pageOf(int i) const {
    auto* ps = qobject_cast<PageScroll*>(tabs_->widget(i));
    return ps ? ps->widget() : tabs_->widget(i);
}

// Selects the Memory area and its "Page replacement" sub-tab (the original Memory tab).
void MainWindow::showMemoryTab() {
    showTab(mem_);                       // selects Memory and its sub-tab (unless hidden in the workspace)
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    // Load Built-in Sample: the same catalogue as the in-module Load Sample buttons, by module.
    QMenu* builtin = file->addMenu("Load Built-in Sample");
    builtin->setToolTipsVisible(true);
    connect(builtin, &QMenu::aboutToShow, this, [this, builtin] { fillBuiltinSampleMenu(builtin); });
    fillBuiltinSampleMenu(builtin);
    // Load sample: every file in samples/ (original menu, unchanged behaviour)
    QMenu* samples = file->addMenu("Load sample");
    samples->setToolTipsVisible(true);
    QDir dir(ui::samplesDir());
    for (const QString& f : dir.entryList({"*.csv", "*.txt"}, QDir::Files, QDir::Name)) {
        QString path = dir.absoluteFilePath(f);
        QAction* a = samples->addAction(f, this, [this, path, f] {
            if (const SampleDef* d = samples_->forFile(f)) revealModule(d->module, true);   // hidden target: ask first
            loadSample(path);
        });
        if (const SampleDef* d = samples_->forFile(f)) a->setToolTip(d->description);
    }
    QAction* open = file->addAction("Open File...", QKeySequence::Open, this, &MainWindow::openFileDialog);
    open->setToolTip("Import your own file (processes CSV, reference string, matrices, lifecycle CSV, disk requests, virtual disk image)");
    file->setToolTipsVisible(true);
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

    // Modules: workspace customisation (Module Visibility Manager). The tree is embedded through
    // a QWidgetAction, so the menu stays open while several checkboxes are changed.
    QMenu* mods = menuBar()->addMenu("&Modules");
    modulesMenu_ = mods;
    mods->setToolTipsVisible(true);
    auto* treeAction = new QWidgetAction(mods);
    menuTree_ = new ModuleTree(modules_, true);
    treeAction->setDefaultWidget(menuTree_);
    mods->addAction(treeAction);
    mods->addSeparator();
    QAction* all = mods->addAction("Show All Modules", modules_, &ModuleManager::showAll);
    all->setToolTip("Show every main module and every child view");
    QAction* only = mods->addAction("Show Current Module Only", modules_, [this] { modules_->showCurrentOnly(); });
    only->setToolTip("Keep the selected main module, hide the others (child choices and simulations are kept)");
    QAction* def = mods->addAction("Reset to Default", modules_, &ModuleManager::resetToDefaults);
    def->setToolTip("Restore the default module visibility");
    QMenu* presets = mods->addMenu("Workspace Presets");
    presets->setToolTipsVisible(true);
    connect(presets, &QMenu::aboutToShow, this, [this, presets] {
        presets->clear();
        for (const QString& n : modules_->builtinPresetNames())
            presets->addAction(n, modules_, [this, n] { modules_->applyPreset(n); })->setToolTip(modules_->builtinPresetDescription(n));
        QStringList custom = modules_->customPresetNames();
        if (!custom.isEmpty()) presets->addSeparator();
        for (const QString& n : custom) presets->addAction(n, modules_, [this, n] { modules_->applyPreset(n); })->setToolTip("Custom preset");
        presets->addSeparator();
        presets->addAction("Save, Rename or Delete Presets...", this, &MainWindow::openWorkspaceDialog);
    });
    mods->addSeparator();
    QAction* manage = mods->addAction("Manage Workspace...", QKeySequence("Ctrl+Shift+M"), this, &MainWindow::openWorkspaceDialog);
    manage->setToolTip("Searchable module tree, counters, bulk actions and workspace presets (Ctrl+Shift+M)");
    manage->setShortcutContext(Qt::ApplicationShortcut);
    connect(mods, &QMenu::aboutToShow, this, [this] { menuTree_->search()->clear(); menuTree_->refresh(); });
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
    if (ok && !lastShowBlocked_) statusBar()->showMessage("Loaded sample " + name, 5000);
    if (ok && lastShowBlocked_) statusBar()->showMessage("Loaded sample " + name + " into a module that is hidden in this workspace (Modules menu)", 8000);
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
            if (!tabs_->isTabVisible(i)) continue;                        // hidden in the Modules menu
            tabs_->setCurrentIndex(i);
            if (auto* area = qobject_cast<QTabWidget*>(pageOf(i))) {      // sub-tab area: audit every child view
                for (int k = 0; k < area->count(); ++k) {
                    if (!area->isTabVisible(k)) continue;
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
        stCheck(sched_->compareAll() && sched_->lastComparison().size() == allAlgos().size(), QString("compare all on ") + f);
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

    // Built-in samples: Load Sample buttons, registry, File menu, visibility, themes, small windows
    g_stFail += runSampleSelfTest();

    // Modules menu (Module Visibility Manager)
    runWorkspaceSelfTest();

    // Leave work running on purpose: closing the window must stop the animations and the live
    // demo helper cleanly (checked from outside: no ossim_helper process may remain).
    if (NativeRunner::available()) proc_->startDemo("sleep");
    disk_->playback()->run();
    sync_->run();
    std::printf("[selftest] %s (%d failure(s))\n", g_stFail ? "FAILED" : "ALL PASSED", g_stFail);
    return g_stFail ? 1 : 0;
}

// ---------------------------------------------------------------- Modules menu self-test
// Uses the real tab widgets: QTabWidget::isTabVisible, currentWidget, indexOf; simulation state is
// checked on the real pages (playback position, playing / paused flags, results).
int MainWindow::runWorkspaceSelfTest() {
    int before = g_stFail;
    ModuleManager* M = modules_;
    const QStringList ids = M->ids();
    // registry: every parent tab and every child view of the sub-tab areas is registered
    int navigable = tabs_->count();
    for (QTabWidget* a : std::initializer_list<QTabWidget*>{schedArea_, procArea_, memArea_, storage_}) navigable += a->count();
    stCheck(ids.size() == navigable && ids.size() == 28, QString("modules: %1 registered entries = %2 navigable tabs").arg(ids.size()).arg(navigable));
    stCheck(M->workspace().topLevel().size() == (size_t)tabs_->count(), "modules: one entry per main tab");
    for (const char* id : {"scheduling.realtime", "processes.monitor", "memory.heap", "storage.recovery", "io", "disk"})
        stCheck(M->page(id) != nullptr, QString("modules: stable id %1").arg(id));
    stCheck(M->page("memory.heap") == memArea_->heapLab() && M->page("processes.monitor") == sysmon_ && M->page("scheduling.cpu") == sched_,
            "modules: ids are bound to the real pages");
    // the Modules menu comes right after Help and holds the tree, the quick actions and Manage Workspace
    QStringList menus;
    for (QAction* a : menuBar()->actions()) menus << a->text().remove('&');
    stCheck(menus.join("|") == "File|View|Help|Modules", "modules: menu bar is File | View | Help | Modules (" + menus.join("|") + ")");
    QStringList modActs;
    std::function<void(QMenu*)> collect = [&](QMenu* m) { for (QAction* a : m->actions()) { if (!a->text().isEmpty()) modActs << a->text(); } };
    collect(modulesMenu_);
    for (const char* a : {"Show All Modules", "Show Current Module Only", "Reset to Default", "Workspace Presets", "Manage Workspace..."})
        stCheck(modActs.contains(a), QString("modules: menu has \"%1\"").arg(a));
    stCheck(menuTree_ && menuTree_->tree()->topLevelItemCount() == tabs_->count(), "modules: menu tree lists every main module");
    int treeItems = 0;
    for (QTreeWidgetItemIterator it(menuTree_->tree()); *it; ++it) ++treeItems;
    stCheck(treeItems == ids.size(), "modules: menu tree lists every entry");
    bool manageKey = false;
    for (QAction* a : modulesMenu_->actions()) if (a->shortcut() == QKeySequence("Ctrl+Shift+M")) manageKey = true;
    stCheck(manageKey, "modules: Manage Workspace has Ctrl+Shift+M");

    // consistency: tab visibility == preference; the selected tab of every visible area is visible
    auto consistent = [&]() {
        for (const QString& id : ids) {
            QTabWidget* a = M->area(id);
            int i = a->indexOf(M->page(id));
            if (i < 0 || a->isTabVisible(i) != M->isVisible(id)) return false;
        }
        for (QTabWidget* a : std::initializer_list<QTabWidget*>{tabs_, schedArea_, procArea_, memArea_, storage_}) {
            QString id = M->idOfWidget(a);
            if (a != tabs_ && !M->isEffective(id)) continue;
            if (!a->isTabVisible(a->currentIndex())) return false;
        }
        return true;
    };
    // order: (area, index, text) per entry never changes
    auto layoutOf = [&]() {
        QStringList v;
        for (const QString& id : ids) { QTabWidget* a = M->area(id); int i = a->indexOf(M->page(id)); v << QString("%1:%2:%3").arg(id).arg(i).arg(a->tabText(i)); }
        return v;
    };
    const QStringList order0 = layoutOf();
    QList<QWidget*> pages0;
    for (const QString& id : ids) pages0 << M->page(id);
    stCheck(consistent(), "modules: default workspace is consistent");

    // 1 + 2: every parent and every child can be hidden and restored
    bool allOk = true;
    for (const QString& id : ids) {
        QTabWidget* a = M->area(id);
        int i = a->indexOf(M->page(id));
        bool hid = M->setVisible(id, false) && !a->isTabVisible(i) && consistent();
        bool back = M->setVisible(id, true) && a->isTabVisible(i) && consistent();
        if (!hid || !back) { allOk = false; stCheck(false, "modules: hide / restore " + id); }
    }
    stCheck(allOk, QString("modules: all %1 entries hidden and restored one by one").arg(ids.size()));
    // 3: original order
    stCheck(layoutOf() == order0, "modules: navigation order unchanged after hide / restore");

    // 4: child preferences survive the parent's visibility
    M->setVisible("memory.heap", false);
    M->setVisible("memory.swapping", false);
    M->setVisible("memory", false);
    stCheck(!tabs_->isTabVisible(tabs_->indexOf(M->page("memory"))) && !M->isEffective("memory.paging"), "modules: Memory hidden");
    stCheck(M->setVisible("memory.thrashing", false), "modules: child changed while its parent is hidden");
    M->setVisible("memory", true);
    stCheck(!M->isVisible("memory.heap") && !M->isVisible("memory.swapping") && !M->isVisible("memory.thrashing") && M->isVisible("memory.paging")
            && !memArea_->isTabVisible(memArea_->indexOf(memArea_->heapLab())) && memArea_->isTabVisible(memArea_->indexOf(mem_)) && consistent(),
            "modules: restoring Memory keeps 5 of 8 children (hidden ones stay hidden)");
    QTreeWidgetItem* memItem = menuTree_->itemFor("memory");
    stCheck(memItem && memItem->text(1) == "5/8" && memItem->font(0).italic(), "modules: partially enabled parent shows 5/8 (italic)");
    M->setVisible("memory", false);
    QTreeWidgetItem* heapItem = menuTree_->itemFor("memory.heap");
    stCheck(heapItem && heapItem->isDisabled() && heapItem->checkState(0) == Qt::Unchecked, "modules: children of a hidden parent are disabled, choice kept");
    M->showAll();
    stCheck(M->workspace().effectiveCount() == ids.size() && consistent(), "modules: Show All Modules restores everything");

    // 5 + 6: simulation state survives; playing simulations are paused, never reset or resumed
    disk_->loadPreset(0);
    PlaybackBar* db = disk_->playback();
    db->setPosition(3);
    db->setIntervalMs(60);
    db->run();
    QApplication::processEvents();
    stCheck(db->playing(), "modules: disk playback running");
    M->setVisible("disk", false);
    int dpos = db->position();
    stCheck(!db->playing() && dpos >= 3, QString("modules: hiding Disk pauses its playback at step %1").arg(dpos));
    waitFor([] { return false; }, 200);
    M->setVisible("disk", true);
    stCheck(!db->playing() && db->position() == dpos && db->length() > 0, "modules: shown again - same step, still paused (manual resume)");
    db->togglePause();
    stCheck(db->playing(), "modules: the user can resume after showing it");
    db->stop();
    // parent hidden: children are paused too (heap laboratory, real-time scheduling)
    HeapLabPage* hl = memArea_->heapLab();
    hl->loadPreset(0);
    hl->playback()->setPosition(2);
    hl->playback()->run();
    rt_->loadPreset(0);
    rt_->playback()->run();
    sync_->run();
    stCheck(hl->playback()->playing() && rt_->playback()->playing() && sync_->animating(), "modules: heap, real-time and sync playing");
    M->setVisible("memory", false);
    M->setVisible("scheduling", false);
    M->setVisible("synchronization", false);
    int hpos = hl->playback()->position(), rpos = rt_->playback()->position();
    stCheck(!hl->playback()->playing() && !rt_->playback()->playing(), "modules: hiding a parent pauses its children's playback");
    stCheck(!sync_->animating() && sync_->paused(), "modules: hiding Synchronization pauses its animation");
    waitFor([] { return false; }, 150);
    M->showAll();
    stCheck(!hl->playback()->playing() && hl->playback()->position() == hpos && rt_->playback()->position() == rpos && !sync_->animating() && sync_->paused(),
            "modules: restored pages keep their paused state and position");
    stCheck(rt_->result().has_value(), "modules: real-time result kept");
    hl->playback()->stop();
    rt_->playback()->stop();
    sync_->stop();
    // the threaded engine replay (Scheduling > CPU Scheduling)
    bool engineDone = false;
    auto ec = connect(sched_, &SchedulingTab::animationFinished, this, [&] { engineDone = true; });
    showTab(sched_);
    stCheck(sched_->runAnimated(200), "modules: engine replay started");
    M->setVisible("scheduling", false);
    stCheck(sched_->animationRunning() && sched_->animationPaused(), "modules: hiding Scheduling pauses the engine replay (threads wait)");
    M->setVisible("scheduling", true);
    stCheck(sched_->animationPaused() && !engineDone, "modules: replay still paused after showing it");
    sched_->stopAnimation();
    waitFor([&] { return !sched_->animationRunning(); }, 3000);
    disconnect(ec);
    stCheck(!sched_->animationRunning(), "modules: engine stopped cleanly (no orphan threads)");
    // System Monitor stops sampling while its page is hidden
    showTab(sysmon_);
    QApplication::processEvents();
    M->setVisible("processes.monitor", false);
    QApplication::processEvents();
    auto* smTimer = sysmon_->findChild<QTimer*>();
    bool smIdle = true;
    for (QTimer* t : sysmon_->findChildren<QTimer*>()) if (t->isActive() && t->interval() >= 250) smIdle = false;
    stCheck(smTimer && smIdle, "modules: System Monitor sampling stops while hidden");
    M->setVisible("processes.monitor", true);

    // 7: the selection moves to the nearest visible module
    showTab(memArea_->heapLab());
    M->setVisible("memory.heap", false);
    stCheck(memArea_->currentWidget() == M->page("memory.thrashing") && consistent(), "modules: hidden current sub-tab -> nearest visible sibling (Thrashing)");
    showTab(M->page("memory.paging"));
    M->setVisible("memory", false);
    stCheck(tabs_->currentWidget() == M->page("deadlock") && consistent(), "modules: hidden current main tab -> next visible (Deadlock)");
    M->setVisible("storage", false);
    showTab(disk_);
    M->setVisible("disk", false);
    stCheck(tabs_->currentWidget() == M->page("io"), "modules: last tab hidden -> previous visible (I/O)");
    M->showAll();
    stCheck(memArea_->currentWidget() == M->page("memory.paging"), "modules: restoring Memory keeps its last visible sub-tab");
    // keyboard navigation (Ctrl+Tab) skips hidden tabs
    M->setVisible("processes", false);
    tabs_->setCurrentIndex(0);
    QKeyEvent ct(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier);
    QApplication::sendEvent(tabs_, &ct);
    stCheck(tabs_->currentWidget() == M->page("memory"), "modules: Ctrl+Tab skips the hidden Processes tab");
    M->setVisible("scheduling.mlq", false);
    schedArea_->setCurrentWidget(sched_);
    QKeyEvent ct2(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier);
    QApplication::sendEvent(schedArea_, &ct2);
    stCheck(schedArea_->currentWidget() == rt_, "modules: Ctrl+Tab in a sub-tab area skips hidden children");
    // menu actions with a hidden destination: the sample loads, the workspace is not changed
    M->setVisible("deadlock", false);
    showTab(io_);
    bool loaded = loadSample(ui::samplesDir() + "/banker_sample.txt");
    stCheck(loaded && tabs_->currentWidget() == M->page("io") && !M->isVisible("deadlock") && consistent(),
            "modules: File > Load sample into a hidden module keeps the workspace (status bar explains)");
    M->showAll();
    showTab(dead_);
    stCheck(tabs_->currentWidget() == M->page("deadlock"), "modules: navigation works again once shown");

    // 8: the last main module and the last child cannot be hidden
    showTab(io_);
    stCheck(M->showCurrentOnly() && M->workspace().effectiveCount() == 1 && M->isVisible("io"), "modules: Show Current Module Only keeps I/O");
    QString why;
    stCheck(!M->setVisible("io", false, &why) && M->isVisible("io") && why.contains("At least one"), "modules: the last main module cannot be hidden");
    stCheck(!menuTree_->clickCheck("io") && menuTree_->lastMessage().contains("At least one") && menuTree_->itemFor("io")->checkState(0) == Qt::Checked,
            "modules: menu checkbox refuses it and explains");
    M->showAll();
    for (const char* c : {"storage.explorer", "storage.access", "storage.blocks"}) M->setVisible(c, false);
    stCheck(!M->setVisible("storage.recovery", false, &why) && why.contains("last visible"), "modules: the last visible child cannot be hidden");
    // menu tree clicks work like the model
    stCheck(menuTree_->clickCheck("storage.access") && M->isVisible("storage.access") && storage_->isTabVisible(1), "modules: menu checkbox shows a child");
    stCheck(menuTree_->clickCheck("threads") && !M->isVisible("threads") && !tabs_->isTabVisible(tabs_->indexOf(M->page("threads"))), "modules: menu checkbox hides a main module");
    // 9 + 10: show all, reset to default
    M->showAll();
    stCheck(consistent() && M->workspace().effectiveCount() == ids.size(), "modules: Show All Modules");
    M->setVisible("ipc", false);
    M->resetToDefaults();
    stCheck(consistent() && M->workspace().effectiveCount() == ids.size(), "modules: Reset to Default (all modules are visible by default)");

    // 11: presets (visibility only) - also while a simulation is playing
    db->setPosition(1);
    db->run();
    stCheck(M->applyPreset("CPU & Process Management"), "modules: apply CPU & Process Management");
    stCheck(M->isEffective("scheduling.realtime") && M->isEffective("processes.jobs") && M->isEffective("threads") && !M->isEffective("memory") && !M->isEffective("disk") && consistent(),
            "modules: preset shows Scheduling, Processes, Threads only");
    stCheck(!db->playing() && db->position() >= 1, "modules: preset during playback pauses the hidden Disk simulation");
    M->applyPreset("Memory & Storage");
    stCheck(M->isEffective("memory.heap") && M->isEffective("storage.recovery") && M->isEffective("disk") && !M->isEffective("scheduling") && consistent(), "modules: Memory & Storage preset");
    M->applyPreset("Synchronization & Deadlocks");
    stCheck(M->isEffective("synchronization") && M->isEffective("deadlock") && M->isEffective("ipc") && M->isEffective("threads") && !M->isEffective("memory") && consistent(), "modules: Synchronization & Deadlocks preset");
    M->applyPreset("Linux Practical Lab");
    stCheck(M->isEffective("processes.monitor") && M->isEffective("processes.jobs") && M->isEffective("ipc") && M->isEffective("storage.explorer") && !M->isVisible("storage.recovery") && !M->isEffective("disk") && consistent(),
            "modules: Linux Practical Lab preset");
    db->stop();
    stCheck(disk_->playback()->length() > 0, "modules: presets did not reset simulation data");
    // custom presets: save, rename, load, delete
    M->showAll();
    M->setVisible("memory.heap", false);
    M->setVisible("ipc", false);
    stCheck(M->saveCustomPreset("Lecture 1"), "modules: save custom preset");
    stCheck(!M->saveCustomPreset("Complete OS-Sim", &why) && !M->saveCustomPreset("   ", &why), "modules: built-in / empty preset names refused");
    stCheck(M->renameCustomPreset("Lecture 1", "Lecture A") && M->customPresetNames() == QStringList{"Lecture A"}, "modules: rename custom preset");
    M->showAll();
    stCheck(M->applyPreset("Lecture A") && !M->isVisible("memory.heap") && !M->isVisible("ipc") && M->isVisible("disk") && consistent(), "modules: load custom preset");
    stCheck(M->deleteCustomPreset("Lecture A") && M->customPresetNames().isEmpty(), "modules: delete custom preset");

    // 12 + 13: persistence (a separate settings store; the user's settings are not touched)
    {
        QSettings st("OS-Sim-selftest", "OS-Sim");
        st.clear();
        st.setValue("layout/other", 7);
        M->showAll();
        M->setVisible("processes", false);
        M->setVisible("processes.monitor", false);
        M->setVisible("scheduling.realtime", false);
        M->saveCustomPreset("Viva");
        M->save(st);
        VisibilityMap saved = M->workspace().snapshot();
        M->showAll();
        M->deleteCustomPreset("Viva");
        M->load(st);
        stCheck(M->workspace().snapshot() == saved && M->customPresetNames() == QStringList{"Viva"} && consistent() && M->loadWarnings().isEmpty(),
                "modules: saved workspace and custom preset restored");
        stCheck(st.value("layout/other").toInt() == 7, "modules: other settings preserved");
        // invalid / outdated settings fall back safely
        st.setValue("modules/v1/visible/memory", "banana");
        st.setValue("modules/v1/visible/gone.module", false);
        st.setValue("modules/v1/visible/scheduling", false);
        st.setValue("modules/v1/visible/processes", false);
        for (const char* t : {"memory", "deadlock", "synchronization", "threads", "ipc", "io", "disk", "storage"})
            if (QString(t) != "memory") st.setValue(QString("modules/v1/visible/%1").arg(t), false);
        M->load(st);
        stCheck(M->isVisible("memory") && M->workspace().effectiveCount() >= 2 && consistent() && !M->loadWarnings().isEmpty(), "modules: corrupted settings -> safe defaults");
        st.setValue("modules/version", 42);
        M->load(st);
        stCheck(M->workspace().effectiveCount() == ids.size() && M->loadWarnings().join(" ").contains("not supported"), "modules: unsupported settings version -> defaults");
        st.clear();
        M->load(st);
        stCheck(M->workspace().effectiveCount() == ids.size() && M->loadWarnings().isEmpty(), "modules: empty settings -> defaults");
        stCheck(M->store().isEmpty(), "modules: the self-test never writes the user's settings");
    }

    // 14: repeated toggling never duplicates widgets or connections
    mlq_->loadPreset(0);
    int logBefore = log_->blockCount();
    mlq_->loadPreset(1);
    int perLoad = log_->blockCount() - logBefore;
    for (int k = 0; k < 40; ++k) {
        M->setVisible(k % 2 ? "scheduling" : "scheduling.mlq", false);
        M->setVisible(k % 2 ? "scheduling" : "scheduling.mlq", true);
        M->applyPreset(k % 3 ? "Memory & Storage" : "Complete OS-Sim");
    }
    M->showAll();
    logBefore = log_->blockCount();
    mlq_->loadPreset(2);
    stCheck(log_->blockCount() - logBefore == perLoad, QString("modules: one log line set per action after 40 toggles (%1)").arg(perLoad));
    int countNow = tabs_->count();
    for (QTabWidget* a : std::initializer_list<QTabWidget*>{schedArea_, procArea_, memArea_, storage_}) countNow += a->count();
    QList<QWidget*> pagesNow;
    for (const QString& id : ids) pagesNow << M->page(id);
    stCheck(countNow == navigable && pagesNow == pages0 && layoutOf() == order0, "modules: no tab or page duplicated, same widgets, same order");
    int items = 0;
    for (QTreeWidgetItemIterator it(menuTree_->tree()); *it; ++it) ++items;
    stCheck(items == ids.size(), "modules: menu tree not duplicated");

    // Manage Workspace dialog: searchable tree, counters, presets
    openWorkspaceDialog();
    QApplication::processEvents();
    stCheck(wsDialog_ && wsDialog_->isVisible() && !wsDialog_->isModal(), "modules: Manage Workspace dialog opens (non-modal)");
    ModuleTree* dt = wsDialog_->moduleTree();
    dt->setFilter("heap");
    int shownItems = 0;
    for (QTreeWidgetItemIterator it(dt->tree()); *it; ++it) if (!(*it)->isHidden()) ++shownItems;
    stCheck(shownItems == 2 && !dt->itemFor("memory.heap")->isHidden(), "modules: search 'heap' lists Memory > Heap Laboratory");
    dt->setFilter("");
    stCheck(dt->clickCheck("memory.heap") && !M->isVisible("memory.heap") && menuTree_->itemFor("memory.heap")->checkState(0) == Qt::Unchecked,
            "modules: dialog change applies immediately (menu tree follows)");
    stCheck(wsDialog_->presetList()->count() == 5 + (int)M->customPresetNames().size(), "modules: dialog lists the built-in and custom presets");
    M->setVisible("ipc", false);
    M->setVisible("storage.blocks", false);
    menuTree_->expandAll();
    dt->expandAll();
    stCheck(menuTree_->tree()->topLevelItem(2)->isExpanded(), "modules: Expand All");
    shot(wsDialog_, "workspace_dialog");
    // screenshots: menu open, customised workspace
    showTab(M->page("memory.paging"));
    modulesMenu_->popup(mapToGlobal(QPoint(menuBar()->actionGeometry(modulesMenu_->menuAction()).left(), menuBar()->height())));
    QApplication::processEvents();
    stCheck(modulesMenu_->isVisible() && modulesMenu_->sizeHint().height() <= 760, QString("modules: menu fits a 768-pixel screen (%1 px)").arg(modulesMenu_->sizeHint().height()));
    shot(modulesMenu_, "modules_menu");
    modulesMenu_->hide();
    menuTree_->collapseAll();
    stCheck(!menuTree_->tree()->topLevelItem(2)->isExpanded(), "modules: Collapse All");
    wsDialog_->close();
    shot(this, "workspace_customised");

    // 15 + 16: themes and the responsive layout with hidden modules
    setDarkTheme(true);
    M->setVisible("ipc", true);
    showTab(ipc_);
    stCheck(ui::darkTheme() && consistent(), "modules: dark theme with hidden modules");
    shot(this, "workspace_dark");
    openWorkspaceDialog();
    shot(wsDialog_, "workspace_dialog_dark");
    wsDialog_->close();
    setDarkTheme(false);
    QStringList lp = layoutReport({QSize(1366, 768), QSize(1024, 768)}, false);
    stCheck(lp.isEmpty(), QString("modules: layout report with hidden modules - %1 problem(s)").arg(lp.size()));
    M->resetToDefaults();
    lp = layoutReport({QSize(1366, 768)}, false);
    stCheck(lp.isEmpty() && consistent(), QString("modules: layout report after Reset to Default - %1 problem(s)").arg(lp.size()));
    resize(1280, 800);
    std::printf("[selftest] modules section: %d failure(s)\n", g_stFail - before);
    return g_stFail - before;
}
