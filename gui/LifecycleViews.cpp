// LifecycleViews.cpp - state diagram with animated tokens and the state timeline.
#include "LifecycleViews.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include "ModuleKit.h"
#include "Style.h"

QColor lcStateColor(ProcState s) {
    switch (s) {
        case ProcState::New: return ui::semIdle();
        case ProcState::Ready: return ui::semReady();
        case ProcState::Running: return ui::semRunning();
        case ProcState::Waiting: return ui::semWaiting();
        case ProcState::Terminated: return ui::semDone();
    }
    return ui::semIdle();
}

static QColor ink(const QWidget* w) { return w->palette().text().color(); }
static QColor faint(const QWidget* w, int a) { QColor c = ink(w); c.setAlpha(a); return c; }
static const double kTokW = 46, kTokH = 22;

LifecycleView::LifecycleView(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    anim_.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t_ = v.toDouble(); update(); });
    setAccessibleName("Process state diagram");
}

void LifecycleView::setTwoState(bool on) {
    two_ = on;
    if (cur_) { to_ = tokenPositions(*cur_); from_ = to_; }
    update();
}

void LifecycleView::resizeEvent(QResizeEvent*) {
    if (cur_) { to_ = tokenPositions(*cur_); from_ = to_; t_ = 1; }
}

// Node rectangles of the diagram area (top ~62% of the widget).
QRectF LifecycleView::nodeRect(ProcState s) const {
    double W = width(), H = height() * 0.62;
    double nw = std::min(150.0, W * 0.2), nh = std::min(110.0, H * 0.34);
    if (two_) {
        double y = H * 0.18;
        switch (s) {
            case ProcState::Running: return QRectF(W * 0.62, y, nw * 1.2, nh * 1.4);
            case ProcState::Terminated: return QRectF(W * 0.84, H * 0.64, nw * 0.8, nh);
            default: return QRectF(W * 0.12, y, nw * 1.6, nh * 1.6);   // Not Running aggregates New/Ready/Waiting
        }
    }
    double y = H * 0.1;
    switch (s) {
        case ProcState::New: return QRectF(W * 0.02, y, nw, nh);
        case ProcState::Ready: return QRectF(W * 0.27, y, nw, nh);
        case ProcState::Running: return QRectF(W * 0.55, y, nw, nh);
        case ProcState::Terminated: return QRectF(W * 0.80, y, nw * 0.95, nh);
        case ProcState::Waiting: return QRectF(W * 0.41, H * 0.6, nw, nh);
    }
    return {};
}

QMap<int, QPointF> LifecycleView::tokenPositions(const LcSnapshot& s) const {
    QMap<int, QPointF> pos;
    QMap<int, int> count;
    auto place = [&](int pid, ProcState st) {
        QRectF r = nodeRect(two_ && st != ProcState::Running && st != ProcState::Terminated ? ProcState::Ready : st);
        int key = two_ && st != ProcState::Running && st != ProcState::Terminated ? 99 : (int)st;
        int k = count[key]++;
        int cols = std::max(1, int((r.width() - 8) / (kTokW + 4)));
        pos[pid] = QPointF(r.left() + 6 + (k % cols) * (kTokW + 4), r.top() + 26 + (k / cols) * (kTokH + 4));
    };
    // queue order inside Ready / Waiting, otherwise PCB order
    for (int pid : s.readyQueue) place(pid, ProcState::Ready);
    for (int pid : s.waitQueue) place(pid, ProcState::Waiting);
    for (const LcPCB& p : s.pcbs)
        if (p.state != ProcState::Ready && p.state != ProcState::Waiting) place(p.pid, p.state);
    return pos;
}

void LifecycleView::setSnapshot(const LcSnapshot* cur, const LcSnapshot* prev, int animMs) {
    anim_.stop();
    cur_ = cur;
    if (!cur) { update(); return; }
    QMap<int, QPointF> target = tokenPositions(*cur);
    QMap<int, QPointF> start = target;
    if (prev && animMs > 0) {
        QMap<int, QPointF> old = tokenPositions(*prev);
        for (auto it = target.begin(); it != target.end(); ++it)
            start[it.key()] = old.contains(it.key()) ? old[it.key()] : it.value() - QPointF(60, 0);   // new process slides in
    }
    from_ = start;
    to_ = target;
    t_ = animMs > 0 ? 0.0 : 1.0;
    if (animMs > 0) { anim_.setDuration(animMs); anim_.start(); }
    update();
}

static void arrow(QPainter& p, QPointF a, QPointF b, const QString& label, bool active, const QColor& col, double bend = 0) {
    QPainterPath path(a);
    QPointF mid = (a + b) / 2 + QPointF(0, bend);
    path.quadTo(mid, b);
    p.setPen(QPen(col, active ? 3.5 : 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    QPointF dir = b - mid;
    double len = std::hypot(dir.x(), dir.y());
    if (len > 0) dir /= len;
    QPointF n(-dir.y(), dir.x());
    QPolygonF head({b, b - dir * 10 + n * 5, b - dir * 10 - n * 5});
    p.setBrush(col);
    p.drawPolygon(head);
    QPointF lp = path.pointAtPercent(0.5) + QPointF(0, bend >= 0 ? -6 : 14);
    QFont f = p.font();
    f.setBold(active);
    p.setFont(f);
    p.drawText(QRectF(lp.x() - 70, lp.y() - 14, 140, 14), Qt::AlignCenter, label);
    f.setBold(false);
    p.setFont(f);
}

void LifecycleView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!cur_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Load a workload"); return; }
    const LcSnapshot& s = *cur_;
    QFont base = font();
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.85);
    p.setFont(small);
    QColor act = ui::semRunning().darker(120), idle = faint(this, 150);
    auto activeEdge = [&](ProcState f, ProcState t) {
        if (s.pid < 0 || s.event == LcEvent::Execute || s.event == LcEvent::Idle || s.event == LcEvent::Create) return false;
        if (two_) {
            bool fr = f == ProcState::Running, tr = t == ProcState::Running;
            bool sfr = s.from == ProcState::Running, str = s.to == ProcState::Running;
            if (t == ProcState::Terminated) return s.to == ProcState::Terminated;
            return fr == sfr && tr == str && sfr != str;
        }
        return s.from == f && s.to == t;
    };
    // arrows
    auto edge = [&](ProcState f, ProcState t, const QString& lab, double bend = 0, double dyA = 0, double dyB = 0) {
        QRectF a = nodeRect(f), b = nodeRect(t);
        QPointF pa, pb;
        if (std::abs(a.center().y() - b.center().y()) < 5) {
            bool right = b.center().x() > a.center().x();
            pa = QPointF(right ? a.right() : a.left(), a.center().y() + dyA);
            pb = QPointF(right ? b.left() : b.right(), b.center().y() + dyB);
        } else {
            pa = QPointF(a.center().x() + dyA, a.center().y() < b.center().y() ? a.bottom() : a.top());
            pb = QPointF(b.center().x() + dyB, a.center().y() < b.center().y() ? b.top() : b.bottom());
        }
        bool on = activeEdge(f, t);
        arrow(p, pa, pb, lab, on, on ? act : idle, bend);
    };
    if (two_) {
        edge(ProcState::Ready, ProcState::Running, "dispatch", -30, -12, -12);
        edge(ProcState::Running, ProcState::Ready, "pause (preempt / block)", 30, 12, 12);
        QRectF r = nodeRect(ProcState::Running), t = nodeRect(ProcState::Terminated);
        bool on = activeEdge(ProcState::Running, ProcState::Terminated);
        arrow(p, QPointF(r.center().x(), r.bottom()), QPointF(t.center().x(), t.top()), "exit", on, on ? act : idle);
    } else {
        edge(ProcState::New, ProcState::Ready, "admit");
        edge(ProcState::Ready, ProcState::Running, "dispatch", -22, -14, -14);
        edge(ProcState::Running, ProcState::Ready, "preempt / quantum", 22, 14, 14);
        edge(ProcState::Running, ProcState::Terminated, "exit");
        edge(ProcState::Running, ProcState::Waiting, "I/O or event wait", 0, 20, 30);
        edge(ProcState::Waiting, ProcState::Ready, "I/O completion", 0, -30, -20);
    }
    // nodes with counts
    QMap<int, int> counts;
    for (const LcPCB& pc : s.pcbs) {
        int key = two_ && pc.state != ProcState::Running && pc.state != ProcState::Terminated ? 99 : (int)pc.state;
        counts[key]++;
    }
    auto node = [&](ProcState st, const QString& title, int key) {
        QRectF r = nodeRect(st);
        QColor c = two_ && key == 99 ? ui::semReady() : lcStateColor(st);
        QColor fill = c;
        fill.setAlpha(45);
        p.setPen(QPen(c, 2));
        p.setBrush(fill);
        p.drawRoundedRect(r, 10, 10);
        p.setPen(ink(this));
        QFont b = small;
        b.setBold(true);
        p.setFont(b);
        p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("%1 (%2)").arg(title).arg(counts.value(key)));
        p.setFont(small);
    };
    if (two_) {
        node(ProcState::Ready, "Not Running", 99);
        node(ProcState::Running, "Running", (int)ProcState::Running);
        node(ProcState::Terminated, "Exited", (int)ProcState::Terminated);
    } else {
        for (ProcState st : {ProcState::New, ProcState::Ready, ProcState::Running, ProcState::Waiting, ProcState::Terminated})
            node(st, stateName(st), (int)st);
    }
    // tokens
    for (const LcPCB& pc : s.pcbs) {
        if (!to_.contains(pc.pid)) continue;
        QPointF a = from_.value(pc.pid, to_[pc.pid]), b = to_[pc.pid];
        QPointF at = a + (b - a) * t_;
        QRectF r(at, QSizeF(kTokW, kTokH));
        p.setPen(QPen(ui::darkInk(), pc.pid == s.pid ? 2.5 : 1));
        p.setBrush(ui::pidColor(pc.pid));
        p.drawRoundedRect(r, 10, 10);
        p.setPen(ui::darkInk());
        p.drawText(r, Qt::AlignCenter, QString("P%1").arg(pc.pid) + (pc.killed ? "x" : ""));
    }
    // CPU and queues
    double y0 = height() * 0.64;
    QRectF cpu(8, y0, 120, height() - y0 - 8);
    p.setPen(QPen(ui::semRunning(), 2));
    p.setBrush(faint(this, 15));
    p.drawRoundedRect(cpu, 8, 8);
    p.setPen(ink(this));
    QFont b = base;
    b.setBold(true);
    p.setFont(b);
    p.drawText(cpu.adjusted(6, 4, -4, 0), Qt::AlignLeft | Qt::AlignTop, "CPU (1 core)");
    p.setFont(base);
    p.drawText(cpu.adjusted(6, 24, -4, 0), Qt::AlignLeft | Qt::AlignTop,
               s.running >= 0 ? QString("running P%1").arg(s.running) : QString("idle"));
    p.drawText(cpu.adjusted(6, 44, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("clock t = %1").arg(s.clock));
    p.drawText(cpu.adjusted(6, 64, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("switches %1").arg(s.contextSwitches));
    auto queue = [&](double y, const QString& title, const std::vector<int>& q, const QColor& c) {
        p.setPen(ink(this));
        p.setFont(b);
        p.drawText(QRectF(140, y, 140, 20), Qt::AlignLeft | Qt::AlignVCenter, title);
        p.setFont(small);
        double x = 280;
        if (q.empty()) { p.setPen(faint(this, 150)); p.drawText(QRectF(x, y, 120, 20), Qt::AlignLeft | Qt::AlignVCenter, "(empty)"); }
        for (size_t i = 0; i < q.size(); ++i) {
            QRectF r(x, y, 40, 20);
            p.setPen(QPen(c, 2));
            p.setBrush(ui::pidColor(q[i]));
            p.drawRect(r);
            p.setPen(ui::darkInk());
            p.drawText(r, Qt::AlignCenter, QString("P%1").arg(q[i]));
            x += 44;
            if (i + 1 < q.size()) { p.setPen(ink(this)); p.drawText(QRectF(x - 4, y, 6, 20), Qt::AlignCenter, ""); }
        }
        p.setPen(faint(this, 160));
        if (!q.empty()) p.drawText(QRectF(x + 2, y, 120, 20), Qt::AlignLeft | Qt::AlignVCenter, q.size() > 1 ? "<- head is left" : "");
    };
    queue(y0 + 4, "Ready queue", s.readyQueue, ui::semReady());
    queue(y0 + 34, "Waiting (I/O)", s.waitQueue, ui::semWaiting());
    std::vector<int> nw;
    for (const LcPCB& pc : s.pcbs) if (pc.state == ProcState::New) nw.push_back(pc.pid);
    queue(y0 + 64, "New (not admitted)", nw, ui::semIdle());
}

// ================================================================ timeline

static const int kLab = 70, kCol = 18, kRow = 22, kHead = 20;

LifecycleTimeline::LifecycleTimeline(QWidget* parent) : QWidget(parent) { setAccessibleName("Process state timeline"); }

void LifecycleTimeline::setTrace(const std::vector<LcSnapshot>* trace, int upto) {
    trace_ = trace;
    upto_ = upto;
    setMinimumSize(sizeHint());
    resize(sizeHint());
    update();
}

QSize LifecycleTimeline::sizeHint() const {
    int ticks = 0, rows = 1;
    if (trace_ && !trace_->empty()) { ticks = trace_->back().clock; rows = (int)trace_->back().pcbs.size(); }
    return QSize(kLab + std::max(30, ticks + 2) * kCol, kHead + std::max(1, rows) * kRow + 30);
}

void LifecycleTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (!trace_ || trace_->empty()) return;
    const LcSnapshot& last = (*trace_)[std::min<int>(upto_, (int)trace_->size() - 1)];
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.82);
    p.setFont(small);
    std::vector<int> pids;
    for (const LcPCB& pc : trace_->back().pcbs) pids.push_back(pc.pid);
    for (size_t r = 0; r < pids.size(); ++r) {
        p.setPen(ink(this));
        p.drawText(QRect(4, kHead + int(r) * kRow, kLab - 8, kRow), Qt::AlignVCenter, QString("P%1").arg(pids[r]));
    }
    // one column per Execute / Idle event (= one tick); states as they were during that tick
    for (int i = 0; i <= upto_ && i < (int)trace_->size(); ++i) {
        const LcSnapshot& s = (*trace_)[i];
        if (s.event != LcEvent::Execute && s.event != LcEvent::Idle) continue;
        int col = s.clock - 1;
        if (col < 0) continue;
        int x = kLab + col * kCol;
        if (col % 5 == 0) { p.setPen(ink(this)); p.drawText(QRect(x - 6, 0, 30, kHead), Qt::AlignLeft | Qt::AlignVCenter, QString::number(col)); }
        for (const LcPCB& pc : s.pcbs) {
            int r = int(std::find(pids.begin(), pids.end(), pc.pid) - pids.begin());
            QRect c(x + 1, kHead + r * kRow + 3, kCol - 2, kRow - 6);
            if (pc.state == ProcState::Terminated) continue;
            QColor col2 = lcStateColor(pc.state);
            if (pc.state != ProcState::Running) col2.setAlpha(110);
            p.fillRect(c, col2);
            p.setPen(ui::darkInk());
            QString ch = pc.state == ProcState::Running ? "R" : pc.state == ProcState::Waiting ? "W" : pc.state == ProcState::Ready ? "r" : "n";
            p.drawText(c, Qt::AlignCenter, ch);
        }
    }
    // legend
    int y = kHead + int(pids.size()) * kRow + 8, x = 4;
    for (ProcState st : {ProcState::New, ProcState::Ready, ProcState::Running, ProcState::Waiting}) {
        p.fillRect(QRect(x, y, 12, 12), lcStateColor(st));
        p.setPen(ink(this));
        QString t = QString(stateName(st)) + (st == ProcState::Running ? " (R)" : st == ProcState::Waiting ? " (W)" : st == ProcState::Ready ? " (r)" : " (n)");
        p.drawText(x + 16, y + 11, t);
        x += 30 + p.fontMetrics().horizontalAdvance(t);
    }
    p.drawText(x + 6, y + 11, QString("logical clock now t = %1").arg(last.clock));
}
