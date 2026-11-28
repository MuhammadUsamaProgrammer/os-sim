// MemoryTab.h - page replacement GUI (FIFO / LRU / Optimal), frame table,
// comparison chart and a page-table panel for the processes of the Scheduling tab.
#pragma once
#include <QWidget>
#include <functional>
#include <vector>
#include "memory.h"
#include "pcb.h"

class QLineEdit; class QSpinBox; class QComboBox; class QTabWidget; class QTableWidget;
class QLabel; class BarChartWidget; class QGroupBox;

class MemoryTab : public QWidget {
    Q_OBJECT
public:
    explicit MemoryTab(QWidget* parent = nullptr);

    // Where the page-table panel gets the process list from (the Scheduling tab).
    void setProcessProvider(std::function<std::vector<PCB>()> f) { procs_ = std::move(f); refreshPids(); }
    bool loadRefFile(const QString& path);
    // Removes the previous results (frame tables, summaries, chart); inputs stay.
    void clearResults();
    void setRefString(const QString& s);
    void setFrames(int n);
    void setMode(int index);                  // 0 FIFO, 1 LRU, 2 Optimal, 3 Compare all
    bool run();                               // runs the selected mode
    void beladyExample();                     // FIFO with 3 and 4 frames
    const std::vector<MemResult>& lastResults() const { return results_; }
    QString beladyText() const { return belady_; }

signals:
    void logMessage(const QString& line);

public slots:
    void refreshPids();                       // process list changed
    void showPageTable();

private:
    QTableWidget* makeFrameTable(const MemResult& r);
    QWidget* makeSummary(const MemResult& r);

    QLineEdit* refEdit_;
    QSpinBox* framesSpin_;
    QComboBox* modeCombo_;
    QTabWidget* tables_;
    QWidget* summaryRow_;
    BarChartWidget* chart_;
    QLabel* beladyLabel_;
    QComboBox* pidCombo_;
    QSpinBox* physSpin_;
    QTableWidget* pageTable_;
    QLabel* allocLabel_;
    std::function<std::vector<PCB>()> procs_;
    std::vector<MemResult> results_;
    QString belady_;
};
