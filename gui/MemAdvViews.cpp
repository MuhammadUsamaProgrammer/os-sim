// MemAdvViews.cpp - painting of the advanced memory views.
#include "MemAdvViews.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <map>
#include "ModuleKit.h"
#include "Style.h"

static QColor ink(const QWidget* w) { return w->palette().text().color(); }
static QColor faint(const QWidget* w, int a) { QColor c = ink(w); c.setAlpha(a); return c; }
static QColor ownerColor(const std::string& o) {
    if (o == "OS") return ui::semIdle();
    unsigned h = 0;
    for (char c : o) h = h * 31 + (unsigned char)c;
    return ui::pidColor((int)(h % 12));
}
static void setupAnim(QVariantAnimation& a, QWidget* w, double& t) {
    a.setStartValue(0.0);
    a.setEndValue(1.0);
    a.setEasingCurve(QEasingCurve::InOutCubic);
    QObject::connect(&a, &QVariantAnimation::valueChanged, w, [w, &t](const QVariant& v) { t = v.toDouble(); w->update(); });
}
static void startAnim(QVariantAnimation& a, double& t, int ms) {
    a.stop();
    t = 1;
    if (ms > 0) { t = 0; a.setDuration(ms); a.start(); }
}

// ================================================================ AllocBarView

AllocBarView::AllocBarView(QWidget* parent) : QWidget(parent) { setupAnim(anim_, this, t_); setAccessibleName("Memory layout bars"); }

void AllocBarView::setResults(const std::vector<const AllocResult*>& rs, int focus) { rs_ = rs; focus_ = focus; step_ = 0; t_ = 1; update(); }
void AllocBarView::setStep(int step, int animMs) { step_ = step; startAnim(anim_, t_, animMs); update(); }

void AllocBarView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (rs_.empty()) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Enter a valid memory layout and operations"); return; }
    QFont small = font(), bold = font();
    small.setPointSizeF(small.pointSizeF() * 0.82);
    bold.setBold(true);
    int total = 0;
    for (const MemBlock& b : rs_[0]->initial) total += b.size;
    const double L = 110, R = 12;
    double W = width() - L - R, laneH = std::min(90.0, (height() - 40.0) / rs_.size());
    auto X = [&](double a) { return L + a / total * W; };
    // address axis
    p.setFont(small);
    p.setPen(ink(this));
    for (int k = 0; k <= 8; ++k) {
        int a = total * k / 8;
        p.drawText(QRectF(X(a) - 30, 2, 60, 14), Qt::AlignCenter, QString::number(a));
    }
    for (size_t i = 0; i < rs_.size(); ++i) {
        const AllocResult& r = *rs_[i];
        double y = 22 + i * laneH;
        const std::vector<MemBlock>& now = step_ == 0 ? r.initial : r.steps[step_ - 1].layout;
        const std::vector<MemBlock>& prev = step_ <= 1 ? r.initial : r.steps[step_ - 2].layout;
        bool focus = (int)i == focus_;
        p.setFont(bold);
        p.setPen(ink(this));
        p.drawText(QRectF(4, y, L - 8, laneH * 0.5), Qt::AlignLeft | Qt::AlignVCenter, r.fixedPartitions && rs_.size() == 1 ? "Fixed partitions" : fitAlgoName(r.algo));
        p.setFont(small);
        if (step_ > 0) {
            const AllocStep& s = r.steps[step_ - 1];
            p.setPen(s.ok ? ui::hitColor().darker(130) : ui::semError());
            p.drawText(QRectF(4, y + laneH * 0.45, L - 8, 16), Qt::AlignLeft, s.ok ? "placed" : "FAILED");
        }
        QRectF bar(L, y + 4, W, laneH * 0.5);
        // animate: blocks of the new layout grow from their previous geometry (same owner)
        std::map<std::string, std::pair<int, int>> prevGeom;
        for (const MemBlock& b : prev) if (!b.isHole()) prevGeom[b.owner] = {b.start, b.size};
        for (const MemBlock& b : now) {
            double s0 = b.start, z0 = b.size;
            if (!b.isHole() && prevGeom.count(b.owner)) { s0 = prevGeom[b.owner].first; z0 = prevGeom[b.owner].second; }
            else if (!b.isHole()) { s0 = b.start; z0 = 0; }
            double s = s0 + (b.start - s0) * t_, z = z0 + (b.size - z0) * t_;
            QRectF rr(X(s), bar.top(), X(s + z) - X(s), bar.height());
            if (b.isHole()) {
                p.setPen(QPen(faint(this, 120), 1));
                p.setBrush(palette().base());
                p.drawRect(QRectF(X(b.start), bar.top(), X(b.start + b.size) - X(b.start), bar.height()));
                continue;
            }
            QColor c = ownerColor(b.owner);
            if (!focus) c.setAlpha(150);
            p.setPen(QPen(ui::darkInk(), 1));
            p.setBrush(b.owner == "OS" ? QBrush(c, Qt::BDiagPattern) : QBrush(c));
            p.drawRect(rr);
            if (r.fixedPartitions && b.requested > 0 && b.requested < b.size) {           // internal waste
                double used = rr.width() * b.requested / b.size;
                p.setBrush(QBrush(ui::semError(), Qt::Dense5Pattern));
                p.drawRect(QRectF(rr.left() + used, rr.top(), rr.width() - used, rr.height()));
            }
            if (rr.width() > 26) {
                p.setPen(ui::darkInk());
                p.drawText(rr, Qt::AlignCenter, QString::fromStdString(b.owner));
            }
        }
        // hole sizes, candidates and the chosen hole
        const AllocStep* s = step_ > 0 ? &r.steps[step_ - 1] : nullptr;
        const std::vector<MemBlock>& before = step_ <= 1 ? r.initial : r.steps[step_ - 2].layout;
        if (s && s->op.kind == AllocOp::Alloc) {
            for (const MemBlock& b : before) {
                bool cand = std::find(s->candidates.begin(), s->candidates.end(), b.start) != s->candidates.end();
                if (!cand) continue;
                bool chosen = b.start == s->chosen;
                p.setPen(QPen(chosen ? ui::semRunning() : ui::semReady(), chosen ? 3 : 1.5, chosen ? Qt::SolidLine : Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(X(b.start), bar.top() - 3, X(b.start + b.size) - X(b.start), bar.height() + 6));
            }
        }
        p.setPen(ink(this));
        for (const MemBlock& b : now)
            if (b.isHole() && X(b.start + b.size) - X(b.start) > 20)
                p.drawText(QRectF(X(b.start), bar.bottom() + 1, X(b.start + b.size) - X(b.start), 14), Qt::AlignCenter, QString::number(b.size));
        AllocMetrics m = r.metrics(step_);
        p.drawText(QRectF(L, bar.bottom() + 14, W, 14), Qt::AlignLeft,
                   QString("free %1 in %2 hole(s), largest %3, failures %4%5")
                       .arg(m.freeBytes).arg(m.holes).arg(m.largestHole).arg(m.failures)
                       .arg(r.fixedPartitions ? QString(", internal waste %1").arg(m.internalWaste) : QString()));
    }
    // legend
    p.setFont(small);
    double y = height() - 16, x = 4;
    auto leg = [&](const QBrush& b, const QString& t, const QPen& pen = QPen(Qt::black, 1)) {
        p.setPen(pen);
        p.setBrush(b);
        p.drawRect(QRectF(x, y, 14, 10));
        p.setPen(ink(this));
        p.drawText(QPointF(x + 18, y + 9), t);
        x += 30 + p.fontMetrics().horizontalAdvance(t);
    };
    leg(QBrush(ui::semIdle(), Qt::BDiagPattern), "OS (reserved)");
    leg(palette().base(), "hole (free)");
    leg(Qt::NoBrush, "large enough", QPen(ui::semReady(), 1.5, Qt::DashLine));
    leg(Qt::NoBrush, "chosen", QPen(ui::semRunning(), 3));
    leg(QBrush(ui::semError(), Qt::Dense5Pattern), "internal waste");
}

// ================================================================ TranslationView

TranslationView::TranslationView(QWidget* parent) : QWidget(parent) { setupAnim(anim_, this, t_); setAccessibleName("Address translation"); }

void TranslationView::setPaging(const PagingConfig* c, const XlateResult* r) { pc_ = c; segs_ = nullptr; r_ = r; shown_ = 0; update(); }
void TranslationView::setSegmentation(const std::vector<SegmentDesc>* segs, uint64_t phys, const XlateResult* r, int seg) {
    segs_ = segs; pc_ = nullptr; phys_ = phys; r_ = r; segment_ = seg; shown_ = 0; update();
}
void TranslationView::setStep(int shown, int animMs) { shown_ = shown; startAnim(anim_, t_, animMs); update(); }

void TranslationView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Enter a valid configuration"); return; }
    if (pc_) paintPaging(p);
    else if (segs_) paintSegments(p);
}

static void box(QPainter& p, const QRectF& r, const QString& title, const QColor& c, bool active, const QWidget* w) {
    QColor f = c;
    f.setAlpha(active ? 70 : 22);
    p.setPen(QPen(c, active ? 3 : 1.4));
    p.setBrush(f);
    p.drawRoundedRect(r, 7, 7);
    p.setPen(ink(w));
    QFont b = p.font();
    b.setBold(true);
    QFont old = p.font();
    p.setFont(b);
    p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, title);
    p.setFont(old);
}

static void flowArrow(QPainter& p, QPointF a, QPointF b, bool active, double t, const QColor& c) {
    p.setPen(QPen(active ? c : QColor(150, 150, 150, 120), active ? 3 : 1.2));
    QPointF e = active ? a + (b - a) * t : b;
    p.drawLine(a, e);
    if (!active || t >= 1) {
        QPointF d = b - a;
        double len = std::hypot(d.x(), d.y());
        if (len < 1) return;
        d /= len;
        QPointF n(-d.y(), d.x());
        p.setBrush(active ? c : QColor(150, 150, 150, 120));
        p.drawPolygon(QPolygonF({b, b - d * 9 + n * 4.5, b - d * 9 - n * 4.5}));
    }
}

void TranslationView::paintPaging(QPainter& p) {
    const PagingConfig& c = *pc_;
    const XlateResult& r = *r_;
    const double W = width(), H = height();
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    QFont mono = small;
    mono.setFamily("monospace");
    p.setFont(small);
    int n = shown_;                                       // number of translation steps shown
    bool fail = !r.ok();
    QRectF cpu(8, 10, W * 0.2, 60), la(8, 90, W * 0.36, 86), pt(W * 0.42, 10, W * 0.22, H - 20), ram(W * 0.7, 10, W * 0.28, H - 20),
        res(8, H - 96, W * 0.36, 84);
    box(p, cpu, "CPU", ui::semRunning(), n >= 1, this);
    p.setPen(ink(this));
    p.drawText(cpu.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("logical address %1").arg(r.address));
    // logical address split into bit fields
    box(p, la, "MMU: split the address", ui::semReady(), n >= 2, this);
    if (n >= 2 && r.outcome != XlateOutcome::OutOfRange) {
        int pb = std::max(1, r.pageBits), ob = r.offsetBits;
        QString pbits = QString::number(r.page, 2).rightJustified(pb, '0');
        QString obits = QString::number(r.offset, 2).rightJustified(ob, '0');
        double bw = (la.width() - 16) / (pb + ob);
        bw = std::min(bw, 14.0);
        p.setFont(mono);
        for (int i = 0; i < pb + ob; ++i) {
            QRectF b(la.left() + 8 + i * bw, la.top() + 24, bw, 18);
            p.setPen(QPen(faint(this, 120), 1));
            p.setBrush(i < pb ? ui::semReady().lighter(160) : ui::semWaiting().lighter(160));
            p.drawRect(b);
            p.setPen(ui::darkInk());
            p.drawText(b, Qt::AlignCenter, i < pb ? QString(pbits[i]) : QString(obits[i - pb]));
        }
        p.setFont(small);
        p.setPen(ink(this));
        p.drawText(la.adjusted(6, 46, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                   QString("page p = %1 (%2 bits)   offset d = %3 (%4 bits)").arg(r.page).arg(r.pageBits).arg(r.offset).arg(r.offsetBits));
        p.drawText(la.adjusted(6, 62, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                   QString("p = %1 / %2,  d = %1 % %2").arg(r.address).arg(c.pageSize));
    } else if (n >= 1 && r.outcome == XlateOutcome::OutOfRange) {
        p.setPen(ui::semError());
        p.drawText(la.adjusted(6, 26, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   QString("%1 >= logical size %2: trap, no lookup").arg(r.address).arg(c.logicalSize));
    }
    // page table
    box(p, pt, "Page table", ui::semDone(), n >= 3, this);
    int pages = (int)c.table.size();
    double rowH = std::min(20.0, (pt.height() - 40) / std::max(1, pages));
    int first = 0;
    int maxRows = int((pt.height() - 40) / rowH);
    if (pages > maxRows) first = std::clamp((int)r.page - maxRows / 2, 0, pages - maxRows);
    p.setPen(ink(this));
    p.drawText(QRectF(pt.left() + 6, pt.top() + 20, pt.width(), 14), Qt::AlignLeft, "page  frame  v p w");
    for (int i = first; i < pages && i - first < maxRows; ++i) {
        const PageEntry& e = c.table[i];
        QRectF row(pt.left() + 4, pt.top() + 36 + (i - first) * rowH, pt.width() - 8, rowH - 1);
        bool hit = n >= 3 && (uint64_t)i == r.page && r.outcome != XlateOutcome::OutOfRange;
        if (hit) { QColor h = fail && n >= 4 ? ui::semError() : ui::semRunning(); h.setAlpha(110); p.fillRect(row, h); }
        p.setPen(ink(this));
        p.setFont(mono);
        p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1  %2  %3 %4 %5").arg(i, 4).arg(e.valid && e.present ? QString::number(e.frame) : QString("-"), 5)
                       .arg(e.valid ? 1 : 0).arg(e.present ? 1 : 0).arg(e.writable ? 1 : 0));
        p.setFont(small);
    }
    // RAM frames
    box(p, ram, "Physical memory (frames)", ui::semWaiting(), n >= 6, this);
    int frames = (int)(c.physicalSize / c.pageSize);
    double fh = std::min(26.0, (ram.height() - 30) / std::max(1, frames));
    int maxF = int((ram.height() - 30) / fh), f0 = 0;
    if (frames > maxF) f0 = std::clamp((int)r.frame - maxF / 2, 0, frames - maxF);
    std::map<int, int> owner;
    for (int i = 0; i < pages; ++i) if (c.table[i].valid && c.table[i].present) owner[c.table[i].frame] = i;
    for (int f = f0; f < frames && f - f0 < maxF; ++f) {
        QRectF fr(ram.left() + 6, ram.top() + 24 + (f - f0) * fh, ram.width() - 12, fh - 2);
        bool target = n >= 5 && r.ok() && (uint64_t)f == r.frame;
        p.setPen(QPen(faint(this, 120), 1));
        p.setBrush(target ? ui::semRunning().lighter(150) : owner.count(f) ? ui::semReady().lighter(175) : palette().base().color());
        p.drawRect(fr);
        p.setPen(target ? ui::darkInk() : ink(this));
        p.drawText(fr.adjusted(4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("frame %1  [%2..%3)%4").arg(f).arg((uint64_t)f * c.pageSize).arg((uint64_t)(f + 1) * c.pageSize)
                       .arg(owner.count(f) ? QString("  page %1").arg(owner[f]) : QString()));
        if (target && n >= 7) {                                              // offset marker inside the frame
            double x = fr.left() + fr.width() * (double)r.offset / c.pageSize;
            p.setPen(QPen(ui::semError(), 3));
            p.drawLine(QPointF(x, fr.top()), QPointF(x, fr.bottom()));
        }
    }
    // result
    box(p, res, "Result", fail ? ui::semError() : ui::semRunning(), n >= (int)r.steps.size(), this);
    p.setPen(ink(this));
    if (n >= (int)r.steps.size())
        p.drawText(res.adjusted(6, 22, -4, -2), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   r.ok() ? QString("physical = %1 x %2 + %3 = %4").arg(r.frame).arg(c.pageSize).arg(r.offset).arg(r.physical)
                          : QString("%1: %2").arg(xlateOutcomeName(r.outcome)).arg(QString::fromStdString(r.message)));
    // flow arrows driven by the step number
    QColor fc = ui::semRunning().darker(110);
    flowArrow(p, QPointF(cpu.center().x(), cpu.bottom()), QPointF(cpu.center().x(), la.top()), n == 2, t_, fc);
    flowArrow(p, QPointF(la.right(), la.center().y()), QPointF(pt.left(), la.center().y()), n == 3, t_, fc);
    if (r.ok()) {
        flowArrow(p, QPointF(pt.right(), H * 0.4), QPointF(ram.left(), H * 0.4), n == 5 || n == 6, t_, fc);
        flowArrow(p, QPointF(ram.left(), H - 60), QPointF(res.right(), H - 60), n >= 7, t_, fc);
    }
}

void TranslationView::paintSegments(QPainter& p) {
    const std::vector<SegmentDesc>& segs = *segs_;
    const XlateResult& r = *r_;
    const double W = width(), H = height();
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    int n = shown_;
    bool fail = !r.ok();
    QRectF la(8, 10, W * 0.3, 54), st(8, 80, W * 0.42, 30 + segs.size() * 20.0), cmp(8, st.bottom() + 14, W * 0.2, 64),
        add(W * 0.24, st.bottom() + 14, W * 0.2, 64), ram(W * 0.56, 10, W * 0.42, H - 20);
    box(p, la, "Logical address (s, d)", ui::semRunning(), n >= 1, this);
    p.setPen(ink(this));
    p.drawText(la.adjusted(6, 24, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("s = %1,  d = %2").arg(segment_).arg(r.offset));
    box(p, st, "Segment table", ui::semDone(), n >= 2, this);
    p.drawText(QRectF(st.left() + 6, st.top() + 20, st.width(), 14), Qt::AlignLeft, "seg  name        base   limit  rwx");
    const SegmentDesc* hitSeg = nullptr;
    for (size_t i = 0; i < segs.size(); ++i) {
        const SegmentDesc& s = segs[i];
        QRectF row(st.left() + 4, st.top() + 36 + i * 20.0, st.width() - 8, 19);
        if (n >= 2 && s.id == segment_) { hitSeg = &s; QColor h = fail && n >= 3 ? ui::semError() : ui::semRunning(); h.setAlpha(110); p.fillRect(row, h); }
        p.setPen(ink(this));
        p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1  %2 %3 %4  %5%6%7").arg(s.id, 3).arg(QString::fromStdString(s.name).leftJustified(10, ' ', true))
                       .arg(s.base, 6).arg(s.limit, 6).arg(s.read ? "r" : "-").arg(s.write ? "w" : "-").arg(s.exec ? "x" : "-"));
    }
    box(p, cmp, "d < limit ?", ui::semWaiting(), n >= 3, this);
    if (n >= 3 && hitSeg) {
        p.setPen(r.outcome == XlateOutcome::Protection && r.steps.size() <= 3 ? ui::semError() : ink(this));
        bool inRange = r.offset < hitSeg->limit;
        p.drawText(cmp.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   QString("%1 < %2 : %3").arg(r.offset).arg(hitSeg->limit).arg(inRange ? "yes" : "NO -> trap"));
    }
    box(p, add, "base + d", ui::semReady(), n >= 5 && r.ok(), this);
    if (n >= 5 && r.ok() && hitSeg) {
        p.setPen(ink(this));
        p.drawText(add.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("%1 + %2 = %3").arg(hitSeg->base).arg(r.offset).arg(r.physical));
    }
    // physical memory with variable-sized segments
    box(p, ram, QString("Physical memory (0..%1)").arg(phys_), ui::semWaiting(), n >= 5, this);
    double top = ram.top() + 24, hh = ram.height() - 30;
    auto Y = [&](double a) { return top + a / phys_ * hh; };
    for (const SegmentDesc& s : segs) {
        QRectF sr(ram.left() + 30, Y(s.base), ram.width() - 40, std::max(2.0, Y(s.base + s.limit) - Y(s.base)));
        QColor c = ui::pidColor(s.id);
        if (!(n >= 2 && s.id == segment_)) c.setAlpha(120);
        p.setPen(QPen(ui::darkInk(), 1));
        p.setBrush(c);
        p.drawRect(sr);
        p.setPen(ui::darkInk());
        if (sr.height() > 12) p.drawText(sr, Qt::AlignCenter, QString("seg %1 %2 [%3..%4)").arg(s.id).arg(QString::fromStdString(s.name)).arg(s.base).arg(s.base + s.limit));
    }
    if (n >= 5 && r.ok()) {
        double y = Y((double)r.physical);
        p.setPen(QPen(ui::semError(), 3));
        p.drawLine(QPointF(ram.left() + 20, y), QPointF(ram.right() - 6, y));
        p.setPen(ink(this));
        p.drawText(QRectF(ram.left() + 2, y - 8, 28, 16), Qt::AlignCenter, "->");
    }
    if (n >= (int)r.steps.size()) {
        QRectF res(8, H - 50, W * 0.46, 42);
        box(p, res, r.ok() ? "Result" : "Trap", r.ok() ? ui::semRunning() : ui::semError(), true, this);
        p.setPen(ink(this));
        p.drawText(res.adjusted(6, 20, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString::fromStdString(r.message));
    }
}

// ================================================================ DemandPagingView

DemandPagingView::DemandPagingView(QWidget* parent) : QWidget(parent) { setupAnim(anim_, this, t_); setAccessibleName("Demand paging"); }
void DemandPagingView::setResult(const DpResult* r) { r_ = r; step_ = 0; t_ = 1; update(); }
void DemandPagingView::setStep(int step, int animMs) { step_ = step; startAnim(anim_, t_, animMs); update(); }

void DemandPagingView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Enter a valid reference string"); return; }
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const double W = width(), H = height();
    const DpStep* s = step_ > 0 ? &r_->steps[step_ - 1] : nullptr;
    std::vector<int> pages;
    for (const DpStep& x : r_->steps) if (std::find(pages.begin(), pages.end(), x.ref.page) == pages.end()) pages.push_back(x.ref.page);
    std::sort(pages.begin(), pages.end());
    QRectF pt(8, 10, W * 0.26, H - 20), ram(W * 0.36, 10, W * 0.26, H - 20), disk(W * 0.72, 10, W * 0.26, H - 20);
    box(p, pt, "Page table", ui::semDone(), s != nullptr, this);
    box(p, ram, QString("RAM: %1 frames").arg(r_->frames), ui::semRunning(), s && !s->hit, this);
    box(p, disk, "Backing store (disk)", ui::semIdle(), s && !s->hit, this);
    std::vector<int> frames = s ? s->frames : std::vector<int>(r_->frames, -1);
    std::vector<bool> dirty = s ? s->dirty : std::vector<bool>(r_->frames, false);
    auto frameOf = [&](int page) { for (size_t i = 0; i < frames.size(); ++i) if (frames[i] == page) return (int)i; return -1; };
    double rowH = std::min(22.0, (pt.height() - 40) / std::max<size_t>(1, pages.size()));
    p.setPen(ink(this));
    p.drawText(QRectF(pt.left() + 6, pt.top() + 20, pt.width(), 14), Qt::AlignLeft, "page  frame  present dirty");
    for (size_t i = 0; i < pages.size(); ++i) {
        int f = frameOf(pages[i]);
        QRectF row(pt.left() + 4, pt.top() + 36 + i * rowH, pt.width() - 8, rowH - 1);
        if (s && pages[i] == s->ref.page) { QColor h = s->hit ? ui::semRunning() : ui::semError(); h.setAlpha(100); p.fillRect(row, h); }
        p.setPen(ink(this));
        p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1   %2   %3   %4").arg(pages[i], 4).arg(f >= 0 ? QString::number(f) : QString("-"), 4)
                       .arg(f >= 0 ? 1 : 0).arg(f >= 0 && dirty[f] ? 1 : 0));
    }
    double fh = std::min(46.0, (ram.height() - 40) / std::max(1, r_->frames));
    std::vector<QRectF> fr;
    for (int i = 0; i < r_->frames; ++i) {
        QRectF b(ram.left() + 10, ram.top() + 30 + i * fh, ram.width() - 20, fh - 6);
        fr.push_back(b);
        p.setPen(QPen(faint(this, 120), 1));
        p.setBrush(palette().base());
        p.drawRect(b);
        p.setPen(ink(this));
        p.drawText(QRectF(b.left() - 2, b.top(), 0, b.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(i));
    }
    double dh = std::min(22.0, (disk.height() - 40) / std::max<size_t>(1, pages.size()));
    std::map<int, QRectF> dr;
    for (size_t i = 0; i < pages.size(); ++i) {
        QRectF b(disk.left() + 10, disk.top() + 30 + i * dh, disk.width() - 20, dh - 3);
        dr[pages[i]] = b;
        p.setPen(QPen(faint(this, 90), 1));
        p.setBrush(faint(this, 15));
        p.drawRect(b);
        p.setPen(ink(this));
        p.drawText(b, Qt::AlignCenter, QString("page %1").arg(pages[i]));
    }
    // resident pages (the faulting page and a dirty victim are animated)
    for (int i = 0; i < r_->frames; ++i) {
        if (frames[i] < 0) continue;
        QRectF target = fr[i];
        QRectF at = target;
        if (s && !s->hit && frames[i] == s->ref.page && dr.count(frames[i])) {
            QRectF src = dr[frames[i]];
            QPointF c = src.center() + (target.center() - src.center()) * t_;
            at = QRectF(c - QPointF(target.width() / 2, target.height() / 2), target.size());
        }
        p.setPen(QPen(ui::darkInk(), dirty[i] ? 2.5 : 1));
        p.setBrush(ui::pidColor(frames[i]));
        p.drawRoundedRect(at, 6, 6);
        p.setPen(ui::darkInk());
        p.drawText(at, Qt::AlignCenter, QString("page %1%2").arg(frames[i]).arg(dirty[i] ? "  (dirty)" : ""));
    }
    if (s && s->victim >= 0 && t_ < 1.0 && dr.count(s->victim)) {
        QRectF src = fr[s->frame], dst = dr[s->victim];
        QPointF c = src.center() + (dst.center() - src.center()) * t_;
        QRectF at(c - QPointF(src.width() / 2, src.height() / 2), src.size());
        QColor col = ui::pidColor(s->victim);
        col.setAlpha(170);
        p.setPen(QPen(s->victimDirty ? ui::semError() : ui::darkInk(), s->victimDirty ? 2.5 : 1, Qt::DashLine));
        p.setBrush(col);
        p.drawRoundedRect(at, 6, 6);
        p.setPen(ui::darkInk());
        p.drawText(at, Qt::AlignCenter, QString("page %1 %2").arg(s->victim).arg(s->victimDirty ? "write-back" : "discard"));
    }
}

// ================================================================ SwapView

SwapView::SwapView(QWidget* parent) : QWidget(parent) { setupAnim(anim_, this, t_); setAccessibleName("Whole-process swapping"); }
void SwapView::setResult(const SwapResult* r) { r_ = r; step_ = 0; t_ = 1; update(); }
void SwapView::setStep(int step, int animMs) { step_ = step; startAnim(anim_, t_, animMs); update(); }

void SwapView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_) return;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const double W = width(), H = height();
    std::map<std::string, int> size;
    for (const SwapProc& pr : r_->procs) size[pr.name] = pr.size;
    const SwapStep* s = step_ > 0 ? &r_->steps[step_ - 1] : nullptr;
    std::vector<std::string> resident = s ? s->resident : std::vector<std::string>{};
    std::vector<std::string> backing;
    if (s) backing = s->backing; else for (const SwapProc& pr : r_->procs) backing.push_back(pr.name);
    QRectF ram(10, 30, W - 20, H * 0.28), disk(10, H * 0.6, W - 20, H * 0.3);
    box(p, ram, QString("RAM (capacity %1 units)%2").arg(r_->capacity).arg(s ? QString(", free %1").arg(s->freeAfter) : QString()), ui::semRunning(), s != nullptr, this);
    box(p, disk, "Backing store (swap space)", ui::semIdle(), s != nullptr, this);
    double x = ram.left() + 6, unit = (ram.width() - 12) / r_->capacity;
    std::map<std::string, QRectF> ramPos, diskPos;
    for (const std::string& nme : resident) { ramPos[nme] = QRectF(x, ram.top() + 24, size[nme] * unit, ram.height() - 30); x += size[nme] * unit; }
    double dx = disk.left() + 6;
    for (const std::string& nme : backing) { diskPos[nme] = QRectF(dx, disk.top() + 24, std::min(size[nme] * unit, 140.0), disk.height() - 30); dx += diskPos[nme].width() + 6; }
    auto drawProc = [&](const std::string& nme, QRectF at, bool moving) {
        p.setPen(QPen(ui::darkInk(), moving ? 2.5 : 1));
        p.setBrush(ownerColor(nme));
        p.drawRoundedRect(at, 6, 6);
        p.setPen(ui::darkInk());
        p.drawText(at, Qt::AlignCenter, QString("%1 (%2)").arg(QString::fromStdString(nme)).arg(size[nme]));
    };
    for (auto& [nme, rr] : diskPos) {
        bool out = s && std::find(s->swappedOut.begin(), s->swappedOut.end(), nme) != s->swappedOut.end();
        if (out && t_ < 1) {           // travelling from RAM to disk: approximate start at the top
            QRectF from(rr.left(), ram.top() + 24, rr.width(), rr.height());
            drawProc(nme, QRectF(from.topLeft() + (rr.topLeft() - from.topLeft()) * t_, rr.size()), true);
        } else drawProc(nme, rr, false);
    }
    for (auto& [nme, rr] : ramPos) {
        bool in = s && std::find(s->swappedIn.begin(), s->swappedIn.end(), nme) != s->swappedIn.end();
        if (in && t_ < 1) {
            QRectF from(rr.left(), disk.top() + 24, rr.width(), rr.height());
            drawProc(nme, QRectF(from.topLeft() + (rr.topLeft() - from.topLeft()) * t_, rr.size()), true);
        } else drawProc(nme, rr, in);
    }
    p.setPen(ink(this));
    p.drawText(QRectF(10, 4, W - 20, 20), Qt::AlignLeft, s ? QString("run %1: %2").arg(QString::fromStdString(s->run), QString::fromStdString(s->text))
                                                         : QString("Illustrative model: whole processes move between RAM and the backing store (no host memory is touched)."));
}

// ================================================================ ThrashView

ThrashView::ThrashView(QWidget* parent) : QWidget(parent) { setAccessibleName("Thrashing graphs"); }

void ThrashView::setData(const ThrashResult* a, const ThrashResult* b, const std::vector<MultiprogPoint>* mp, int faultTime) {
    a_ = a; b_ = b; mp_ = mp; faultTime_ = faultTime;
    update();
}
void ThrashView::setStep(int upto) { upto_ = upto; update(); }

void ThrashView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!a_ || !b_) return;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.82);
    p.setFont(small);
    const double W = width(), H = height();
    auto panel = [&](const ThrashResult& r, QRectF area, const QString& title) {
        box(p, area, title, ui::semReady(), true, this);
        int n = (int)r.points.size();
        int lim = std::min(upto_, n);
        double L = area.left() + 34, Rr = area.right() - 8, top = area.top() + 24, strip = 14;
        double cw = (Rr - L) / std::max(1, n);
        for (int i = 0; i < lim; ++i) {
            QRectF c(L + i * cw, top, std::max(1.0, cw - 1), strip);
            p.fillRect(c, r.points[i].fault ? ui::semError() : ui::semRunning());
            if (cw > 10) { p.setPen(ui::darkInk()); p.drawText(c, Qt::AlignCenter, QString::number(r.refs[i])); }
        }
        // graph: rolling fault rate (0..1) and working set vs frames (0..max)
        QRectF g(L, top + strip + 8, Rr - L, area.bottom() - top - strip - 30);
        p.setPen(faint(this, 90));
        p.drawRect(g);
        int maxWs = r.frames;
        for (const ThrashPoint& pt : r.points) maxWs = std::max(maxWs, pt.workingSet);
        p.setPen(ink(this));
        p.drawText(QRectF(area.left() + 2, g.top() - 6, 30, 12), Qt::AlignRight, "1.0");
        p.drawText(QRectF(area.left() + 2, g.bottom() - 6, 30, 12), Qt::AlignRight, "0");
        QPainterPath rate, ws;
        for (int i = 0; i < lim; ++i) {
            double x = L + (i + 0.5) * cw;
            QPointF a(x, g.bottom() - r.points[i].rollingRate * g.height());
            QPointF b(x, g.bottom() - double(r.points[i].workingSet) / maxWs * g.height());
            if (i == 0) { rate.moveTo(a); ws.moveTo(b); } else { rate.lineTo(a); ws.lineTo(b); }
        }
        p.setPen(QPen(ui::semError(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(rate);
        p.setPen(QPen(ui::semReady(), 2, Qt::DashLine));
        p.drawPath(ws);
        double fy = g.bottom() - double(r.frames) / maxWs * g.height();
        p.setPen(QPen(ui::semRunning(), 1.5, Qt::DotLine));
        p.drawLine(QPointF(g.left(), fy), QPointF(g.right(), fy));
        int faults = 0;
        for (int i = 0; i < lim; ++i) faults += r.points[i].fault;
        p.setPen(ink(this));
        p.drawText(QRectF(L, g.bottom() + 2, g.width(), 14), Qt::AlignLeft,
                   QString("after %1 refs: %2 faults (%3%%);  red = rolling fault rate (last %4 refs), dashed = working set (last %5 refs), dotted = frames (%6)")
                       .arg(lim).arg(faults).arg(lim ? 100 * faults / lim : 0).arg(r.window).arg(r.wsWindow).arg(r.frames));
    };
    double half = mp_ ? H * 0.34 : H * 0.48;
    panel(*a_, QRectF(6, 6, W - 12, half - 8), QString("%1 frames").arg(a_->frames));
    panel(*b_, QRectF(6, half + 2, W - 12, half - 8), QString("%1 frames").arg(b_->frames));
    if (!mp_ || mp_->empty()) return;
    QRectF area(6, 2 * half, W - 12, H - 2 * half - 6);
    box(p, area, QString("Multiprogramming model: CPU utilisation vs degree (fault service %1 ticks, logical model)").arg(faultTime_),
        ui::semWaiting(), true, this);
    QRectF g(area.left() + 40, area.top() + 26, area.width() - 60, area.height() - 50);
    p.setPen(faint(this, 90));
    p.drawRect(g);
    int n = (int)mp_->size();
    double bw = g.width() / n;
    for (int i = 0; i < n; ++i) {
        const MultiprogPoint& pt = (*mp_)[i];
        double h = pt.utilisation * g.height();
        QRectF b(g.left() + i * bw + 3, g.bottom() - h, bw - 6, h);
        p.fillRect(b, pt.framesPerProc >= 1 && pt.faults <= pt.refs / 10 ? ui::semRunning() : ui::semError());
        p.setPen(ink(this));
        p.drawText(QRectF(b.left(), g.bottom() + 2, b.width(), 14), Qt::AlignCenter, QString::number(pt.degree));
        if (bw > 26) p.drawText(QRectF(b.left(), b.top() - 14, b.width(), 14), Qt::AlignCenter, QString::number(int(pt.utilisation * 100)) + "%");
    }
    p.drawText(QRectF(area.left() + 2, g.top() - 6, 36, 12), Qt::AlignRight, "100%");
    p.drawText(QRectF(g.left(), area.bottom() - 16, g.width(), 14), Qt::AlignCenter,
               "degree of multiprogramming (red = frames per process below the working set: thrashing)");
}
