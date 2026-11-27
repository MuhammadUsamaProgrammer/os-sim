// IoTab.h - M5 I/O Management: polling, interrupts and DMA on the same workload.
#pragma once
#include <QWidget>
#include <optional>
#include <vector>
#include "iomodel.h"

class QComboBox; class QSpinBox; class QCheckBox; class QTableWidget; class QPlainTextEdit; class QTabWidget;
class QScrollArea; class PlaybackBar; class PseudoCodeView; class ModuleHeader; class SystemDiagram; class IoTimeline;
class BarChartWidget;

class IoTab : public QWidget {
    Q_OBJECT
public:
    explicit IoTab(QWidget* parent = nullptr);
    bool loadPreset(int index);
    void setMode(IoMode m);
    bool setParams(const IoParams& p);
    bool rebuild();
    PlaybackBar* playback() const { return bar_; }
    const std::optional<IoResult>& result() const { return result_; }
    const std::vector<IoResult>& comparison() const { return compare_; }
    QString statusText() const;
    QString reportText() const;
    QString csvSection() const;
signals:
    void logMessage(const QString& line);
private:
    IoParams readParams() const;
    void showTick(int pos, bool forward);
    void fillComparison();

    ModuleHeader* header_;
    QComboBox *mode_, *preset_;
    QSpinBox *units_, *interval_, *buffer_, *poll_, *work_, *copy_, *isr_, *setup_, *complete_, *rate_;
    QCheckBox* steal_;
    SystemDiagram* diagram_;
    QTableWidget* regs_;
    PseudoCodeView* code_;
    PlaybackBar* bar_;
    QTabWidget* bottom_;
    IoTimeline* timeline_;
    QScrollArea* timelineScroll_;
    QPlainTextEdit* log_;
    IoTimeline* cmpLanes_;
    QTableWidget* cmpTable_;
    BarChartWidget* cmpChart_;
    std::optional<IoResult> result_;
    std::vector<IoResult> compare_;
    bool loading_ = false;
};
