// DiskTab.h - M1 Disk Scheduling tab: request queue editor, platter + seek graph driven by
// the core/disk.h move trace, service table, pseudocode, comparison of all six algorithms.
#pragma once
#include <QWidget>
#include <optional>
#include <vector>
#include "disk.h"

class QComboBox; class QSpinBox; class QTableWidget; class QLabel; class QPlainTextEdit; class QListWidget;
class QTabWidget; class PlaybackBar; class PseudoCodeView; class ModuleHeader; class PlatterWidget;
class SeekGraph; class BarChartWidget;

class DiskTab : public QWidget {
    Q_OBJECT
public:
    explicit DiskTab(QWidget* parent = nullptr);
    // ---- actions (public for the menu and the self-test)
    bool loadFile(const QString& path);           // samples/disk_*.txt
    bool loadPreset(int index);
    bool setConfig(const DiskConfig& c);          // fills the editor and rebuilds
    void setAlgorithm(DiskAlgo a);
    bool rebuild();                               // new trace from the inputs (false + message if invalid)
    bool compare();
    PlaybackBar* playback() const { return bar_; }
    const std::optional<DiskResult>& result() const { return result_; }
    const std::vector<DiskResult>& comparison() const { return compare_; }
    QString statusText() const;
    QString reportText() const;
    QString csvSection() const;
signals:
    void logMessage(const QString& line);
private slots:
    void onStep(int pos, bool forward);
    void onInputsChanged();
private:
    bool readConfig(DiskConfig& c, QString& err) const;
    void fillRequests(const std::vector<int>& req);
    void updateInspector(int step);

    ModuleHeader* header_;
    QComboBox *preset_, *algo_, *dir_;
    QSpinBox *minC_, *maxC_, *head_, *randCount_, *randSeed_;
    QTableWidget* reqTable_;
    PlatterWidget* platter_;
    SeekGraph* graph_;
    QLabel *calc_, *reason_;
    QListWidget* pending_;
    QTableWidget* service_;
    PseudoCodeView* code_;
    PlaybackBar* bar_;
    QTabWidget* bottom_;
    QPlainTextEdit* log_;
    QTableWidget* cmpTable_;
    BarChartWidget* cmpChart_;
    std::optional<DiskResult> result_;
    std::vector<DiskResult> compare_;
    bool loading_ = false;
};
