// MemAdvViews.h - QPainter views for the advanced memory pages (display of core/memadv.h).
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include <vector>
#include "memadv.h"

// Three memory bars (First / Best / Worst Fit) at the same operation index; blocks glide
// between consecutive layouts; candidate holes are outlined, the chosen hole highlighted.
class AllocBarView : public QWidget {
    Q_OBJECT
public:
    explicit AllocBarView(QWidget* parent = nullptr);
    void setResults(const std::vector<const AllocResult*>& rs, int focus);
    void setStep(int step, int animMs);
    QSize sizeHint() const override { return QSize(640, 360); }
    QSize minimumSizeHint() const override { return QSize(260, 170); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<const AllocResult*> rs_;
    int focus_ = 0, step_ = 0;
    double t_ = 1;
    QVariantAnimation anim_;
};

// MMU view for paging (CPU -> logical address bits -> page table -> frame -> RAM) and for
// segmentation (segment table, limit comparator, base adder, variable-sized segments).
class TranslationView : public QWidget {
    Q_OBJECT
public:
    explicit TranslationView(QWidget* parent = nullptr);
    void setPaging(const PagingConfig* c, const XlateResult* r);
    void setSegmentation(const std::vector<SegmentDesc>* segs, uint64_t phys, const XlateResult* r, int segment);
    void setStep(int shownSteps, int animMs);
    QSize sizeHint() const override { return QSize(680, 420); }
    QSize minimumSizeHint() const override { return QSize(260, 170); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void paintPaging(class QPainter& p);
    void paintSegments(class QPainter& p);
    const PagingConfig* pc_ = nullptr;
    const std::vector<SegmentDesc>* segs_ = nullptr;
    uint64_t phys_ = 0;
    int segment_ = 0;
    const XlateResult* r_ = nullptr;
    int shown_ = 0;
    double t_ = 1;
    QVariantAnimation anim_;
};

// Page table (present / dirty), RAM frames and backing store; the faulting page travels
// disk -> frame, a dirty victim frame -> disk.
class DemandPagingView : public QWidget {
    Q_OBJECT
public:
    explicit DemandPagingView(QWidget* parent = nullptr);
    void setResult(const DpResult* r);
    void setStep(int step, int animMs);
    QSize sizeHint() const override { return QSize(640, 400); }
    QSize minimumSizeHint() const override { return QSize(260, 170); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const DpResult* r_ = nullptr;
    int step_ = 0;
    double t_ = 1;
    QVariantAnimation anim_;
};

// Whole-process swapping: RAM bar with resident processes, backing store, moving processes.
class SwapView : public QWidget {
    Q_OBJECT
public:
    explicit SwapView(QWidget* parent = nullptr);
    void setResult(const SwapResult* r);
    void setStep(int step, int animMs);
    QSize sizeHint() const override { return QSize(600, 300); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const SwapResult* r_ = nullptr;
    int step_ = 0;
    double t_ = 1;
    QVariantAnimation anim_;
};

// Thrashing: per-reference hit / fault strip, rolling fault rate, working set vs frames, for
// two frame counts side by side; plus the multiprogramming utilisation curve.
class ThrashView : public QWidget {
    Q_OBJECT
public:
    explicit ThrashView(QWidget* parent = nullptr);
    void setData(const ThrashResult* a, const ThrashResult* b, const std::vector<MultiprogPoint>* mp, int faultTime);
    void setStep(int upto);
    QSize sizeHint() const override { return QSize(680, 440); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const ThrashResult *a_ = nullptr, *b_ = nullptr;
    const std::vector<MultiprogPoint>* mp_ = nullptr;
    int faultTime_ = 0, upto_ = 0;
};
