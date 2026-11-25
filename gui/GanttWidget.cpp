// GanttWidget.cpp - draws one coloured box per timeline segment with a time axis.
#include "GanttWidget.h"
#include <QPainter>
#include "Style.h"

static const int kLeft = 20, kRight = 20, kTop = 30, kBarH = 46, kMinTickPx = 22;

GanttWidget::GanttWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(kTop + kBarH + 50);
}

// Stores the timeline and resizes so a wide schedule becomes scrollable.
void GanttWidget::setTimeline(const std::vector<Segment>& tl, int visibleUntil) {
    tl_ = tl;
    visibleUntil_ = visibleUntil;
    int end = tl_.empty() ? 0 : tl_.back().end;
    setMinimumWidth(kLeft + kRight + end * kMinTickPx);
    updateGeometry();
    update();
}

void GanttWidget::setVisibleUntil(int tick) {
    visibleUntil_ = tick;
    update();
}

QSize GanttWidget::sizeHint() const { return QSize(600, kTop + kBarH + 50); }

// Paints the chart: boxes (P<pid> or idle in grey) and boundary tick labels.
void GanttWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    p.setPen(palette().text().color());
    if (!title_.isEmpty()) p.drawText(kLeft, 18, title_);
    if (tl_.empty()) {
        p.drawText(rect(), Qt::AlignCenter, "Run a scheduler to see the Gantt chart");
        return;
    }
    int end = tl_.back().end;
    double px = double(width() - kLeft - kRight) / end;       // pixels per tick
    int limit = visibleUntil_ < 0 ? end : visibleUntil_;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);

    // Outline of the full schedule so the animation shows what is still to come.
    p.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
    p.drawRect(QRectF(kLeft, kTop, end * px, kBarH));

    for (const Segment& s : tl_) {
        if (s.start >= limit) break;
        int segEnd = std::min(s.end, limit);
        QRectF r(kLeft + s.start * px, kTop, (segEnd - s.start) * px, kBarH);
        p.fillRect(r, ui::pidColor(s.pid));
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(r);
        QString label = s.pid < 0 ? "idle" : QString("P%1").arg(s.pid);
        if (p.fontMetrics().horizontalAdvance(label) < r.width() - 2)
            p.drawText(r, Qt::AlignCenter, label);
    }
    // Time axis: label every segment boundary that has been reached.
    p.setFont(small);
    p.setPen(palette().text().color());
    int lastX = -1000;
    auto tickLabel = [&](int t) {
        int x = int(kLeft + t * px);
        p.drawLine(x, kTop + kBarH, x, kTop + kBarH + 5);
        if (x - lastX < 18 && t != end) return;                // avoid overlapping labels
        p.drawText(QRect(x - 20, kTop + kBarH + 6, 40, 16), Qt::AlignHCenter, QString::number(t));
        lastX = x;
    };
    tickLabel(0);
    for (const Segment& s : tl_) if (s.end <= limit) tickLabel(s.end);
    if (visibleUntil_ >= 0 && visibleUntil_ < end) {           // current-time marker
        int x = int(kLeft + limit * px);
        p.setPen(QPen(ui::faultColor(), 2));
        p.drawLine(x, kTop - 6, x, kTop + kBarH + 6);
    }
}
