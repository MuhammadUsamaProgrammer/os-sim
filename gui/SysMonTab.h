// SysMonTab.h - Processes > System Monitor (Phase 3): LIVE system data of the machine running
// OS-Sim (Linux /proc backend; Windows API backend compiled only on Windows), clearly separated from
// the simulated workloads of the other pages.
//   - sampling runs on a worker QThread (SysMonSampler); results reach the GUI as immutable
//     SystemSnapshot values through queued signals - the GUI thread never reads /proc;
//   - summary strip, bounded history charts, per-core bars, sortable / filterable process table,
//     process tree, selected-process inspector, informational alerts with debounce;
//   - the host process table is READ-ONLY. The only processes this page can act on are the demo
//     workers it started itself (QProcess-owned ossim_helper "worker"): suspend / resume
//     (SIGSTOP / SIGCONT, POSIX only), cooperative stop (SIGTERM) and a confirmed force kill.
#pragma once
#include <QMetaType>
#include <QObject>
#include <QWidget>
#include "sysmon.h"

Q_DECLARE_METATYPE(SystemSnapshot)

class QThread; class QTimer; class QSpinBox; class QLineEdit; class QPushButton; class QLabel; class QTableWidget;
class QTreeWidget; class QCheckBox; class QProcess; class QPlainTextEdit; class ModuleHeader;

// Lives on the sampling thread: reads the platform backend and computes snapshots.
class SysMonSampler : public QObject {
    Q_OBJECT
public slots:
    void sample();
    void resetModel();
signals:
    void sampled(const SystemSnapshot& s);
private:
    SysMonModel model_;
};

// Line chart of bounded histories (0..100 %) with threshold lines.
class HistoryChart : public QWidget {
    Q_OBJECT
public:
    explicit HistoryChart(QWidget* parent = nullptr);
    void setSeries(const MetricHistory* cpu, const MetricHistory* mem, double cpuThr, double memThr, double intervalSec);
    void setCores(const std::vector<double>& cores) { cores_ = cores; update(); }
    QSize sizeHint() const override { return QSize(520, 200); }
    QSize minimumSizeHint() const override { return QSize(220, 120); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const MetricHistory *cpu_ = nullptr, *mem_ = nullptr;
    double cpuThr_ = 90, memThr_ = 90, interval_ = 1;
    std::vector<double> cores_;
};

class SystemMonitorPage : public QWidget {
    Q_OBJECT
public:
    explicit SystemMonitorPage(QWidget* parent = nullptr);
    ~SystemMonitorPage() override;
    // ---- monitoring (public for the self-test)
    void startMonitoring();
    void pauseMonitoring();
    void stopMonitoring();                    // stop + reset history and warm-up
    bool monitoring() const;
    void setIntervalMs(int ms);
    void sampleOnce();                        // asynchronous: one sample on the worker thread
    long snapshots() const { return received_; }
    const SystemSnapshot& last() const { return last_; }
    int historySize() const { return (int)cpuHist_.values().size(); }
    void setHistoryCapacity(int n);
    int alertCount() const { return alerts_; }
    void setFilter(const QString& text);
    int visibleRows() const;
    // ---- owned demo worker
    bool startWorker(int seconds, int mib, int busyPercent);
    bool suspendWorker();
    bool resumeWorker();
    bool stopWorker();                        // cooperative (SIGTERM)
    bool killWorker(bool confirmed);          // forceful; needs confirmation
    bool workerRunning() const;
    qint64 workerPid() const;
    static bool canSuspend();
signals:
    void logMessage(const QString& line);
protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    void onSnapshot(const SystemSnapshot& s);
    void fillTable();
    void fillTree();
    void showInspector();
    void addAlert(const AlertEvent& e);
    void updateWorkerButtons();
    ModuleHeader* header_;
    QThread* thread_;
    SysMonSampler* sampler_;
    QTimer* timer_;
    QSpinBox *interval_, *history_, *cpuThr_, *memThr_, *debounce_, *wSeconds_, *wMib_, *wBusy_;
    QLineEdit* filter_;
    QCheckBox* onlyMine_;
    QPushButton *start_, *pause_, *stop_, *wStart_, *wSuspend_, *wResume_, *wStop_, *wKill_;
    QLabel *cpuLbl_, *memLbl_, *availLbl_, *procLbl_, *diskLbl_, *srcLbl_, *inspect_;
    HistoryChart* chart_;
    QTableWidget *table_, *alertTable_;
    QTreeWidget* tree_;
    QPlainTextEdit* workerLog_;
    QProcess* worker_ = nullptr;
    qint64 workerPid_ = 0;
    MetricHistory cpuHist_{120}, memHist_{120};
    AlertMonitor cpuAlert_{"CPU", 90, 3}, memAlert_{"Memory", 90, 3};
    SystemSnapshot last_;
    long received_ = 0;
    int alerts_ = 0, selectedPid_ = -1;
    bool inFlight_ = false, userPaused_ = false, wasRunningBeforeHide_ = false;
};
