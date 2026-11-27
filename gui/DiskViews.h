// DiskViews.h - QPainter views of one disk-scheduling trace (display only).
//   PlatterWidget: schematic platter with concentric tracks, requested cylinders and an
//                  animated head/arm (illustrative, not a mechanical/angular model).
//   SeekGraph:     cylinder (x) versus service step (y) with connected points; boundary
//                  transit dashed, circular wrap dash-dot, current position highlighted.
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include "disk.h"

class PlatterWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlatterWidget(QWidget* parent = nullptr);
    void setResult(const DiskResult* r);
    void setStep(int step, int animMs);        // step = number of moves applied
    QSize sizeHint() const override { return QSize(360, 340); }
    QSize minimumSizeHint() const override { return QSize(170, 160); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const DiskResult* r_ = nullptr;
    int step_ = 0;
    double headCyl_ = 0;
    QVariantAnimation anim_;
};

class SeekGraph : public QWidget {
    Q_OBJECT
public:
    explicit SeekGraph(QWidget* parent = nullptr);
    void setResult(const DiskResult* r);
    void setStep(int step, int animMs);
    QSize sizeHint() const override { return QSize(460, 340); }
    QSize minimumSizeHint() const override { return QSize(200, 160); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const DiskResult* r_ = nullptr;
    int step_ = 0;
    double progress_ = 1.0;                    // drawing progress of the current move
    QVariantAnimation anim_;
};
