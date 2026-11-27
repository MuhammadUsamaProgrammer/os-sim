// RtSchedTab.h - Scheduling > Real-Time Scheduling (Phase 3): RM and EDF on one CPU (core/rtsched.h).
//   RtGanttView  CPU lane + one lane per task, release arrows (up), deadline markers (down), misses
//                (red X), drawn up to the playback time (cursor), job labels.
//   RtSchedPage  task editor, presets (incl. a FICTIONAL mission-control workload), algorithm and miss
//                policy, playback, active-job inspector (running job + ready queue in priority order),
//                utilization analysis with stated assumptions, per-task statistics, event log, RM-vs-EDF
//                comparison on identical input and horizon.
#pragma once
#include <QWidget>
#include <optional>
#include "rtsched.h"

class QComboBox; class QSpinBox; class QTableWidget; class QLabel; class QListWidget; class PlaybackBar; class ModuleHeader;

class RtGanttView : public QWidget {
    Q_OBJECT
public:
    explicit RtGanttView(QWidget* parent = nullptr);
    void setResult(const RtResult* r);
    void setTime(int t) { time_ = t; update(); }       // slots [0, t) are drawn
    void setTitle(const QString& t) { title_ = t; update(); }
    QSize sizeHint() const override { return QSize(640, 300); }
    QSize minimumSizeHint() const override { return QSize(240, 140); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const RtResult* r_ = nullptr;
    int time_ = 0;
    QString title_;
};

class RtSchedPage : public QWidget {
    Q_OBJECT
public:
    explicit RtSchedPage(QWidget* parent = nullptr);
    bool loadPreset(int i);
    bool setTasks(const std::vector<RtTask>& tasks, int horizon);
    void setAlgorithm(RtAlgo a);
    void setPolicy(MissPolicy p);
    bool rebuild();
    const std::optional<RtResult>& result() const { return result_; }
    const std::optional<RtResult>& compareRm() const { return cmpRm_; }
    const std::optional<RtResult>& compareEdf() const { return cmpEdf_; }
    PlaybackBar* playback() const { return bar_; }
    QString reportText() const;
signals:
    void logMessage(const QString& line);
private:
    bool readConfig(RtConfig& c, QString& err) const;
    void fillTasks(const std::vector<RtTask>& t);
    void onStep(int pos, bool forward);
    void fillComparison();
    ModuleHeader* header_;
    QComboBox *preset_, *algo_, *policy_;
    QSpinBox* horizon_;
    QTableWidget *tasks_, *stats_, *events_, *cmp_;
    QLabel *presetText_, *analysis_, *now_;
    QListWidget* ready_;
    RtGanttView *gantt_, *cmpRmView_, *cmpEdfView_;
    PlaybackBar* bar_;
    std::optional<RtResult> result_, cmpRm_, cmpEdf_;
    bool loading_ = false;
};
