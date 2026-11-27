// MlqTab.h - Scheduling > Multilevel Queues (Phase 3 gap fill): MLQ and MLFQ (core/mlq.h) with
// the existing GanttWidget, a queue view (who waits in which level at the playback tick), events
// (arrivals, demotions, preemptions, boosts) and the usual metrics.
#pragma once
#include <QWidget>
#include <functional>
#include <optional>
#include "mlq.h"

class QComboBox; class QSpinBox; class QCheckBox; class QTableWidget; class QLabel; class QListWidget;
class GanttWidget; class PlaybackBar; class ModuleHeader;

class QueueLevelView : public QWidget {
    Q_OBJECT
public:
    explicit QueueLevelView(QWidget* parent = nullptr);
    void setTick(const MlqResult* r, int tick);
    QSize sizeHint() const override { return QSize(520, 170); }
    QSize minimumSizeHint() const override { return QSize(220, 110); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const MlqResult* r_ = nullptr;
    int tick_ = 0;
};

class MlqPage : public QWidget {
    Q_OBJECT
public:
    explicit MlqPage(QWidget* parent = nullptr);
    bool loadPreset(int i);
    void setFeedback(bool on);
    bool rebuild();
    const std::optional<MlqResult>& result() const { return result_; }
    PlaybackBar* playback() const { return bar_; }
    // Optional source of the CPU Scheduling tab's processes ("Copy from CPU Scheduling").
    void setProcessProvider(std::function<std::vector<PCB>()> f) { provider_ = std::move(f); }
    bool copyFromScheduling();
    QString reportText() const;
signals:
    void logMessage(const QString& line);
private:
    bool readConfig(MlqConfig& c, std::vector<PCB>& p, QString& err) const;
    void fillQueues(const std::vector<MlqQueue>& q);
    void fillProcs(const std::vector<PCB>& p);
    void onStep(int pos, bool forward);
    ModuleHeader* header_;
    QComboBox *preset_, *mode_;
    QSpinBox *nQueues_, *boost_;
    QCheckBox* preempt_;
    QTableWidget *queues_, *procs_, *metrics_;
    QLabel *presetText_, *now_, *summary_;
    QListWidget* events_;
    GanttWidget* gantt_;
    QueueLevelView* levels_;
    PlaybackBar* bar_;
    std::optional<MlqResult> result_;
    std::function<std::vector<PCB>()> provider_;
    bool loading_ = false;
};
