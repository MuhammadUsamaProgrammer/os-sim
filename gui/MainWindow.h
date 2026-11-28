// MainWindow.h - main window: tabs, shared event-log dock, menus, status bar, self-test.
#pragma once
#include <QMainWindow>

class QTabWidget; class QPlainTextEdit; class QAction; class QToolButton; class QDockWidget; class PanelSplitter;
class ModuleManager; class ModuleTree; class WorkspaceDialog; class QMenu; class SampleRegistry; struct SampleDef;
class SchedulingTab; class MemoryTab; class DeadlockTab; class SyncTab; class DiskTab; class ProcessTab; class IoTab; class MemAdvArea; class IpcTab; class ThreadsTab; class StorageArea; class SystemMonitorPage; class JobControlPage; class RtSchedPage; class MlqPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    // Loads a sample file into the right tab (chosen by file name). Returns false on error.
    bool loadSample(const QString& path);
    // --selftest: exercises every tab and action without a display. Returns the exit code.
    int runSelfTest();
    // Layout regression check: for every window size, every tab (and Memory sub-tab) is shown and
    // all visible controls are audited (see LayoutAudit.h). Prints a report; returns the problems.
    // OSSIM_SHOTS=<dir> also saves a screenshot per tab and size.
    QStringList layoutReport(const QList<QSize>& sizes, bool print = true);
    // ---- View menu: panel layout of the CURRENT tab (and Memory sub-tab)
    QList<PanelSplitter*> currentSplitters() const;   // visible panel splitters of the shown page
    void resetPanelLayout();
    void setConfigPanelsVisible(bool on);
    void setInspectorPanelsVisible(bool on);
    void setFocusView(bool on);
    bool focusView() const { return !focused_.isEmpty(); }
    // Writes <base>.txt and <base>.csv with every section. Returns false and sets *err on failure.
    bool writeReport(const QString& base, QString* err);
    // ---- Modules menu: the Module Visibility Manager (see ModuleManager.h)
    ModuleManager* modules() const { return modules_; }
    void openWorkspaceDialog();
    QMenu* modulesMenu() const { return modulesMenu_; }
    ModuleTree* modulesMenuTree() const { return menuTree_; }
    WorkspaceDialog* workspaceDialog() const { return wsDialog_; }
    // QSettings organisation for the workspace (empty = defaults, nothing saved); set before construction.
    static QString& workspaceStore();
    // ---- built-in samples (gui/SampleCatalog.cpp, see SampleRegistry.h)
    SampleRegistry* samples() const { return samples_; }
    QStringList sampleModules() const;               // leaf modules (each has a Load Sample button)
    QString sampleReport() const;                    // module -> category -> sample inventory
    int loadAllSamples(bool print);                  // returns the number of failures
    // File > Load Built-in Sample: load through the registry and navigate (asks to reveal a hidden module)
    bool loadSampleFromMenu(const QString& id);
    // File > Open File...: imports an external file (kind = importer name, "" = by file name / ask)
    bool importFile(const QString& path, const QString& kind = QString());

public slots:
    void appendLog(const QString& line);

private slots:
    void showAbout();
    void exportReport();
    // Switches between the light and dark theme (and remembers the choice).
    void setDarkTheme(bool dark);

private:
    void buildMenus();
    QTabWidget* tabs_;
    SchedulingTab* sched_;
    MemoryTab* mem_;
    DeadlockTab* dead_;
    SyncTab* sync_;
    DiskTab* disk_;
    ProcessTab* proc_;
    IoTab* io_;
    MemAdvArea* memArea_;
    IpcTab* ipc_;
    ThreadsTab* threads_;
    StorageArea* storage_;
    QTabWidget* procArea_;          // Processes: Process Lifecycle, System Monitor, Job Control
    SystemMonitorPage* sysmon_;
    JobControlPage* jobs_;
    QTabWidget* schedArea_;         // Scheduling: CPU Scheduling, Multilevel Queues, Real-Time Scheduling
    RtSchedPage* rt_;
    MlqPage* mlq_;
    void showMemoryTab();
    bool showTab(QWidget* page);         // false when the page is hidden by the Modules menu
    bool lastShowBlocked_ = false;
    void registerModules();
    ModuleManager* modules_ = nullptr;
    QMenu* modulesMenu_ = nullptr;
    ModuleTree* menuTree_ = nullptr;
    WorkspaceDialog* wsDialog_ = nullptr;
    int runWorkspaceSelfTest();          // Modules menu section of --selftest (returns failures)
    void registerSamples();
    void installSampleButtons();
    bool sampleGuard(const SampleDef& d);
    bool revealModule(const QString& id, bool ask);
    void fillBuiltinSampleMenu(QMenu* menu);
    void openFileDialog();
    int runSampleSelfTest();             // "samples" section of --selftest (returns failures)
    void sampleScreenshots();            // OSSIM_SHOTS: Load Sample / LJF / LRTF screenshots
    SampleRegistry* samples_ = nullptr;
    QWidget* pageOf(int i) const;
    QPlainTextEdit* log_;
    QAction* darkAction_ = nullptr;     // View > Dark theme (checkable)
    QAction *configAction_ = nullptr, *inspectorAction_ = nullptr, *focusAction_ = nullptr;
    QDockWidget* logDock_ = nullptr;
    QList<PanelSplitter*> focused_;     // splitters put into focus mode (restored on exit)
    bool logWasVisible_ = true;
    void syncViewActions();
    QToolButton* themeButton_ = nullptr; // switch button in the menu-bar corner
};
