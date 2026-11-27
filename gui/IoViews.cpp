// IoViews.cpp - system diagram with animated bus routes, and the lane timeline.
#include "IoViews.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include "ModuleKit.h"
#include "Style.h"

QColor cpuActColor(CpuAct a) {
    switch (a) {
        case CpuAct::Useful: return ui::semRunning();
        case CpuAct::Poll: return QColor("#edc948");
        case CpuAct::Copy: return ui::semWaiting();
        case CpuAct::ISR: return ui::semError();
        case CpuAct::DMASetup: return QColor("#76b7b2");
        case CpuAct::DMAComplete: return QColor("#d37295");
        case CpuAct::Idle: return ui::semIdle();
        case CpuAct::BusStall: return QColor("#9c755f");
    }
    return ui::semIdle();
}

static QColor ink(const QWidget* w) { return w->palette().text().color(); }
static QColor faint(const QWidget* w, int a) { QColor c = ink(w); c.setAlpha(a); return c; }

SystemDiagram::SystemDiagram(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    anim_.setEasingCurve(QEasingCurve::InOutQuad);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t_ = v.toDouble(); update(); });
    setAccessibleName("System diagram: CPU, RAM, bus, device, DMA and interrupt controllers");
}

void SystemDiagram::setResult(const IoResult* r) { anim_.stop(); r_ = r; shown_ = 0; t_ = 1; update(); }

void SystemDiagram::setTick(int shownTicks, int animMs) {
    anim_.stop();
    shown_ = shownTicks;
    t_ = 1;
    if (animMs > 0 && shownTicks > 0) { t_ = 0; anim_.setDuration(animMs); anim_.start(); }
    update();
}

void SystemDiagram::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Choose valid parameters"); return; }
    const double W = width(), H = height();
    const IoParams& prm = r_->params;
    const IoTick* k = shown_ > 0 ? &r_->ticks[shown_ - 1] : nullptr;
    QRectF cpu(W * 0.03, H * 0.06, W * 0.22, H * 0.28), pic(W * 0.36, H * 0.06, W * 0.2, H * 0.2),
        ram(W * 0.66, H * 0.04, W * 0.31, H * 0.34), dev(W * 0.02, H * 0.68, W * 0.13, H * 0.26),
        ctl(W * 0.19, H * 0.62, W * 0.3, H * 0.33), dmaR(W * 0.6, H * 0.62, W * 0.3, H * 0.33);
    double busY = H * 0.5;
    QFont base = font(), small = font(), bold = font();
    small.setPointSizeF(base.pointSizeF() * 0.85);
    bold.setBold(true);

    // bus
    p.setPen(QPen(faint(this, 120), 8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(W * 0.03, busY), QPointF(W * 0.97, busY));
    p.setPen(ink(this));
    p.setFont(small);
    p.drawText(QRectF(W * 0.03, busY + 6, 200, 16), Qt::AlignLeft, "system bus (address + data)");
    auto stub = [&](const QRectF& r, bool top) { return QPointF(r.center().x(), top ? r.bottom() : r.top()); };
    for (auto pr : {std::make_pair(cpu, true), std::make_pair(ram, true), std::make_pair(ctl, false), std::make_pair(dmaR, false)}) {
        p.setPen(QPen(faint(this, 110), 4));
        QPointF a = stub(pr.first, pr.second);
        p.drawLine(a, QPointF(a.x(), busY));
    }
    // IRQ lines (dashed): controller -> PIC, DMA -> PIC, PIC -> CPU
    QPen irqPen(faint(this, 140), 1.5, Qt::DashLine);
    p.setPen(irqPen);
    p.drawLine(QPointF(ctl.right() - 10, ctl.top()), QPointF(pic.left() + 10, pic.bottom()));
    p.drawLine(QPointF(dmaR.left() + 10, dmaR.top()), QPointF(pic.right() - 10, pic.bottom()));
    p.drawLine(QPointF(pic.left(), pic.center().y()), QPointF(cpu.right(), pic.center().y()));

    // active route and travelling token for the current tick
    auto viaBus = [&](QPointF a, QPointF b) { return std::vector<QPointF>{a, {a.x(), busY}, {b.x(), busY}, b}; };
    std::vector<QPointF> route;
    QString token;
    QColor tokCol = ui::semReady();
    if (k) {
        if (k->cpu == CpuAct::Poll) { route = viaBus(stub(cpu, true), stub(ctl, false)); token = "status?"; tokCol = cpuActColor(CpuAct::Poll); }
        else if (k->cpu == CpuAct::Copy) {
            auto a = viaBus(stub(ctl, false), stub(cpu, true)), b = viaBus(stub(cpu, true), stub(ram, true));
            route = a;
            route.insert(route.end(), b.begin() + 1, b.end());
            token = QString("unit %1").arg(k->unitMoving);
            tokCol = cpuActColor(CpuAct::Copy);
        } else if (k->cpu == CpuAct::DMASetup) { route = viaBus(stub(cpu, true), stub(dmaR, false)); token = QString("count=%1").arg(prm.units); tokCol = cpuActColor(CpuAct::DMASetup); }
        else if (k->dmaBusy) {
            route = viaBus(stub(ctl, false), stub(ram, true));
            token = QString("DMA unit %1").arg(k->unitMoving);
            tokCol = QColor("#76b7b2");
        }
        if (k->irqRaised && route.empty()) {
            QPointF src = r_->mode == IoMode::DMA ? QPointF(dmaR.left() + 10, dmaR.top()) : QPointF(ctl.right() - 10, ctl.top());
            route = {src, QPointF(src.x() < pic.center().x() ? pic.left() + 10 : pic.right() - 10, pic.bottom()),
                     QPointF(pic.left(), pic.center().y()), QPointF(cpu.right(), pic.center().y())};
            token = "IRQ";
            tokCol = ui::semError();
        } else if ((k->cpu == CpuAct::ISR || k->cpu == CpuAct::DMAComplete) && route.empty()) {
            route = {QPointF(pic.left(), pic.center().y()), QPointF(cpu.right(), pic.center().y())};
            token = "ack";
            tokCol = ui::semError();
        }
    }
    if (route.size() >= 2) {
        QPainterPath path(route[0]);
        for (size_t i = 1; i < route.size(); ++i) path.lineTo(route[i]);
        QColor c = tokCol;
        c.setAlpha(170);
        p.setPen(QPen(c, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        QPointF at = path.pointAtPercent(std::clamp(t_, 0.0, 1.0));
        QRectF tr(at.x() - 34, at.y() - 11, 68, 22);
        p.setPen(QPen(ui::darkInk(), 1));
        p.setBrush(tokCol);
        p.drawRoundedRect(tr, 8, 8);
        p.setFont(small);
        p.setPen(ui::darkInk());
        p.drawText(tr, Qt::AlignCenter, token);
    }

    auto block = [&](const QRectF& r, const QString& title, const QColor& accent, bool active) {
        QColor fill = accent;
        fill.setAlpha(active ? 70 : 28);
        p.setPen(QPen(accent, active ? 3 : 1.5));
        p.setBrush(fill);
        p.drawRoundedRect(r, 8, 8);
        p.setPen(ink(this));
        p.setFont(bold);
        p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, title);
        p.setFont(small);
    };
    // CPU
    CpuAct act = k ? k->cpu : CpuAct::Idle;
    block(cpu, "CPU", cpuActColor(act), k != nullptr);
    p.setFont(bold);
    p.drawText(cpu.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop, k ? QString(cpuActName(act)) : QString("(t = 0)"));
    p.setFont(small);
    p.drawText(cpu.adjusted(6, 42, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("logical tick %1").arg(shown_));
    // PIC
    int pending = k ? k->pending : 0;
    block(pic, "Interrupt ctrl", ui::semError(), k && (k->irqRaised || k->cpu == CpuAct::ISR || k->cpu == CpuAct::DMAComplete));
    p.drawText(pic.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop,
               QString("pending IRQ: %1%2").arg(pending).arg(k && k->irqRaised ? "  (raised now)" : ""));
    // RAM with unit slots
    block(ram, "RAM (destination buffer)", ui::semDone(), k && (k->cpu == CpuAct::Copy || k->dmaBusy));
    int inRam = k ? k->ram : 0;
    int n = prm.units, cols = std::min(n, 16), rows = (n + cols - 1) / cols;
    double sw = (ram.width() - 12) / cols, sh = std::min(18.0, (ram.height() - 28) / std::max(1, rows));
    for (int i = 0; i < n; ++i) {
        QRectF s(ram.left() + 6 + (i % cols) * sw, ram.top() + 24 + (i / cols) * sh, sw - 2, sh - 2);
        bool filled = i < inRam;
        p.setPen(QPen(faint(this, 120), 1));
        p.setBrush(filled ? ui::lightGreen() : palette().base().color());
        p.drawRect(s);
        if (filled && sw > 14) { p.setPen(ui::darkInk()); p.drawText(s, Qt::AlignCenter, QString::number(r_->ramData[i])); }
    }
    // device and controller
    block(dev, "Device", ui::semReady(), k && k->dev == DevState::Producing && k->produced < prm.units);
    p.drawText(dev.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               !k ? "starting" : k->dev == DevState::Done ? "all units\nproduced" : k->dev == DevState::Stalled ? "STALLED\n(buffer full)"
                                                                                                   : QString("producing\n%1/%2").arg(k->produced).arg(prm.units));
    int buf = k ? k->buffer : 0;
    block(ctl, "Device controller", ui::semReady(), k && k->cpu == CpuAct::Poll);
    p.drawText(ctl.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop,
               QString("STATUS register: %1").arg(buf > 0 ? "READY" : "BUSY"));
    p.drawText(ctl.adjusted(6, 40, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("buffer %1 / %2 units").arg(buf).arg(prm.bufferCap));
    int bcols = std::min(prm.bufferCap, 16);
    double bw = (ctl.width() - 12) / bcols;
    for (int i = 0; i < prm.bufferCap && i < 32; ++i) {
        QRectF s(ctl.left() + 6 + (i % bcols) * bw, ctl.top() + 60 + (i / bcols) * 16, bw - 2, 14);
        p.setPen(QPen(faint(this, 120), 1));
        p.setBrush(i < buf ? ui::semReady().lighter(150) : palette().base().color());
        p.drawRect(s);
    }
    // DMA controller
    bool dmaMode = r_->mode == IoMode::DMA;
    block(dmaR, dmaMode ? "DMA controller" : "DMA controller (unused)", QColor("#76b7b2"), k && (k->dmaBusy || k->cpu == CpuAct::DMASetup));
    if (dmaMode) {
        p.drawText(dmaR.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop, "src: device ctrl   dst: RAM buffer");
        p.drawText(dmaR.adjusted(6, 40, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                   QString("count register: %1   %2").arg(k ? k->dmaLeft : prm.units).arg(k && k->dmaBusy ? "BUSY (owns the bus)" : "idle"));
    } else {
        p.setPen(faint(this, 150));
        p.drawText(dmaR.adjusted(6, 22, -4, 0), Qt::AlignLeft | Qt::AlignTop, QString("not used in %1 mode").arg(ioModeName(r_->mode)));
    }
    p.setPen(ink(this));
    p.setFont(small);
    p.drawText(QRectF(W * 0.36, H * 0.28, W * 0.28, 32), Qt::AlignLeft | Qt::TextWordWrap,
               k ? QString("bus owner: %1").arg(k->bus == BusUse::CPU ? "CPU" : k->bus == BusUse::DMA ? "DMA" : "free") : QString());
}

// ================================================================ IoTimeline

static const int kLab = 132, kCol = 14, kRow = 20, kHead = 18;

IoTimeline::IoTimeline(QWidget* parent) : QWidget(parent) { setAccessibleName("I/O timeline lanes"); }

void IoTimeline::setResults(const std::vector<const IoResult*>& rs, int upto) {
    rs_ = rs;
    upto_ = upto;
    setMinimumSize(sizeHint());
    resize(sizeHint());
    update();
}

QSize IoTimeline::sizeHint() const {
    int ticks = 0;
    for (const IoResult* r : rs_) ticks = std::max(ticks, (int)r->ticks.size());
    int lanes = rs_.size() == 1 ? 4 : (int)rs_.size() * 2;
    return QSize(kLab + std::max(40, ticks + 2) * kCol, kHead + lanes * kRow + 44);
}

void IoTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (rs_.empty()) return;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p.setFont(small);
    int ticks = 0;
    for (const IoResult* r : rs_) ticks = std::max(ticks, (int)r->ticks.size());
    for (int t = 0; t <= ticks; t += 5) {
        p.setPen(faint(this, 50));
        p.drawLine(kLab + t * kCol, kHead, kLab + t * kCol, height() - 40);
        p.setPen(ink(this));
        p.drawText(QRect(kLab + t * kCol - 2, 0, 30, kHead), Qt::AlignLeft | Qt::AlignVCenter, QString::number(t));
    }
    int lane = 0;
    auto label = [&](const QString& s) {
        p.setPen(ink(this));
        p.drawText(QRect(4, kHead + lane * kRow, kLab - 8, kRow), Qt::AlignVCenter, s);
    };
    auto cell = [&](int t, const QColor& c, const QString& ch) {
        QRect r(kLab + t * kCol + 1, kHead + lane * kRow + 2, kCol - 2, kRow - 4);
        p.fillRect(r, c);
        if (!ch.isEmpty()) { p.setPen(ui::darkInk()); p.drawText(r, Qt::AlignCenter, ch); }
    };
    for (const IoResult* r : rs_) {
        int lim = rs_.size() == 1 ? upto_ : (int)r->ticks.size();
        QString pre = rs_.size() == 1 ? QString() : QString(ioModeName(r->mode)) + " ";
        label(pre + "CPU");
        for (int t = 0; t < lim && t < (int)r->ticks.size(); ++t) {
            CpuAct a = r->ticks[t].cpu;
            QString ch = a == CpuAct::Useful ? "U" : a == CpuAct::Poll ? "P" : a == CpuAct::Copy ? "C" : a == CpuAct::ISR ? "I"
                       : a == CpuAct::DMASetup ? "S" : a == CpuAct::DMAComplete ? "D" : a == CpuAct::BusStall ? "B" : "";
            cell(t, cpuActColor(a), ch);
        }
        if (r->m.completion > 0 && r->m.completion <= lim) {
            p.setPen(QPen(ui::semError(), 2));
            int x = kLab + r->m.completion * kCol;
            p.drawLine(x, kHead + lane * kRow, x, kHead + (lane + 1) * kRow);
        }
        ++lane;
        if (rs_.size() == 1) {
            label("Device");
            for (int t = 0; t < lim; ++t) {
                const IoTick& k = r->ticks[t];
                QColor c = k.dev == DevState::Stalled ? ui::semWaiting() : k.dev == DevState::Done ? palette().base().color() : ui::semReady().lighter(150);
                cell(t, c, k.irqRaised && r->mode != IoMode::DMA ? "!" : (k.events.size() && k.events[0].rfind("device: unit", 0) == 0 ? "+" : ""));
            }
            ++lane;
        }
        label(pre + (rs_.size() == 1 ? "Bus" : "bus"));
        for (int t = 0; t < lim && t < (int)r->ticks.size(); ++t) {
            const IoTick& k = r->ticks[t];
            if (k.bus == BusUse::CPU) cell(t, cpuActColor(CpuAct::Copy), "c");
            else if (k.bus == BusUse::DMA) cell(t, QColor("#76b7b2"), "d");
        }
        ++lane;
        if (rs_.size() == 1) {
            label("DMA / IRQ");
            for (int t = 0; t < lim; ++t) {
                const IoTick& k = r->ticks[t];
                if (k.dmaBusy) cell(t, QColor("#76b7b2"), QString::number(k.unitMoving % 10));
                else if (k.irqRaised) cell(t, ui::semError(), "!");
            }
            ++lane;
        }
    }
    // legend
    int y = kHead + lane * kRow + 10, x = 4;
    for (CpuAct a : {CpuAct::Useful, CpuAct::Poll, CpuAct::Copy, CpuAct::ISR, CpuAct::DMASetup, CpuAct::DMAComplete, CpuAct::Idle, CpuAct::BusStall}) {
        p.fillRect(QRect(x, y, 12, 12), cpuActColor(a));
        p.setPen(ink(this));
        p.drawText(x + 15, y + 11, cpuActName(a));
        x += 26 + p.fontMetrics().horizontalAdvance(cpuActName(a));
    }
    p.drawText(x + 4, y + 11, "| red bar = I/O complete, ! = interrupt request, + = unit ready");
}
