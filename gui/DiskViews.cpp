// DiskViews.cpp - platter and seek-graph painting.
#include "DiskViews.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include "ModuleKit.h"
#include "Style.h"

static QColor ink(const QWidget* w) { return w->palette().text().color(); }
static QColor faint(const QWidget* w, int a = 40) { QColor c = ink(w); c.setAlpha(a); return c; }

// Is request id serviced within the first `step` moves?
static bool servicedBy(const DiskResult* r, int id, int step) {
    for (int i = 0; i < step && i < (int)r->moves.size(); ++i)
        if (r->moves[i].requestId == id) return true;
    return false;
}

// ================================================================ PlatterWidget

PlatterWidget::PlatterWidget(QWidget* parent) : QWidget(parent) {
    anim_.setEasingCurve(QEasingCurve::InOutQuad);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        headCyl_ = v.toDouble();
        update();
    });
    setAccessibleName("Disk platter and head");
}

void PlatterWidget::setResult(const DiskResult* r) {
    anim_.stop();
    r_ = r;
    step_ = 0;
    headCyl_ = r ? r->cfg.head : 0;
    update();
}

void PlatterWidget::setStep(int step, int animMs) {
    if (!r_) return;
    anim_.stop();
    step_ = step;
    double target = step <= 0 ? r_->cfg.head : r_->moves[step - 1].to;
    double from = step <= 0 ? r_->cfg.head : r_->moves[step - 1].from;
    if (animMs > 0 && step > 0) {
        anim_.setStartValue(from);
        anim_.setEndValue(target);
        anim_.setDuration(animMs);
        anim_.start();
    } else {
        headCyl_ = target;
    }
    update();
}

void PlatterWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Enter a valid request queue"); return; }
    const DiskConfig& c = r_->cfg;
    double R = std::min(width() * 0.42, height() * 0.44);
    QPointF ctr(width() * 0.44, height() * 0.5);
    double rIn = R * 0.18;
    auto radius = [&](double cyl) { return rIn + (cyl - c.minCyl) / double(c.maxCyl - c.minCyl) * (R - rIn); };

    // platter and tracks
    p.setPen(QPen(faint(this, 90), 1));
    p.setBrush(faint(this, 18));
    p.drawEllipse(ctr, R, R);
    p.setBrush(Qt::NoBrush);
    for (int k = 1; k <= 10; ++k) {
        double cyl = c.minCyl + (c.maxCyl - c.minCyl) * k / 10.0;
        p.setPen(QPen(faint(this, 45), 1));
        p.drawEllipse(ctr, radius(cyl), radius(cyl));
    }
    p.setBrush(faint(this, 60));
    p.drawEllipse(ctr, rIn * 0.7, rIn * 0.7);             // spindle

    // requests: one marker per request, spread over the angle so duplicates stay visible
    int n = (int)c.requests.size();
    int targetId = step_ > 0 ? r_->moves[step_ - 1].requestId : -1;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p.setFont(small);
    for (int id = 0; id < n; ++id) {
        double ang = (200.0 - 330.0 * (id + 0.5) / std::max(1, n)) * M_PI / 180.0;   // avoid the arm side
        double rr = radius(c.requests[id]);
        QPointF pt(ctr.x() + rr * std::cos(ang), ctr.y() - rr * std::sin(ang));
        bool done = servicedBy(r_, id, step_);
        QColor col = id == targetId ? ui::semRunning() : done ? ui::semDone() : ui::semReady();
        p.setPen(QPen(ui::darkInk(), 1));
        p.setBrush(col);
        double sz = id == targetId ? 7 : 5;
        p.drawEllipse(pt, sz, sz);
        p.setPen(ink(this));
        p.drawText(QRectF(pt.x() - 30, pt.y() - 20, 60, 14), Qt::AlignCenter, QString::number(c.requests[id]));
    }
    // current track ring
    double hr = radius(headCyl_);
    p.setPen(QPen(ui::semRunning(), 2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ctr, hr, hr);
    // arm from a pivot on the right to the head position on the horizontal radius
    QPointF headPt(ctr.x() + hr, ctr.y());
    QPointF pivot(ctr.x() + R + std::min(40.0, width() * 0.08), ctr.y() + R * 0.75);
    p.setPen(QPen(faint(this, 200), 5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(pivot, headPt);
    p.setPen(QPen(ui::darkInk(), 1));
    p.setBrush(ui::semRunning());
    p.drawRect(QRectF(headPt.x() - 6, headPt.y() - 5, 12, 10));
    p.setBrush(faint(this, 200));
    p.drawEllipse(pivot, 8, 8);
    p.setFont(font());
    p.setPen(ink(this));
    p.drawText(QRectF(4, 4, width() - 8, 18), Qt::AlignLeft,
               QString("Head at cylinder %1   (tracks %2..%3, outer = higher)")
                   .arg(int(std::lround(headCyl_))).arg(c.minCyl).arg(c.maxCyl));
    // legend
    int y = height() - 18, x = 6;
    auto leg = [&](const QColor& col, const QString& t) {
        p.setPen(QPen(ui::darkInk(), 1));
        p.setBrush(col);
        p.drawEllipse(QPointF(x + 5, y + 6), 5, 5);
        p.setPen(ink(this));
        p.drawText(x + 14, y + 11, t);
        x += 22 + p.fontMetrics().horizontalAdvance(t);
    };
    p.setFont(small);
    leg(ui::semReady(), "pending");
    leg(ui::semRunning(), "being serviced");
    leg(ui::semDone(), "serviced");
}

// ================================================================ SeekGraph

SeekGraph::SeekGraph(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    anim_.setEasingCurve(QEasingCurve::InOutQuad);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        progress_ = v.toDouble();
        update();
    });
    setAccessibleName("Cylinder versus step graph");
}

void SeekGraph::setResult(const DiskResult* r) {
    anim_.stop();
    r_ = r;
    step_ = 0;
    progress_ = 1;
    update();
}

void SeekGraph::setStep(int step, int animMs) {
    anim_.stop();
    step_ = step;
    progress_ = 1;
    if (animMs > 0 && step > 0) {
        anim_.setDuration(animMs);
        anim_.start();
    }
    update();
}

void SeekGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_) return;
    const DiskConfig& c = r_->cfg;
    const int L = 40, Rm = 16, T = 34, B = 26;
    int n = (int)r_->moves.size();
    double W = width() - L - Rm, H = height() - T - B;
    auto X = [&](double cyl) { return L + (cyl - c.minCyl) / double(c.maxCyl - c.minCyl) * W; };
    auto Y = [&](double k) { return T + (n == 0 ? 0 : k / n * H); };
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p.setFont(small);
    // axes: cylinders on top, steps down the left side
    p.setPen(faint(this, 60));
    for (int k = 0; k <= 10; ++k) {
        double cyl = c.minCyl + (c.maxCyl - c.minCyl) * k / 10.0;
        p.drawLine(QPointF(X(cyl), T), QPointF(X(cyl), T + H));
    }
    p.setPen(ink(this));
    for (int k = 0; k <= 10; k += 2) {
        int cyl = int(std::lround(c.minCyl + (c.maxCyl - c.minCyl) * k / 10.0));
        p.drawText(QRectF(X(cyl) - 20, T - 16, 40, 14), Qt::AlignCenter, QString::number(cyl));
    }
    p.drawText(QRectF(0, 2, width(), 14), Qt::AlignHCenter,
               QString("%1: cylinder (x) vs service step (y)").arg(diskAlgoName(r_->algo)));
    for (int k = 0; k <= n; ++k)
        if (n <= 20 || k % 5 == 0) p.drawText(QRectF(2, Y(k) - 7, L - 8, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(k));
    // request ticks along the top axis
    for (int v : c.requests) { p.setPen(QPen(ui::semReady(), 2)); p.drawLine(QPointF(X(v), T - 3), QPointF(X(v), T + 3)); }

    // path
    QPointF prev(X(c.head), Y(0));
    for (int k = 0; k < step_ && k < n; ++k) {
        const DiskMove& m = r_->moves[k];
        QPointF to(X(m.to), Y(k + 1));
        bool current = k == step_ - 1;
        QPointF end = current ? prev + (to - prev) * progress_ : to;
        QPen pen(ui::semReady(), 2);
        if (m.kind == MoveKind::Boundary) pen = QPen(ui::semIdle(), 2, Qt::DashLine);
        if (m.kind == MoveKind::Wrap) pen = QPen(ui::semError(), 2, Qt::DashDotLine);
        if (current) pen.setColor(m.kind == MoveKind::Service ? ui::semRunning() : pen.color());
        p.setPen(pen);
        p.drawLine(prev, end);
        if (!current || progress_ >= 1.0) {
            p.setPen(QPen(ui::darkInk(), 1));
            p.setBrush(m.requestId >= 0 ? (current ? ui::semRunning() : ui::semDone()) : ui::semIdle());
            p.drawEllipse(to, current ? 6 : 4, current ? 6 : 4);
            p.setPen(ink(this));
            QString lab = m.requestId >= 0 ? QString("%1").arg(m.to) : QString("%1 (%2)").arg(m.to).arg(m.kind == MoveKind::Wrap ? "wrap" : "end");
            p.drawText(QPointF(to.x() + 7, to.y() + 4), lab);
        }
        prev = to;
    }
    p.setPen(QPen(ui::darkInk(), 1));
    p.setBrush(ui::semWaiting());
    p.drawEllipse(QPointF(X(c.head), Y(0)), 5, 5);
    p.setPen(ink(this));
    p.drawText(QPointF(X(c.head) - 60, Y(0) + 12), QString("start %1").arg(c.head));
    // legend
    int y = height() - 16, x = L;
    auto leg = [&](const QPen& pen, const QString& t) {
        p.setPen(pen);
        p.drawLine(x, y, x + 22, y);
        p.setPen(ink(this));
        p.drawText(x + 26, y + 4, t);
        x += 40 + p.fontMetrics().horizontalAdvance(t);
    };
    leg(QPen(ui::semReady(), 2), "service move");
    leg(QPen(ui::semIdle(), 2, Qt::DashLine), "boundary transit");
    leg(QPen(ui::semError(), 2, Qt::DashDotLine), "wrap (counted)");
}
