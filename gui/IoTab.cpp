// IoTab.cpp - I/O Management module (M5).
#include "IoTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include "BarChartWidget.h"
#include "IoViews.h"
#include "ModuleKit.h"
#include "Style.h"

static QSpinBox* spin(int lo, int hi, int v, const QString& tip) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    s->setToolTip(tip);
    return s;
}

IoTab::IoTab(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("I/O Management: Polling, Interrupts and DMA",
        "The same transfer (same device schedule, same CPU work) done three ways. Logical ticks, conceptual "
        "model: no real hardware, registers or physical memory are touched.");
    mode_ = new QComboBox;
    for (IoMode m : {IoMode::Polling, IoMode::Interrupt, IoMode::DMA}) mode_->addItem(ioModeName(m));
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const IoPreset& p : ioPresets()) preset_->addItem(QString::fromStdString(p.name));
    units_ = spin(1, 256, 4, "Data units to transfer (N)");
    interval_ = spin(1, 50, 3, "Device: ticks between two ready units (R)");
    buffer_ = spin(1, 64, 8, "Device controller buffer capacity (B); full buffer = device stalls");
    poll_ = spin(1, 50, 2, "Polling: ticks between status checks (P)");
    work_ = spin(0, 100000, 1000, "Useful CPU work available (ticks)");
    copy_ = spin(1, 20, 1, "CPU ticks to copy one unit (C)");
    isr_ = spin(1, 20, 1, "Interrupt service routine ticks (I)");
    setup_ = spin(1, 50, 2, "Ticks to program the DMA controller (S)");
    complete_ = spin(1, 20, 1, "DMA completion ISR ticks (E)");
    rate_ = spin(1, 20, 1, "DMA ticks per unit (D)");
    steal_ = new QCheckBox("DMA cycle stealing (CPU stalls on DMA bus ticks)");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->setSpacing(6);
    cf->addRow("Mode:", mode_);
    cf->addRow("Preset:", preset_);
    cf->addRow(new QLabel("<b>Shared workload</b>"));
    cf->addRow("Data units N:", units_);
    cf->addRow("Device interval R:", interval_);
    cf->addRow("Controller buffer B:", buffer_);
    cf->addRow("CPU work W:", work_);
    cf->addRow(new QLabel("<b>Mode costs (ticks)</b>"));
    cf->addRow("Poll interval P:", poll_);
    cf->addRow("Copy per unit C:", copy_);
    cf->addRow("ISR I:", isr_);
    cf->addRow("DMA setup S:", setup_);
    cf->addRow("DMA completion E:", complete_);
    cf->addRow("DMA per unit D:", rate_);
    cf->addRow(steal_);
    auto* note = new QLabel("Order inside a tick: device produces (and raises IRQ) -> CPU acts (IRQs are taken at the "
                            "tick boundary) -> DMA transfers. One CPU state per tick.");
    note->setWordWrap(true);
    cf->addRow(note);

    diagram_ = new SystemDiagram;
    regs_ = new QTableWidget(0, 2);
    regs_->setHorizontalHeaderLabels({"Register / counter", "Value"});
    regs_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    regs_->setWordWrap(true);
    regs_->verticalHeader()->setVisible(false);
    regs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    code_ = new PseudoCodeView;
    auto* ins = new QSplitter(Qt::Vertical);
    ins->addWidget(regs_);
    ins->addWidget(code_);
    ins->setSizes({280, 150});

    bar_ = new PlaybackBar;
    bottom_ = new QTabWidget;
    timeline_ = new IoTimeline;
    timelineScroll_ = new QScrollArea;
    timelineScroll_->setWidget(timeline_);
    bottom_->addTab(timelineScroll_, "Timeline (CPU, device, bus, DMA)");
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    bottom_->addTab(log_, "Event log");
    cmpLanes_ = new IoTimeline;
    auto* lanesScroll = new QScrollArea;
    lanesScroll->setWidget(cmpLanes_);
    cmpTable_ = new QTableWidget;
    cmpTable_->verticalHeader()->setVisible(false);
    cmpTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cmpChart_ = new BarChartWidget;
    auto* defs = new QLabel(
        "<b>Definitions</b> (logical ticks): <i>latency</i> = tick when the last unit is in RAM and handled; "
        "<i>useful</i>, <i>overhead</i> (poll + copy + ISR + DMA setup/completion), <i>idle/stall</i> are CPU ticks in the "
        "COMMON window (the slowest mode's latency), so all three use the same observation window; throughput = units / latency. "
        "DMA is not always best: its setup can dominate tiny transfers.");
    defs->setWordWrap(true);
    auto* cmpW = new QWidget;
    auto* cl = new QVBoxLayout(cmpW);
    auto* ch = new QHBoxLayout;
    ch->addWidget(cmpTable_, 3);
    ch->addWidget(cmpChart_, 2);
    cl->addWidget(lanesScroll, 2);
    cl->addLayout(ch, 3);
    cl->addWidget(defs);
    auto* cmpScroll = new QScrollArea;
    cmpW->setMinimumHeight(460);
    cmpScroll->setWidget(cmpW);
    cmpScroll->setWidgetResizable(true);
    bottom_->addTab(cmpScroll, "Compare the three modes");

    assembleModule(this, header_, cfgW, titledBox("System (schematic)", diagram_), titledBox("Inspector", ins), bar_, bottom_);

    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) rebuild(); });
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    for (QSpinBox* s : {units_, interval_, buffer_, poll_, work_, copy_, isr_, setup_, complete_, rate_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (loading_) return;
            loading_ = true;
            preset_->setCurrentIndex(0);
            loading_ = false;
            rebuild();
        });
    connect(steal_, &QCheckBox::toggled, this, [this] { if (!loading_) rebuild(); });
    connect(bar_, &PlaybackBar::positionChanged, this, &IoTab::showTick);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });
    loadPreset(0);
}

IoParams IoTab::readParams() const {
    IoParams p;
    p.units = units_->value(); p.deviceInterval = interval_->value(); p.bufferCap = buffer_->value();
    p.pollInterval = poll_->value(); p.cpuWork = work_->value(); p.copyCost = copy_->value(); p.isrCost = isr_->value();
    p.dmaSetup = setup_->value(); p.dmaComplete = complete_->value(); p.dmaRate = rate_->value();
    p.cycleStealing = steal_->isChecked();
    return p;
}

bool IoTab::rebuild() {
    diagram_->setResult(nullptr);
    try {
        result_ = runIo((IoMode)mode_->currentIndex(), readParams());
        compare_ = compareIo(readParams());
    } catch (const std::exception& e) {
        result_.reset();
        compare_.clear();
        bar_->setLength(0);
        header_->setStatus(QString("Invalid parameters: %1").arg(e.what()), ui::semError());
        return false;
    }
    diagram_->setResult(&*result_);
    QStringList lines;
    for (const std::string& s : ioPseudocode(result_->mode)) lines << QString::fromStdString(s);
    code_->setLines(lines);
    log_->clear();
    for (const IoTick& k : result_->ticks)
        for (const std::string& e : k.events) log_->appendPlainText(QString("[t=%1] %2").arg(k.t).arg(QString::fromStdString(e)));
    bar_->setLength((int)result_->ticks.size());
    fillComparison();
    showTick(0, false);
    emit logMessage(QString("I/O: %1, %2 units -> complete at t=%3, useful CPU %4 of %5 ticks")
                        .arg(ioModeName(result_->mode)).arg(result_->params.units).arg(result_->m.completion)
                        .arg(result_->m.useful).arg(result_->m.window));
    return true;
}

void IoTab::showTick(int pos, bool forward) {
    if (!result_) return;
    diagram_->setTick(pos, forward ? bar_->animMs() : 0);
    timeline_->setResults({&*result_}, pos);
    timelineScroll_->horizontalScrollBar()->setValue(std::max(0, pos * 14 - timelineScroll_->viewport()->width() / 2));
    const IoTick* k = pos > 0 ? &result_->ticks[pos - 1] : nullptr;
    code_->setCurrent(k ? k->pseudoLine : -1);
    // counters up to this tick
    IoMetrics m;
    for (int i = 0; i < pos; ++i) {
        switch (result_->ticks[i].cpu) {
            case CpuAct::Useful: ++m.useful; break;
            case CpuAct::Poll: ++m.poll; break;
            case CpuAct::Copy: ++m.copy; break;
            case CpuAct::ISR: ++m.isr; break;
            case CpuAct::DMASetup: ++m.setup; break;
            case CpuAct::DMAComplete: ++m.dmaComplete; break;
            case CpuAct::Idle: ++m.idle; break;
            case CpuAct::BusStall: ++m.stall; break;
        }
    }
    std::vector<std::pair<QString, QString>> rows = {
        {"Logical tick", QString::number(pos)},
        {"CPU state (this tick)", k ? cpuActName(k->cpu) : "-"},
        {"Device status register", k ? (k->buffer > 0 ? "READY" : "BUSY") : "BUSY"},
        {"Controller buffer", QString("%1 / %2").arg(k ? k->buffer : 0).arg(result_->params.bufferCap)},
        {"Units produced", QString("%1 / %2").arg(k ? k->produced : 0).arg(result_->params.units)},
        {"Units in RAM", QString("%1 / %2").arg(k ? k->ram : 0).arg(result_->params.units)},
        {"Pending interrupts", QString::number(k ? k->pending : 0)},
        {"DMA count register", result_->mode == IoMode::DMA ? QString::number(k ? k->dmaLeft : result_->params.units) : "-"},
        {"Bus owner", k ? (k->bus == BusUse::CPU ? "CPU (copy)" : k->bus == BusUse::DMA ? "DMA controller" : "free") : "free"},
        {"Useful CPU ticks so far", QString::number(m.useful)},
        {"Overhead so far (poll/copy/ISR/DMA)", QString("%1 / %2 / %3 / %4").arg(m.poll).arg(m.copy).arg(m.isr).arg(m.setup + m.dmaComplete)},
        {"Idle / stall so far", QString("%1 / %2").arg(m.idle).arg(m.stall)},
    };
    regs_->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); ++r) {
        auto* a = ui::roItem(rows[r].first);
        a->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        regs_->setItem(r, 0, a);
        regs_->setItem(r, 1, ui::roItem(rows[r].second));
    }
    QString st = QString("%1 | tick %2 of %3").arg(ioModeName(result_->mode)).arg(pos).arg(result_->ticks.size());
    if (k) {
        QStringList ev;
        for (const std::string& e : k->events) ev << QString::fromStdString(e);
        st += " | " + ev.join("; ");
    } else {
        st += " | press Run or Next";
    }
    if (pos == (int)result_->ticks.size())
        st += QString(" | DONE: latency %1 ticks, useful %2, overhead %3, polls %4, interrupts %5")
                  .arg(result_->m.completion).arg(result_->m.useful).arg(result_->m.overhead()).arg(result_->m.polls).arg(result_->m.interrupts);
    header_->setStatus(st);
}

void IoTab::fillComparison() {
    std::vector<const IoResult*> rs;
    for (const IoResult& r : compare_) rs.push_back(&r);
    cmpLanes_->setResults(rs, 0);
    QStringList cols = {"Mode", "Latency (ticks)", "Window", "Useful", "Useful %", "Overhead", "Poll", "Copy", "ISR",
                        "DMA setup+done", "Idle", "Stall", "Polls", "Interrupts", "Units", "Units / tick"};
    cmpTable_->setColumnCount(cols.size());
    cmpTable_->setHorizontalHeaderLabels(cols);
    cmpTable_->setRowCount((int)compare_.size());
    QStringList cats;
    std::vector<std::vector<double>> vals(3);
    for (int i = 0; i < (int)compare_.size(); ++i) {
        const IoMetrics& m = compare_[i].m;
        QStringList c = {ioModeName(compare_[i].mode), QString::number(m.completion), QString::number(m.window),
                         QString::number(m.useful), QString::number(m.usefulPct(), 'f', 1), QString::number(m.overhead()),
                         QString::number(m.poll), QString::number(m.copy), QString::number(m.isr),
                         QString::number(m.setup + m.dmaComplete), QString::number(m.idle), QString::number(m.stall),
                         QString::number(m.polls), QString::number(m.interrupts), QString::number(m.transferred),
                         QString::number(m.throughput(), 'f', 3)};
        for (int k = 0; k < c.size(); ++k) cmpTable_->setItem(i, k, ui::roItem(c[k]));
        cats << ioModeName(compare_[i].mode);
        vals[0].push_back(m.useful);
        vals[1].push_back(m.overhead());
        vals[2].push_back(m.completion);
    }
    cmpTable_->resizeColumnsToContents();
    cmpChart_->setData("Same workload, common window (ticks)", cats, {"useful CPU", "CPU overhead", "I/O latency"}, vals,
                       {cpuActColor(CpuAct::Useful), cpuActColor(CpuAct::Copy), ui::semReady()});
}

bool IoTab::loadPreset(int index) {
    std::vector<IoPreset> v = ioPresets();
    if (index < 0 || index >= (int)v.size()) return false;
    bool ok = setParams(v[index].params);
    loading_ = true;
    preset_->setCurrentIndex(index + 1);
    loading_ = false;
    if (ok) header_->setStatus(QString::fromStdString(v[index].description));
    return ok;
}

bool IoTab::setParams(const IoParams& p) {
    loading_ = true;
    units_->setValue(p.units); interval_->setValue(p.deviceInterval); buffer_->setValue(p.bufferCap);
    poll_->setValue(p.pollInterval); work_->setValue(p.cpuWork); copy_->setValue(p.copyCost); isr_->setValue(p.isrCost);
    setup_->setValue(p.dmaSetup); complete_->setValue(p.dmaComplete); rate_->setValue(p.dmaRate);
    steal_->setChecked(p.cycleStealing);
    preset_->setCurrentIndex(0);
    loading_ = false;
    return rebuild();
}

void IoTab::setMode(IoMode m) { mode_->setCurrentIndex((int)m); }
QString IoTab::statusText() const { return header_->status(); }

QString IoTab::reportText() const {
    if (compare_.empty()) return "(invalid parameters)\n";
    const IoParams& p = compare_[0].params;
    QString out = QString("N=%1 R=%2 B=%3 W=%4 P=%5 C=%6 I=%7 S=%8 E=%9 D=%10 cycle stealing=%11\n")
                      .arg(p.units).arg(p.deviceInterval).arg(p.bufferCap).arg(p.cpuWork).arg(p.pollInterval).arg(p.copyCost)
                      .arg(p.isrCost).arg(p.dmaSetup).arg(p.dmaComplete).arg(p.dmaRate).arg(p.cycleStealing ? "on" : "off");
    for (const IoResult& r : compare_)
        out += QString("%1: latency %2, window %3, useful %4 (%5%), overhead %6, idle %7, stall %8, polls %9, interrupts %10\n")
                   .arg(ioModeName(r.mode), -17).arg(r.m.completion).arg(r.m.window).arg(r.m.useful).arg(r.m.usefulPct(), 0, 'f', 1)
                   .arg(r.m.overhead()).arg(r.m.idle).arg(r.m.stall).arg(r.m.polls).arg(r.m.interrupts);
    return out;
}

QString IoTab::csvSection() const {
    QString out = "## I/O management\nmode,latency,window,useful,overhead,poll,copy,isr,dma_setup_done,idle,stall,polls,interrupts,units\n";
    for (const IoResult& r : compare_)
        out += QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14\n").arg(ioModeName(r.mode)).arg(r.m.completion).arg(r.m.window)
                   .arg(r.m.useful).arg(r.m.overhead()).arg(r.m.poll).arg(r.m.copy).arg(r.m.isr).arg(r.m.setup + r.m.dmaComplete)
                   .arg(r.m.idle).arg(r.m.stall).arg(r.m.polls).arg(r.m.interrupts).arg(r.m.transferred);
    return out;
}
