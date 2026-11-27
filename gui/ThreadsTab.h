// ThreadsTab.h - M6 Multithreading and threading models (simulation) + Live Linux pthread demo.
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include <optional>
#include <vector>
#include "threads.h"

class QComboBox; class QSpinBox; class QTableWidget; class QPlainTextEdit; class QTabWidget; class QScrollArea; class QLabel;
class QPushButton; class PlaybackBar; class ModuleHeader; class NativeRunner; class BarChartWidget;

// User-thread lane, kernel-context lane and CPU-core lane with animated mapping links.
class ThreadMapView : public QWidget {
    Q_OBJECT
public:
    explicit ThreadMapView(QWidget* parent = nullptr);
    void setData(const ThreadResult* r, const std::vector<TSnap>* threads, const ThreadTick* tick, int animMs);
    QSize sizeHint() const override { return QSize(620, 380); }
    QSize minimumSizeHint() const override { return QSize(280, 180); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const ThreadResult* r_ = nullptr;
    const std::vector<TSnap>* th_ = nullptr;
    const ThreadTick* tick_ = nullptr;
    double t_ = 1;
    QVariantAnimation anim_;
};

// Per-thread state rows and per-core rows over logical ticks (one run or several aligned).
class ThreadTimeline : public QWidget {
    Q_OBJECT
public:
    explicit ThreadTimeline(QWidget* parent = nullptr);
    void setResults(const std::vector<const ThreadResult*>& rs, const QStringList& labels, int upto);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<const ThreadResult*> rs_;
    QStringList labels_;
    int upto_ = 0;
};

class ThreadsTab : public QWidget {
    Q_OBJECT
public:
    explicit ThreadsTab(QWidget* parent = nullptr);
    bool loadPreset(int index);
    bool rebuild();
    PlaybackBar* playback() const { return bar_; }
    const std::optional<ThreadResult>& result() const { return result_; }
    const std::vector<ThreadResult>& comparison() const { return compare_; }
    bool startDemo(int threads, int base);
    void stopDemo();
    bool demoRunning() const;
    int demoExitCode() const;
    int demoCorrectResults() const;              // "joined ... (correct)" lines of the last demo
    QString statusText() const;
    QString reportText() const;
    QString csvSection() const;
signals:
    void logMessage(const QString& line);
private:
    bool readConfig(ThreadConfig& c, QString& err) const;
    void fillThreads(const std::vector<UThreadSpec>& v);
    void showStep(int pos, bool forward);

    ModuleHeader* header_;
    QComboBox *model_, *preset_;
    QSpinBox *contexts_, *cores_, *quantum_, *dThreads_, *dBase_;
    QTableWidget *threads_, *inspector_, *cmpTable_, *demoTable_;
    ThreadMapView* map_;
    PlaybackBar* bar_;
    QTabWidget* bottom_;
    ThreadTimeline *timeline_, *cmpLanes_, *parLanes_;
    QScrollArea* timelineScroll_;
    QPlainTextEdit* log_;
    BarChartWidget* cmpChart_;
    QLabel *parLabel_, *demoStatus_;
    QPushButton *demoStart_, *demoStop_;
    NativeRunner* native_;
    std::optional<ThreadResult> result_;
    std::vector<ThreadResult> compare_, par_;
    std::vector<TSnap> initial_;                 // state before tick 0 (shown at step 0)
    bool loading_ = false;
};
