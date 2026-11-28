// SchedulingTab.h - process table, scheduler controls, Gantt chart, results,
// animated replay through the threaded engine, and algorithm comparison.
#pragma once
#include <QWidget>
#include <optional>
#include <vector>
#include "scheduler.h"
#include "integrated.h"

class QTableWidget; class QComboBox; class QSpinBox; class QPushButton; class QSlider;
class QLabel; class QTabWidget; class QLineEdit; class QCheckBox; class GanttWidget; class BarChartWidget; class EngineBridge;

class SchedulingTab : public QWidget {
    Q_OBJECT
public:
    explicit SchedulingTab(QWidget* parent = nullptr);

    // Reads and validates the process table. Shows a warning and returns false on bad input.
    bool readProcesses(std::vector<PCB>& out, bool quiet = false);
    void setProcesses(const std::vector<PCB>& procs);
    bool loadCsv(const QString& path);       // also used by File -> Load sample
    bool saveCsv(const QString& path);
    void setAlgorithm(Algo a);
    void setQuantum(int q);

    // Actions (public so the menu and the self-test can call them)
    bool runOnce();
    bool runAnimated(int delayMs = -1);      // -1 = use the speed slider
    bool compareAll();
    void stopAnimation();
    bool animationRunning() const;
    // Pauses a running animated replay (no-op otherwise); resume with the Resume button.
    void pauseAnimation();
    bool animationPaused() const { return paused_; }
    // Integrated run (scheduling + paging + resources); avoidance = Banker's on/off.
    bool runIntegrated();
    void setIntegratedOptions(const QString& requestsFile, bool avoidance, bool autoRecover, int frames, int k);

    const std::optional<SchedResult>& lastResult() const { return last_; }
    const std::vector<SchedResult>& lastComparison() const { return compare_; }
    const std::vector<PCB>& lastProcesses() const { return lastProcs_; }
    const std::optional<IntegratedResult>& lastIntegrated() const { return integ_; }
    EngineBridge* bridge() const { return bridge_; }

signals:
    void logMessage(const QString& line);    // goes to the shared log dock
    void processesChanged();                 // table edited / loaded
    void animationFinished();
    // Emitted when an integrated run hit a deadlock: the state for the Deadlock tab.
    void integratedDeadlock(const std::vector<int>& rowPids, const DeadlockSnapshot& snap);

private slots:
    void addRow();
    void removeRow();
    void clearTable();
    void onLoadCsv();
    void onSaveCsv();
    void onAlgoChanged();
    void onEngineEvent(int tick, int pid, int state, const QString& text);
    void onEngineFinished();

private:
    Algo currentAlgo() const;
    void showResult(const SchedResult& r);
    void setAnimControls(bool running);
    void refreshStateTable();

    QTableWidget* procTable_;
    QComboBox* algoCombo_;
    QSpinBox* quantumSpin_;
    QPushButton *runBtn_, *animBtn_, *compareBtn_, *pauseBtn_, *stepBtn_, *stopBtn_;
    QSlider* speedSlider_;
    QLabel* speedLabel_;
    GanttWidget* gantt_;
    QTabWidget* outTabs_;
    QTableWidget* resultTable_;
    QLabel* summaryLabel_;
    QTableWidget* stateTable_;
    QTableWidget* compareTable_;
    BarChartWidget* chart_;
    EngineBridge* bridge_;
    // integrated-run controls
    QLineEdit* reqFileEdit_;
    QLineEdit* totalEdit_;
    QSpinBox* intFramesSpin_;
    QComboBox* intPageCombo_;
    QCheckBox* avoidCheck_;
    QCheckBox* recoverCheck_;
    QSpinBox* detectSpin_;
    QPushButton* intRunBtn_;

    std::optional<SchedResult> last_;
    std::vector<SchedResult> compare_;
    std::vector<PCB> lastProcs_;
    std::optional<IntegratedResult> integ_;
    bool paused_ = false;
};
