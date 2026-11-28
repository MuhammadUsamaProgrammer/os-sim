// ProcessTab.h - M2 Process Management: 5-state / 2-state diagram, CPU and queues, process
// tree, PCB inspector, state timeline, manual transitions and the Live Linux lifecycle demos.
#pragma once
#include <QMap>
#include <QWidget>
#include <memory>
#include "procmgmt.h"

class QComboBox; class QSpinBox; class QTableWidget; class QTreeWidget; class QLabel; class QPlainTextEdit;
class QTabWidget; class QCheckBox; class QPushButton; class QScrollArea; class QGroupBox;
class PlaybackBar; class ModuleHeader; class LifecycleView; class LifecycleTimeline; class NativeRunner;

class ProcessTab : public QWidget {
    Q_OBJECT
public:
    explicit ProcessTab(QWidget* parent = nullptr);
    // ---- actions (public for the menu and the self-test)
    bool loadPreset(int index);
    bool loadFile(const QString& path);                 // samples/lifecycle_*.csv
    // Selects a Live Linux demo in the demo list WITHOUT starting it (the user presses Start).
    bool selectLiveDemo(const QString& key);
    static QString liveDemoText(const QString& key);    // one-line explanation of a live demo
    void setManual(bool manual);
    void setPolicy(Algo a, int quantum);
    bool rebuild();
    // Manual command on the selected process; returns "" or the reason it was refused.
    QString command(const QString& name, int pid, int arg = 2);
    bool startDemo(const QString& demo);                // fork, exec, wait, sleep, zombie, orphan, signal
    void stopDemo();
    bool demoRunning() const;
    int demoExitCode() const;
    PlaybackBar* playback() const { return bar_; }
    const LifecycleSim* sim() const { return sim_.get(); }
    const LcSnapshot* shown() const;
    QString statusText() const;
    QString reportText() const;
    QString csvSection() const;
signals:
    void logMessage(const QString& line);
    void demoFinished();
private slots:
    void onStep(int pos, bool forward);
private:
    bool readWorkload(std::vector<LcProcSpec>& v, QString& err) const;
    void fillWorkload(const std::vector<LcProcSpec>& v);
    void showSnapshot(int pos, bool forward);
    void updateTree(const LcSnapshot& s);
    void updatePcb(const LcSnapshot& s);
    void afterManual(const QString& err);
    int selectedPid() const;

    ModuleHeader* header_;
    QComboBox *mode_, *preset_, *policy_, *target_, *demo_;
    QSpinBox *quantum_, *ioTicks_;
    QTableWidget* work_;
    QGroupBox* manualBox_;
    QCheckBox* twoState_;
    LifecycleView* view_;
    QTreeWidget* tree_;
    QTableWidget* pcb_;
    QLabel *explain_, *demoStatus_;
    PlaybackBar* bar_;
    QTabWidget* bottom_;
    QPlainTextEdit* log_;
    LifecycleTimeline* timeline_;
    QScrollArea* timelineScroll_;
    QTableWidget* demoTable_;
    QTreeWidget* demoTree_;
    QPushButton *demoStart_, *demoStop_;
    NativeRunner* native_;
    std::unique_ptr<LifecycleSim> sim_;
    QMap<QString, QString> lastPcb_;
    int lastPcbPid_ = -1;
    bool loading_ = false;
};
