// LifecycleViews.h - display of core/procmgmt.h snapshots (no logic of its own).
//   LifecycleView:     5-state (or 2-state) diagram; every process is a token inside its state
//                      node and glides along the transition arrow of the current event; below it
//                      the CPU, the ready queue and the waiting queue, and the logical clock.
//   LifecycleTimeline: one row per process, one column per tick, coloured by state.
#pragma once
#include <QMap>
#include <QPointF>
#include <QVariantAnimation>
#include <QWidget>
#include <vector>
#include "procmgmt.h"

class LifecycleView : public QWidget {
    Q_OBJECT
public:
    explicit LifecycleView(QWidget* parent = nullptr);
    void setSnapshot(const LcSnapshot* cur, const LcSnapshot* prev, int animMs);
    void setTwoState(bool on);
    QSize sizeHint() const override { return QSize(640, 420); }
    QSize minimumSizeHint() const override { return QSize(420, 320); }
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    QRectF nodeRect(ProcState s) const;
    QMap<int, QPointF> tokenPositions(const LcSnapshot& s) const;
    const LcSnapshot* cur_ = nullptr;
    QMap<int, QPointF> from_, to_;
    double t_ = 1.0;
    bool two_ = false;
    QVariantAnimation anim_;
};

class LifecycleTimeline : public QWidget {
    Q_OBJECT
public:
    explicit LifecycleTimeline(QWidget* parent = nullptr);
    void setTrace(const std::vector<LcSnapshot>* trace, int upto);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const std::vector<LcSnapshot>* trace_ = nullptr;
    int upto_ = 0;
};

// Colour of a process state (always shown together with its name).
QColor lcStateColor(ProcState s);
