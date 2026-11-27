// SyncTab.h - "Synchronization" tab: race condition, Peterson's solution, mutex,
// binary / counting semaphores, producer-consumer and monitors.
//
// The step-by-step simulation is core/sync.h (deterministic); this class only
// drives it (QTimer for the animation, buttons for manual steps) and displays it.
// The "Real POSIX demo" page runs engine/sync_demo.* on a background thread
// through SyncBridge (queued signals only).
#pragma once
#include <QMap>
#include <QWidget>
#include <memory>
#include <optional>
#include <vector>
#include "sync.h"
#include "sync_demo.h"

class QComboBox; class QSpinBox; class QPushButton; class QSlider; class QLabel; class QTableWidget;
class QPlainTextEdit; class QTextBrowser; class QTabWidget; class QFormLayout; class QHBoxLayout;
class QFrame; class QToolButton; class QScrollArea; class QTimer;
class SyncView; class CodeView; class SyncTimeline; class BarChartWidget; class SyncBridge;

class SyncTab : public QWidget {
    Q_OBJECT
public:
    explicit SyncTab(QWidget* parent = nullptr);
    ~SyncTab() override;

    // ---- actions (public for the menu and the self-test)
    void setAlgorithm(SyncAlgo a);
    SyncAlgo algorithm() const;
    bool loadScenario(int index);            // index into syncScenarios(algorithm())
    bool applyConfig(const SyncConfig& c);   // validates, fills the controls, resets (warning on bad input)
    bool resetSim(bool quiet = false);       // new simulation from the controls
    void run();                              // animate with the timer
    void pause();
    void resume();
    bool stepOnce();                         // one instruction chosen by the scheduler
    bool stepProcess(int pid);               // one instruction of a chosen process (manual interleaving)
    void stop();
    int runToEnd();                          // remaining steps at once (no animation)
    void setSpeed(int msPerStep);
    bool compare();                          // comparison of the four mechanisms
    bool runRealDemo();                      // starts the POSIX demo (background thread)
    void stopRealDemo();

    const SyncSim* sim() const { return sim_.get(); }
    bool animating() const;
    bool paused() const { return paused_; }
    bool realDemoRunning() const;
    const std::vector<SyncComparisonRow>& lastComparison() const { return compare_; }
    const std::optional<SyncDemoResult>& lastDemo() const { return demo_; }
    QString statusText() const;

    // ---- report
    QString reportText() const;
    QString csvSection() const;

signals:
    void logMessage(const QString& line);    // goes to the shared log dock
    void demoFinished();

private slots:
    void onTimer();
    void onAlgoChanged();
    void onScenarioChanged();
    void onParamChanged();
    void onDemoLine(const QString& line);
    void onDemoFinished();

private:
    struct Card { QFrame* frame; QLabel* title; QLabel* state; QLabel* info; QToolButton* step; };
    SyncConfig configFromControls() const;
    void setControls(const SyncConfig& c);
    void updateParamVisibility();
    void refresh(int animMs);                // every view from the simulation
    void rebuildCards();
    void updateCards();
    void updateVariables();
    void updateStats();
    void updateButtons();
    void appendNewLog();
    void setDemoDefaults(int kind);
    void updateDemoVisibility();
    void prepareAlgo(SyncAlgo a);           // scenario list, visible parameters, explanation

    std::unique_ptr<SyncSim> sim_;
    std::vector<SyncScenario> scenarios_;
    QTimer* timer_;
    bool paused_ = false, stopped_ = false, loading_ = false;
    std::vector<int> script_;               // forced order of the loaded scenario (empty = custom)
    size_t logShown_ = 0;                    // lines of sim_->log() already shown
    QMap<QString, QString> lastVars_;        // to highlight changed variables

    // controls
    QComboBox *algoCombo_, *scenarioCombo_, *orderCombo_;
    QLabel* scenarioDesc_;
    QFormLayout* paramForm_;
    QSpinBox *procSpin_, *iterSpin_, *resSpin_, *prodSpin_, *consSpin_, *capSpin_, *prodSpeed_, *consSpeed_,
             *sliceSpin_, *seedSpin_, *readersSpin_, *writersSpin_, *philSpin_;
    QComboBox* philOrder_;                         // dining philosophers: naive / asymmetric
    QPushButton *runBtn_, *pauseBtn_, *resumeBtn_, *stepBtn_, *stopBtn_, *resetBtn_, *endBtn_;
    QSlider* speed_;
    QLabel* speedLabel_;
    QTextBrowser* explain_;

    // views
    QLabel *status_, *result_;
    QHBoxLayout* cardRow_;
    std::vector<Card> cards_;
    SyncView* view_;
    CodeView* code_;
    QTableWidget* vars_;
    QTabWidget* bottom_;
    SyncTimeline* timeline_;
    QScrollArea* timelineScroll_;
    QPlainTextEdit* events_;
    QTableWidget* stats_;
    QLabel* guarantee_;

    // comparison
    QSpinBox *cmpIter_, *cmpSlice_, *cmpSeed_;
    QComboBox* cmpOrder_;
    QTableWidget* cmpTable_;
    BarChartWidget* cmpChart_;
    std::vector<SyncComparisonRow> compare_;

    // real POSIX demo
    SyncBridge* bridge_;
    QComboBox* demoKind_;
    QFormLayout* demoForm_;
    QSpinBox *dThreads_, *dIter_, *dRes_, *dProd_, *dCons_, *dCap_, *dItems_, *dDelay_;
    QPushButton *demoRun_, *demoStop_;
    QLabel* demoResult_;
    QPlainTextEdit* demoLog_;
    std::optional<SyncDemoResult> demo_;
};
