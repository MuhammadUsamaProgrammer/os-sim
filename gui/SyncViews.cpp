// SyncViews.cpp - painting of the synchronization scene, pseudocode and timeline.
#include "SyncViews.h"
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include "Style.h"
#include "sync.h"

static const double kChipW = 58, kChipH = 24, kGap = 6;
static QColor spinColor() { return QColor("#f28e2b"); }

QString syncShortName(const QString& name) {
    QString digits = name;
    digits.remove(QRegularExpression("[^0-9]"));
    return name.left(1) + digits;
}

static bool isBufferAlgo(const SyncSim* s) {
    return s->config().algo == SyncAlgo::PRODUCER_CONSUMER || s->config().algo == SyncAlgo::MONITOR;
}

// Light background colour that works in the light and the dark theme.
static QColor regionFill(const QWidget* w, int alpha = 24) {
    QColor c = w->palette().text().color();
    c.setAlpha(alpha);
    return c;
}

// Position of chip number k inside a region (rows of chips under the region title).
static QPointF slot(const QRectF& r, int k, double titleH = 22) {
    int cols = std::max(1, int((r.width() - kGap) / (kChipW + kGap)));
    return QPointF(r.left() + kGap + (k % cols) * (kChipW + kGap), r.top() + titleH + (k / cols) * (kChipH + kGap));
}

// ================================================================ SyncView

SyncView::SyncView(QWidget* parent) : QWidget(parent) {
    anim_.setInterval(30);                                     // >= 30 ms: light on a VM
    connect(&anim_, &QTimer::timeout, this, [this] {
        double t = duration_ <= 0 ? 1.0 : std::min(1.0, clock_.elapsed() / double(duration_));
        double e = t * t * (3 - 2 * t);                        // smooth start and stop
        for (size_t i = 0; i < cur_.size() && i < from_.size() && i < to_.size(); ++i)
            cur_[i] = from_[i] + (to_[i] - from_[i]) * e;
        if (t >= 1.0) anim_.stop();
        update();
    });
}

void SyncView::setSim(const SyncSim* sim) {
    sim_ = sim;
    snap();
}

// Places every chip at its target immediately (new simulation or resize).
void SyncView::snap() {
    anim_.stop();
    if (!sim_) { from_.clear(); to_.clear(); cur_.clear(); update(); return; }
    to_ = targets(layout());
    from_ = cur_ = to_;
    update();
}

void SyncView::resizeEvent(QResizeEvent*) { snap(); }

// Starts the glide from the current chip positions to the new targets.
void SyncView::refresh(int animMs) {
    if (!sim_) { update(); return; }
    std::vector<QPointF> t = targets(layout());
    if (cur_.size() != t.size() || animMs <= 0) { to_ = t; from_ = cur_ = t; anim_.stop(); update(); return; }
    from_ = cur_;
    to_ = t;
    duration_ = animMs;
    clock_.start();
    anim_.start();
    update();
}

// Splits the widget into: buffer (top), lanes (middle), object boxes (bottom).
SyncView::Layout SyncView::layout() const {
    Layout l;
    double W = width(), H = height(), y = 6;
    if (isBufferAlgo(sim_)) { l.buffer = QRectF(8, y, W - 16, 56); y += 60; }

    // Bottom row: sync objects and the important shared variables.
    SyncAlgo a = sim_->config().algo;
    if (a == SyncAlgo::RACE || a == SyncAlgo::PETERSON)
        l.boxes.push_back({{}, "counter (shared)", QString::number(sim_->counter()), -1, ui::pidColor(0)});
    if (a == SyncAlgo::PETERSON) {
        for (int i = 0; i < 2; ++i)
            l.boxes.push_back({{}, QString("interested[%1]").arg(i), sim_->interested(i) ? "true" : "false", -1,
                               sim_->interested(i) ? ui::hitColor() : ui::idleColor()});
        l.boxes.push_back({{}, "turn", QString::number(sim_->turn()), -1, ui::pidColor(sim_->turn())});
    }
    const auto& objs = sim_->objects();
    for (int i = 0; i < (int)objs.size(); ++i) {
        const SyncObject& o = objs[i];
        QString v;
        if (o.kind == ObjKind::Semaphore) v = QString("value = %1").arg(o.value);
        else if (o.kind == ObjKind::Condition) v = "condition variable";
        else v = o.owner < 0 ? "free" : "owner " + QString::fromStdString(sim_->procs()[o.owner].name);
        l.boxes.push_back({{}, QString("%1 (%2)").arg(QString::fromStdString(o.name), objKindName(o.kind)), v, i,
                           o.kind == ObjKind::Semaphore ? (o.value > 0 ? ui::hitColor() : ui::faultColor())
                           : o.kind == ObjKind::Condition ? QColor("#b07aa1")
                           : (o.owner < 0 ? ui::hitColor() : ui::faultColor())});
    }
    // Objects with a queue need room for chips: give them 2 shares, plain variables 1.
    double boxH = std::clamp(H * 0.3, 66.0, 92.0), by = H - boxH - 6;
    double shares = 0;
    for (const Box& b : l.boxes) shares += b.obj >= 0 ? 2 : 1;
    double x = 8, unit = shares > 0 ? (W - 16 - kGap * (l.boxes.size() - 1)) / shares : 0;
    for (Box& b : l.boxes) {
        double w = unit * (b.obj >= 0 ? 2 : 1);
        b.rect = QRectF(x, by, w, boxH);
        x += w + kGap;
    }
    if (l.boxes.empty()) by = H;

    // Middle lanes.
    double mh = by - y - 8;
    bool spin = a == SyncAlgo::PETERSON;
    double wReady = (W - 16) * (spin ? 0.27 : 0.32), wSpin = spin ? (W - 16) * 0.2 : 0, wDone = (W - 16) * 0.17;
    double wCs = W - 16 - wReady - wSpin - wDone - (spin ? 3 : 2) * kGap;
    x = 8;
    l.ready = QRectF(x, y, wReady, mh); x += wReady + kGap;
    if (spin) { l.spin = QRectF(x, y, wSpin, mh); x += wSpin + kGap; }
    l.cs = QRectF(x, y, wCs, mh); x += wCs + kGap;
    l.done = QRectF(x, y, wDone, mh);
    return l;
}

// Where each process chip belongs now: queue of the object it sleeps on, critical
// section, busy-wait lane, finished lane or the entry/remainder lane.
std::vector<QPointF> SyncView::targets(const Layout& l) const {
    const auto& procs = sim_->procs();
    std::vector<QPointF> t(procs.size());
    int nReady = 0, nSpin = 0, nCs = 0, nDone = 0;
    for (size_t i = 0; i < procs.size(); ++i) {
        const SyncProc& p = procs[i];
        if (p.state == SyncProcState::Blocked && p.waitingOn >= 0) {
            const auto& q = sim_->objects()[p.waitingOn].queue;
            int pos = int(std::find(q.begin(), q.end(), (int)i) - q.begin());
            for (const Box& b : l.boxes)
                if (b.obj == p.waitingOn) t[i] = slot(b.rect, pos, b.rect.height() - kChipH - 6);
        } else if (p.inCS) t[i] = slot(l.cs, nCs++, 40);
        else if (p.state == SyncProcState::Done) t[i] = slot(l.done, nDone++);
        else if (p.state == SyncProcState::Spinning && !l.spin.isNull()) t[i] = slot(l.spin, nSpin++);
        else t[i] = slot(l.ready, nReady++);
    }
    return t;
}

void SyncView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    QColor ink = palette().text().color();
    if (!sim_) { p.setPen(ink); p.drawText(rect(), Qt::AlignCenter, "Choose an algorithm and press Run or Step"); return; }
    Layout l = layout();
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.88);

    auto lane = [&](const QRectF& r, const QString& title, const QColor& border, double width = 1) {
        p.setPen(QPen(border, width));
        p.setBrush(regionFill(this));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(ink);
        p.setFont(bold);
        p.drawText(r.adjusted(8, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, title);
    };
    lane(l.ready, "Entry / remainder", palette().mid().color());
    if (!l.spin.isNull()) lane(l.spin, "Busy waiting", spinColor());
    lane(l.done, "Finished", palette().mid().color());

    // Critical section: green border, red when more processes are inside than allowed.
    int inside = sim_->inCSCount(), cap = sim_->csCapacity();
    bool violation = inside > cap;
    SyncAlgo a = sim_->config().algo;
    const auto& ps = sim_->procs();
    if (a == SyncAlgo::READERS_WRITERS) {                       // a writer together with anybody else
        bool w = false;
        for (const SyncProc& q : ps) if (q.inCS && q.program == 1) w = true;
        violation = w && inside > 1;
    } else if (a == SyncAlgo::DINING_PHILOSOPHERS) {            // two neighbours eating
        violation = false;
        for (size_t i = 0; i < ps.size(); ++i) if (ps[i].inCS && ps[(i + 1) % ps.size()].inCS && ps.size() > 1) violation = true;
    }
    QString csTitle = a == SyncAlgo::MONITOR ? "Inside the monitor" : a == SyncAlgo::COUNTING_SEM
                      ? QString("Resource in use (max %1)").arg(cap)
                      : a == SyncAlgo::READERS_WRITERS ? "Shared data (many readers OR one writer)"
                      : a == SyncAlgo::DINING_PHILOSOPHERS ? QString("Eating (max %1, never two neighbours)").arg(cap) : "Critical section";
    lane(l.cs, csTitle, violation ? ui::faultColor() : ui::hitColor(), violation ? 3 : 2);
    p.setFont(small);
    QString csInfo;
    if (a == SyncAlgo::COUNTING_SEM) csInfo = QString("%1 of %2 instances in use").arg(inside).arg(cap);
    else if (a == SyncAlgo::READERS_WRITERS) csInfo = "readers share the data; a writer is alone";
    else if (a == SyncAlgo::DINING_PHILOSOPHERS) csInfo = QString("%1 philosopher(s) eating").arg(inside);
    else if (a == SyncAlgo::PRODUCER_CONSUMER || a == SyncAlgo::MONITOR) csInfo = "buffer access (in, out, count)";
    else csInfo = QString("counter = %1").arg(sim_->counter());
    p.setPen(ink);
    p.drawText(l.cs.adjusted(8, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop, csInfo);
    if (violation) {
        p.setPen(ui::faultColor());
        p.setFont(bold);
        p.drawText(l.cs.adjusted(0, 0, -6, -4), Qt::AlignRight | Qt::AlignBottom, "VIOLATION: race!");
    }

    // Bounded buffer: N slots, filled ones show the item, in/out pointers below.
    if (!l.buffer.isNull()) {
        int n = (int)sim_->buffer().size();
        p.setFont(bold);
        p.setPen(ink);
        p.drawText(l.buffer.adjusted(0, 0, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                   QString("Bounded buffer  N = %1   count = %2").arg(n).arg(sim_->bufferCount()));
        double sw = std::min(48.0, (l.buffer.width() - 40) / std::max(1, n));
        double sx = l.buffer.left() + 4, sy = l.buffer.top() + 18;
        p.setFont(small);
        for (int i = 0; i < n; ++i) {
            QRectF r(sx + i * sw, sy, sw - 4, 24);
            int item = sim_->buffer()[i];
            p.setPen(QPen(palette().mid().color(), 1));
            p.setBrush(item >= 0 ? ui::lightGreen() : palette().base().color());
            p.drawRect(r);
            p.setPen(item >= 0 ? ui::darkInk() : palette().mid().color());
            p.drawText(r, Qt::AlignCenter, item >= 0 ? QString::number(item) : "_");
            QString ptr = QString(i == sim_->bufferIn() ? "in" : "") +
                          (i == sim_->bufferIn() && i == sim_->bufferOut() ? "/" : "") +
                          (i == sim_->bufferOut() ? "out" : "");
            if (!ptr.isEmpty()) { p.setPen(ink); p.drawText(QRectF(r.left() - 6, r.bottom(), r.width() + 12, 14), Qt::AlignCenter, ptr); }
        }
    }

    // Bottom boxes: objects (value + waiting queue) and shared variables.
    for (const Box& b : l.boxes) {
        p.setPen(QPen(b.accent, 2));
        p.setBrush(regionFill(this, 14));
        p.drawRoundedRect(b.rect, 6, 6);
        p.setPen(ink);
        p.setFont(bold);
        p.drawText(b.rect.adjusted(8, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, b.title);
        p.setFont(small);
        p.drawText(b.rect.adjusted(8, 20, -4, 0), Qt::AlignLeft | Qt::AlignTop, b.value);
        if (b.obj >= 0 && sim_->objects()[b.obj].queue.empty()) {     // chips are drawn here when not empty
            p.setPen(palette().mid().color());
            p.drawText(QRectF(b.rect.left() + 8, b.rect.bottom() - kChipH - 6, b.rect.width() - 12, kChipH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       sim_->objects()[b.obj].kind == ObjKind::Monitor ? "entry queue: empty" : "waiting queue: empty");
        }
    }

    // Process chips (animated positions).
    p.setFont(bold);
    const auto& procs = sim_->procs();
    for (size_t i = 0; i < procs.size() && i < cur_.size(); ++i) {
        const SyncProc& pr = procs[i];
        QRectF r(cur_[i], QSizeF(kChipW, kChipH));
        QColor c = ui::pidColor((int)i);
        if (pr.state == SyncProcState::Done) c = c.lighter(140);
        p.setBrush(c);
        QPen pen(ui::darkInk(), pr.state == SyncProcState::Running ? 2.5 : 1);
        if (pr.state == SyncProcState::Spinning) pen = QPen(spinColor(), 2.5, Qt::DashLine);
        if (pr.state == SyncProcState::Blocked) pen = QPen(ui::darkInk(), 1, Qt::DotLine);
        p.setPen(pen);
        p.drawRoundedRect(r, 11, 11);
        p.setPen(ui::darkInk());
        p.drawText(r, Qt::AlignCenter, QString::fromStdString(pr.name));
        // owner mark: a small key dot on mutex / monitor owners
        for (const SyncObject& o : sim_->objects())
            if (o.owner == (int)i) {
                p.setBrush(QColor("#edc948"));
                p.setPen(QPen(ui::darkInk(), 1));
                p.drawEllipse(QPointF(r.right() - 2, r.top() + 2), 5, 5);
            }
    }
}

// ================================================================ CodeView

static const int kLineH = 19, kGutter = 92;

CodeView::CodeView(QWidget* parent) : QWidget(parent) {}

void CodeView::setSim(const SyncSim* sim) {
    sim_ = sim;
    updateGeometry();
    update();
}

QSize CodeView::sizeHint() const {
    int lines = 1;
    if (sim_) for (const auto& pr : sim_->programs()) lines += (int)pr.size() + 2;
    return QSize(460, lines * kLineH + 8);
}

// Draws every program; the line just executed is highlighted in the colour of its
// process, and a chip in the left gutter marks the NEXT line of every process.
void CodeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    QColor ink = palette().text().color();
    if (!sim_) return;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFont bold = font();
    bold.setBold(true);
    QFont tiny = font();
    tiny.setPointSizeF(tiny.pointSizeF() * 0.8);
    tiny.setBold(true);
    const SyncStepRecord* last = sim_->history().empty() ? nullptr : &sim_->history().back();
    int y = 4;
    for (int g = 0; g < (int)sim_->programs().size(); ++g) {
        p.setFont(bold);
        p.setPen(ink);
        p.drawText(QRect(6, y, width() - 12, kLineH), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromStdString(sim_->programTitles()[g]));
        y += kLineH;
        const auto& prog = sim_->programs()[g];
        for (int i = 0; i < (int)prog.size(); ++i) {
            QRect row(0, y, width(), kLineH);
            if (last && last->program == g && last->line == i) {
                QColor c = ui::pidColor(last->pid);
                c.setAlpha(110);
                p.fillRect(row, c);
            }
            if (prog[i].enterCS || prog[i].leaveCS) {        // mark the critical-section boundaries
                p.fillRect(QRect(kGutter - 4, y, 3, kLineH), ui::hitColor());
            }
            // Gutter chips: processes whose next line is this one.
            int gx = 4, shown = 0, more = 0;
            for (const SyncProc& pr : sim_->procs()) {
                if (pr.program != g || pr.pc != i || pr.state == SyncProcState::Done) continue;
                if (shown >= 3) { ++more; continue; }
                QRectF chip(gx, y + 2, 26, kLineH - 4);
                p.setBrush(ui::pidColor(pr.id));
                QPen pen(ui::darkInk(), 1);
                if (pr.state == SyncProcState::Blocked) pen.setStyle(Qt::DotLine);
                if (pr.state == SyncProcState::Spinning) pen = QPen(spinColor(), 2);
                p.setPen(pen);
                p.drawRoundedRect(chip, 4, 4);
                p.setFont(tiny);
                p.setPen(ui::darkInk());
                p.drawText(chip, Qt::AlignCenter, syncShortName(QString::fromStdString(pr.name)));
                gx += 28;
                ++shown;
            }
            if (more) { p.setPen(ink); p.setFont(tiny); p.drawText(QRect(gx, y, 24, kLineH), Qt::AlignVCenter, QString("+%1").arg(more)); }
            p.setFont(mono);
            p.setPen(palette().mid().color());
            p.drawText(QRect(kGutter, y, 24, kLineH), Qt::AlignVCenter | Qt::AlignRight, QString::number(i + 1));
            p.setPen(ink);
            p.drawText(QRect(kGutter + 32, y, width() - kGutter - 36, kLineH), Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromStdString(prog[i].code));
            y += kLineH;
        }
        y += kLineH / 2;
    }
}

// ================================================================ SyncTimeline

static const int kLabelW = 72, kCellW = 14, kRowH = 20, kHeadH = 18;

SyncTimeline::SyncTimeline(QWidget* parent) : QWidget(parent) {}

void SyncTimeline::setSim(const SyncSim* sim) {
    sim_ = sim;
    QSize s = sizeHint();                  // exact size: grows and shrinks with the history
    setMinimumSize(s);
    resize(s);
    update();
}

QSize SyncTimeline::sizeHint() const {
    int steps = sim_ ? (int)sim_->history().size() : 0;
    int rows = sim_ ? (int)sim_->procs().size() : 1;
    return QSize(kLabelW + std::max(40, steps + 4) * kCellW, kHeadH + rows * kRowH + 30);
}

void SyncTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    QColor ink = palette().text().color();
    if (!sim_) return;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.82);
    p.setFont(small);
    const auto& hist = sim_->history();
    const auto& procs = sim_->procs();
    for (size_t r = 0; r < procs.size(); ++r) {
        int y = kHeadH + int(r) * kRowH;
        p.setPen(ink);
        p.drawText(QRect(4, y, kLabelW - 8, kRowH), Qt::AlignVCenter, QString::fromStdString(procs[r].name));
        p.setPen(QPen(regionFill(this, 40), 1));
        p.drawLine(kLabelW, y + kRowH, width(), y + kRowH);
    }
    for (size_t s = 0; s < hist.size(); ++s) {
        int x = kLabelW + int(s) * kCellW;
        if ((s + 1) % 5 == 0) {
            p.setPen(ink);
            p.drawText(QRect(x - 10, 0, kCellW + 20, kHeadH), Qt::AlignCenter, QString::number(s + 1));
        }
        for (size_t r = 0; r < hist[s].cells.size(); ++r) {
            QRect c(x + 1, kHeadH + int(r) * kRowH + 3, kCellW - 2, kRowH - 6);
            QColor pc = ui::pidColor(int(r));
            switch (hist[s].cells[r]) {
                case SyncCell::Run: pc.setAlpha(120); p.fillRect(c, pc); break;
                case SyncCell::RunCS:
                    p.fillRect(c, pc);
                    p.setPen(QPen(ui::darkInk(), 1));
                    p.drawRect(c.adjusted(0, 0, -1, -1));
                    break;
                case SyncCell::Spin: {                       // busy wait: zigzag = spinning in the loop
                    pc.setAlpha(70);
                    p.fillRect(c, pc);
                    p.setPen(QPen(ui::darkInk(), 1.2));
                    QPolygon zig;
                    for (int k = 0; k <= 4; ++k) zig << QPoint(c.left() + k * c.width() / 4, k % 2 ? c.top() + 2 : c.bottom() - 2);
                    p.drawPolyline(zig);
                    break;
                }
                case SyncCell::Blocked: p.fillRect(c, QBrush(ui::idleColor(), Qt::BDiagPattern)); break;
                default: break;
            }
        }
    }
    // Legend
    int ly = kHeadH + int(procs.size()) * kRowH + 8, lx = 4;
    auto item = [&](const QBrush& b, bool border, const QString& t) {
        QRect c(lx, ly, 12, 12);
        p.fillRect(c, b);
        if (border) { p.setPen(QPen(ui::darkInk(), 1)); p.drawRect(c.adjusted(0, 0, -1, -1)); }
        p.setPen(ink);
        p.drawText(lx + 16, ly + 11, t);
        lx += 22 + p.fontMetrics().horizontalAdvance(t) + 10;
    };
    QColor faint = ui::pidColor(0);
    faint.setAlpha(120);
    item(faint, false, "ran a line");
    item(ui::pidColor(0), true, "ran inside the critical section");
    {   // zigzag legend for busy waiting
        QRect c(lx, ly, 12, 12);
        p.setPen(QPen(ui::darkInk(), 1.2));
        p.drawPolyline(QPolygon({QPoint(lx, ly + 10), QPoint(lx + 3, ly + 2), QPoint(lx + 6, ly + 10), QPoint(lx + 9, ly + 2), QPoint(lx + 12, ly + 10)}));
        p.setPen(ink);
        QString t = "busy wait (spinning)";
        p.drawText(lx + 16, ly + 11, t);
        lx += 22 + p.fontMetrics().horizontalAdvance(t) + 10;
    }
    item(QBrush(ui::idleColor(), Qt::BDiagPattern), false, "blocked (asleep)");
}
