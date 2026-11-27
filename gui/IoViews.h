// IoViews.h - display of core/iomodel.h traces (no logic of its own).
//   SystemDiagram: CPU, interrupt controller, RAM, system bus, device + controller (status
//                  register, buffer) and DMA controller; the route used in the current tick is
//                  highlighted and a token (status check / data unit / IRQ) travels along it.
//   IoTimeline:    Gantt-style lanes (CPU, device, bus, DMA / IRQ) on one logical time scale,
//                  for one run or for the three modes aligned (comparison).
#pragma once
#include <QPointF>
#include <QVariantAnimation>
#include <QWidget>
#include <vector>
#include "iomodel.h"

class SystemDiagram : public QWidget {
    Q_OBJECT
public:
    explicit SystemDiagram(QWidget* parent = nullptr);
    void setResult(const IoResult* r);
    void setTick(int shownTicks, int animMs);  // shownTicks = number of ticks completed
    QSize sizeHint() const override { return QSize(640, 400); }
    QSize minimumSizeHint() const override { return QSize(280, 180); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const IoResult* r_ = nullptr;
    int shown_ = 0;
    double t_ = 1.0;
    QVariantAnimation anim_;
};

class IoTimeline : public QWidget {
    Q_OBJECT
public:
    explicit IoTimeline(QWidget* parent = nullptr);
    // One result: CPU, device, bus, DMA lanes. Several: CPU + bus lane per mode (aligned).
    void setResults(const std::vector<const IoResult*>& rs, int upto);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<const IoResult*> rs_;
    int upto_ = 0;
};

QColor cpuActColor(CpuAct a);
