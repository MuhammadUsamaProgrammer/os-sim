// SysMonTab.cpp - Processes > System Monitor (see SysMonTab.h).
#include "SysMonTab.h"
#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include "ModuleKit.h"
#include "NativeRunner.h"
#include "Style.h"
#include "sysmon_native.h"
#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Table cell that sorts by a number (stored in Qt::UserRole) instead of its text.
class NumItem : public QTableWidgetItem {
public:
    NumItem(const QString& text, double v) : QTableWidgetItem(text) { setData(Qt::UserRole, v); setTextAlignment(Qt::AlignRight | Qt::AlignVCenter); }
    bool operator<(const QTableWidgetItem& o) const override { return data(Qt::UserRole).toDouble() < o.data(Qt::UserRole).toDouble(); }
};
QSpinBox* spin(int lo, int hi, int v, const QString& suffix, const QString& tip) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    s->setSuffix(suffix);
    s->setToolTip(tip);
    return s;
}
}

// ======================================================================= sampler (worker thread)

void SysMonSampler::sample() { emit sampled(model_.update(sampleSystem())); }
void SysMonSampler::resetModel() { model_.reset(); }

// ======================================================================= chart

HistoryChart::HistoryChart(QWidget* parent) : QWidget(parent) {}

void HistoryChart::setSeries(const MetricHistory* cpu, const MetricHistory* mem, double cpuThr, double memThr, double intervalSec) {
    cpu_ = cpu; mem_ = mem; cpuThr_ = cpuThr; memThr_ = memThr; interval_ = intervalSec;
    update();
}

void HistoryChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::WindowText);
    int coresW = cores_.empty() ? 0 : std::min(width() / 3, 14 + (int)cores_.size() * 12);
    QRectF plot(36, 18, width() - 44 - coresW, height() - 40);
    p.setPen(ink);
    p.drawText(QRectF(0, 0, width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
               QString("Live history: CPU %  (blue) and memory used %  (orange), newest on the right, one point per sample (%1 s)").arg(interval_, 0, 'f', 1));
    p.setPen(QColor("#c0c4c8"));
    for (int v = 0; v <= 100; v += 25) {
        double y = plot.bottom() - plot.height() * v / 100;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(ink);
        p.drawText(QRectF(0, y - 8, 32, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(v));
        p.setPen(QColor("#c0c4c8"));
    }
    auto line = [&](const MetricHistory* h, const QColor& c, double thr) {
        if (!h) return;
        p.setPen(QPen(c, 1, Qt::DashLine));
        double ty = plot.bottom() - plot.height() * thr / 100;
        p.drawLine(QPointF(plot.left(), ty), QPointF(plot.right(), ty));
        const auto& v = h->values();
        if (v.size() < 2) return;
        QPolygonF poly;
        int cap = h->capacity();
        for (size_t i = 0; i < v.size(); ++i) {
            double x = plot.right() - plot.width() * double(v.size() - 1 - i) / (cap - 1);
            poly << QPointF(x, plot.bottom() - plot.height() * std::max(0.0, std::min(100.0, v[i])) / 100);
        }
        p.setPen(QPen(c, 2));
        p.drawPolyline(poly);
    };
    line(cpu_, QColor("#1a73e8"), cpuThr_);
    line(mem_, QColor("#e8710a"), memThr_);
    p.setPen(ink);
    p.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width(), 16), Qt::AlignCenter, "older  <-  samples  ->  now   (dashed = alert thresholds)");
    if (!cores_.empty()) {
        QRectF cr(width() - coresW - 2, 18, coresW, height() - 40);
        double bw = (cr.width() - 10) / cores_.size();
        for (size_t i = 0; i < cores_.size(); ++i) {
            double v = cores_[i];
            QRectF bar(cr.left() + 8 + i * bw, cr.top(), bw - 2, cr.height());
            p.setPen(QColor("#c0c4c8"));
            p.setBrush(Qt::NoBrush);
            p.drawRect(bar);
            if (v >= 0) {
                double h = bar.height() * v / 100;
                p.fillRect(QRectF(bar.left(), bar.bottom() - h, bar.width(), h), QColor("#1a73e8"));
            }
        }
        p.setPen(ink);
        p.drawText(QRectF(cr.left(), cr.bottom() + 2, cr.width(), 16), Qt::AlignCenter, "per core");
    }
}

// ======================================================================= page

bool SystemMonitorPage::canSuspend() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

SystemMonitorPage::SystemMonitorPage(QWidget* parent) : QWidget(parent) {
    qRegisterMetaType<SystemSnapshot>();
    header_ = new ModuleHeader("System Monitor",
        QString("LIVE data of this computer (%1 backend), not a simulation: CPU, memory, disk I/O and processes, sampled on a "
                "background thread. The host process table is read-only; only demo workers started here can be controlled.")
            .arg(sysmonBackendName()));
    header_->setBadge(QString("Live System (%1)").arg(sysmonBackendName()), ui::semWaiting());

    // ---- configuration
    interval_ = spin(250, 10000, 1000, " ms", "Sampling interval: every sample reads the backend on the worker thread");
    interval_->setSingleStep(250);
    history_ = spin(10, 600, 120, " samples", "Bounded history: older samples are dropped");
    start_ = new QPushButton("Start");
    pause_ = new QPushButton("Pause");
    stop_ = new QPushButton("Stop (reset)");
    stop_->setToolTip("Stops sampling and clears history; the next start warms up again");
    filter_ = new QLineEdit;
    filter_->setPlaceholderText("filter: name or PID");
    onlyMine_ = new QCheckBox("Only my processes");
    onlyMine_->setToolTip("Processes with the same real user id as OS-Sim (Linux)");
    cpuThr_ = spin(10, 100, 90, " %", "Informational CPU alert threshold");
    memThr_ = spin(10, 100, 90, " %", "Informational memory alert threshold");
    debounce_ = spin(1, 30, 3, " samples", "Debounce: the value must stay above the threshold this many consecutive samples");
    wSeconds_ = spin(1, 120, 30, " s", "Lifetime limit of the demo worker");
    wMib_ = spin(0, 256, 32, " MiB", "Memory the worker touches (becomes resident)");
    wBusy_ = spin(0, 100, 40, " %", "CPU use of the worker (share of one core)");
    wStart_ = new QPushButton("Start demo worker");
    wSuspend_ = new QPushButton("Suspend (SIGSTOP)");
    wResume_ = new QPushButton("Resume (SIGCONT)");
    wStop_ = new QPushButton("Stop (SIGTERM)");
    wKill_ = new QPushButton("Force kill...");
    wKill_->setToolTip("Forceful termination of the OWN demo worker - asks for confirmation");
    if (!canSuspend()) { wSuspend_->setToolTip("Not supported on this platform"); wResume_->setToolTip("Not supported on this platform"); }
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    auto* runRow = new QWidget;
    auto* rg = new QGridLayout(runRow);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->addWidget(start_, 0, 0);
    rg->addWidget(pause_, 0, 1);
    rg->addWidget(stop_, 1, 0, 1, 2);
    cf->addRow(new QLabel("<b>Live sampling</b>"));
    cf->addRow("Interval:", interval_);
    cf->addRow("History:", history_);
    cf->addRow(runRow);
    cf->addRow("Filter:", filter_);
    cf->addRow(onlyMine_);
    cf->addRow(new QLabel("<b>Alerts (informational)</b>"));
    cf->addRow("CPU above:", cpuThr_);
    cf->addRow("Memory above:", memThr_);
    cf->addRow("For:", debounce_);
    auto* wl = new QLabel("<b>Owned demo worker</b> (a bounded ossim_helper child; the only process this page can control)");
    wl->setWordWrap(true);
    cf->addRow(wl);
    cf->addRow("Lifetime:", wSeconds_);
    cf->addRow("Memory:", wMib_);
    cf->addRow("CPU:", wBusy_);
    auto* wb = new QWidget;
    auto* wg = new QGridLayout(wb);
    wg->setContentsMargins(0, 0, 0, 0);
    wg->addWidget(wStart_, 0, 0, 1, 2);
    wg->addWidget(wSuspend_, 1, 0);
    wg->addWidget(wResume_, 1, 1);
    wg->addWidget(wStop_, 2, 0);
    wg->addWidget(wKill_, 2, 1);
    cf->addRow(wb);

    // ---- centre: summary strip + chart / table + tree
    cpuLbl_ = new QLabel; memLbl_ = new QLabel; availLbl_ = new QLabel; procLbl_ = new QLabel; diskLbl_ = new QLabel; srcLbl_ = new QLabel;
    for (QLabel* l : {cpuLbl_, memLbl_, availLbl_, procLbl_, diskLbl_, srcLbl_}) { l->setWordWrap(true); l->setFrameShape(QFrame::StyledPanel); l->setMargin(4); }
    auto* strip = new QWidget;
    auto* sg = new QGridLayout(strip);
    sg->setContentsMargins(0, 0, 0, 0);
    sg->addWidget(cpuLbl_, 0, 0);
    sg->addWidget(memLbl_, 0, 1);
    sg->addWidget(availLbl_, 0, 2);
    sg->addWidget(procLbl_, 1, 0);
    sg->addWidget(diskLbl_, 1, 1);
    sg->addWidget(srcLbl_, 1, 2);
    chart_ = new HistoryChart;
    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({"PID", "PPID", "Name", "State", "CPU % (1 core)", "Resident (RSS)", "Threads"});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSortingEnabled(true);
    table_->sortByColumn(4, Qt::DescendingOrder);
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Process tree", "PID", "CPU %", "RSS"});
    auto* views = new QTabWidget;
    views->addTab(table_, "Process table");
    views->addTab(tree_, "Process tree");
    auto* top = new QWidget;
    auto* tl = new QVBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(strip);
    tl->addWidget(chart_, 1);
    auto* center = new QSplitter(Qt::Vertical);
    center->addWidget(top);
    center->addWidget(views);
    center->setStretchFactor(0, 2);
    center->setStretchFactor(1, 3);

    // ---- inspector
    inspect_ = new QLabel("Select a process.");
    inspect_->setWordWrap(true);
    inspect_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* notes = new QLabel(
        "<b>How to read the numbers</b><br>"
        "<b>Total CPU %</b>: busy share of all logical CPUs between two samples.<br>"
        "<b>Process CPU %</b>: share of ONE logical core (CPU ticks / elapsed time); a multithreaded process can exceed 100 %. "
        "Divide by the number of logical CPUs for the share of the whole machine.<br>"
        "<b>RSS</b>: pages of the process in RAM, including shared pages (libraries, shared memory). It is not exclusive "
        "memory: the RSS values of all processes cannot be added up to the used memory.<br>"
        "<b>Used memory</b> = total - available (the kernel's estimate of memory that can be given to programs).<br>"
        "Rates and percentages need two samples: the first sample only warms up. Command lines are not collected.");
    notes->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>Selected process</b>"));
    il->addWidget(inspect_);
    il->addWidget(notes);
    il->addStretch();

    // ---- bottom: alerts, worker log
    auto* bottom = new QTabWidget;
    alertTable_ = new QTableWidget(0, 4);
    alertTable_->setHorizontalHeaderLabels({"Time", "Sample", "Alert", "Details"});
    alertTable_->verticalHeader()->setVisible(false);
    alertTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alertTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    alertTable_->horizontalHeader()->setStretchLastSection(true);
    bottom->addTab(alertTable_, "Alert log");
    workerLog_ = new QPlainTextEdit;
    workerLog_->setReadOnly(true);
    bottom->addTab(workerLog_, "Demo worker log");

    assembleModule(this, header_, cfgW, center, titledBox("Inspector", insW), nullptr, bottom);

    // ---- sampling thread
    thread_ = new QThread(this);
    thread_->setObjectName("SysMonSampler");
    sampler_ = new SysMonSampler;
    sampler_->moveToThread(thread_);
    connect(thread_, &QThread::finished, sampler_, &QObject::deleteLater);
    connect(sampler_, &SysMonSampler::sampled, this, &SystemMonitorPage::onSnapshot, Qt::QueuedConnection);
    thread_->start();
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &SystemMonitorPage::sampleOnce);

    connect(start_, &QPushButton::clicked, this, [this] { userPaused_ = false; startMonitoring(); });
    connect(pause_, &QPushButton::clicked, this, [this] { userPaused_ = true; pauseMonitoring(); });
    connect(stop_, &QPushButton::clicked, this, [this] { userPaused_ = true; stopMonitoring(); });
    connect(interval_, &QSpinBox::valueChanged, this, [this](int ms) { setIntervalMs(ms); });
    connect(history_, &QSpinBox::valueChanged, this, [this](int n) { setHistoryCapacity(n); });
    connect(filter_, &QLineEdit::textChanged, this, [this] { fillTable(); fillTree(); });
    connect(onlyMine_, &QCheckBox::toggled, this, [this] { fillTable(); fillTree(); });
    for (QSpinBox* s : {cpuThr_, memThr_, debounce_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            cpuAlert_.configure(cpuThr_->value(), debounce_->value());
            memAlert_.configure(memThr_->value(), debounce_->value());
        });
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int) {
        if (auto* it = table_->item(r, 0)) { selectedPid_ = it->data(Qt::UserRole).toInt(); showInspector(); }
    });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { selectedPid_ = it->text(1).toInt(); showInspector(); });
    connect(wStart_, &QPushButton::clicked, this, [this] { startWorker(wSeconds_->value(), wMib_->value(), wBusy_->value()); });
    connect(wSuspend_, &QPushButton::clicked, this, [this] { suspendWorker(); });
    connect(wResume_, &QPushButton::clicked, this, [this] { resumeWorker(); });
    connect(wStop_, &QPushButton::clicked, this, [this] { stopWorker(); });
    connect(wKill_, &QPushButton::clicked, this, [this] {
        bool ok = ui::selfTestMode() ||
                  QMessageBox::question(this, "OS-Sim", QString("Forcefully kill the demo worker (PID %1)?\nIt cannot clean up; this cannot be undone.")
                                                            .arg(workerPid_)) == QMessageBox::Yes;
        killWorker(ok);
    });
    updateWorkerButtons();
    header_->setStatus("Stopped. Sampling starts when the page is shown (or press Start).");
}

SystemMonitorPage::~SystemMonitorPage() {
    timer_->stop();
    thread_->quit();
    thread_->wait(3000);
    if (worker_ && worker_->state() != QProcess::NotRunning) {
        worker_->disconnect(this);
        worker_->terminate();
        if (!worker_->waitForFinished(1500)) { worker_->kill(); worker_->waitForFinished(1000); }
    }
}

void SystemMonitorPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!userPaused_ && !timer_->isActive()) startMonitoring();
}

void SystemMonitorPage::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    if (timer_->isActive()) timer_->stop();        // no background sampling while another page is shown
}

void SystemMonitorPage::startMonitoring() {
    timer_->start(interval_->value());
    sampleOnce();
    header_->setStatus(QString("Monitoring every %1 ms (live data)").arg(interval_->value()), ui::hitColor().darker(130));
}

void SystemMonitorPage::pauseMonitoring() {
    timer_->stop();
    header_->setStatus("Paused: the last sample stays visible; Start continues (rates use the time since the last sample).", ui::semWaiting());
}

void SystemMonitorPage::stopMonitoring() {
    timer_->stop();
    QMetaObject::invokeMethod(sampler_, "resetModel", Qt::QueuedConnection);
    cpuHist_.clear();
    memHist_.clear();
    cpuAlert_.reset();
    memAlert_.reset();
    chart_->setSeries(&cpuHist_, &memHist_, cpuThr_->value(), memThr_->value(), interval_->value() / 1000.0);
    header_->setStatus("Stopped: history cleared; the next sample warms up again.");
}

bool SystemMonitorPage::monitoring() const { return timer_->isActive(); }

void SystemMonitorPage::setIntervalMs(int ms) {
    if (interval_->value() != ms) interval_->setValue(ms);
    if (timer_->isActive()) timer_->start(ms);
}

void SystemMonitorPage::setHistoryCapacity(int n) {
    if (history_->value() != n) history_->setValue(n);
    cpuHist_.setCapacity(n);
    memHist_.setCapacity(n);
    chart_->update();
}

void SystemMonitorPage::sampleOnce() {
    if (inFlight_) return;                         // never queue a backlog of samples
    inFlight_ = true;
    QMetaObject::invokeMethod(sampler_, "sample", Qt::QueuedConnection);
}

void SystemMonitorPage::setFilter(const QString& t) { filter_->setText(t); }

int SystemMonitorPage::visibleRows() const { return table_->rowCount(); }

void SystemMonitorPage::onSnapshot(const SystemSnapshot& s) {
    inFlight_ = false;
    last_ = s;
    ++received_;
    if (s.cpuValid) cpuHist_.push(s.cpuPercent);
    if (s.memValid) memHist_.push(s.memPercent);
    AlertEvent ev;
    if (s.cpuValid && cpuAlert_.feed(s.seq, s.cpuPercent, ev)) addAlert(ev);
    if (s.memValid && memAlert_.feed(s.seq, s.memPercent, ev)) addAlert(ev);
    QString warm = s.warmingUp ? "warming up (needs a second sample)" : QString();
    cpuLbl_->setText(QString("<b>CPU</b><br>%1").arg(s.cpuValid ? QString("%1 % of %2 logical CPUs").arg(s.cpuPercent, 0, 'f', 1).arg(s.logicalCpus)
                                                                : (warm.isEmpty() ? QString("unavailable") : warm)));
    memLbl_->setText(QString("<b>Memory used</b><br>%1").arg(s.memValid ? QString("%1 of %2 (%3 %)").arg(q(formatKb(s.memUsedKb)), q(formatKb(s.memTotalKb)))
                                                                              .arg(s.memPercent, 0, 'f', 1) : QString("unavailable")));
    availLbl_->setText(QString("<b>Available</b><br>%1").arg(s.memValid ? q(formatKb(s.memAvailableKb)) : QString("unavailable")));
    procLbl_->setText(QString("<b>Processes</b><br>%1").arg(s.processCount));
    diskLbl_->setText(QString("<b>Disk I/O</b><br>%1").arg(s.diskValid ? QString("read %1/s, write %2/s").arg(q(formatKb((long long)(s.diskReadBps / 1024))),
                                                                                                             q(formatKb((long long)(s.diskWriteBps / 1024))))
                                                                     : (s.warmingUp ? warm : QString("not available"))));
    srcLbl_->setText(QString("<b>Source</b><br>%1 via %2, interval %3 s").arg(q(s.platform), q(s.source)).arg(s.intervalSec, 0, 'f', 2));
    chart_->setSeries(&cpuHist_, &memHist_, cpuThr_->value(), memThr_->value(), interval_->value() / 1000.0);
    chart_->setCores(s.corePercent);
    fillTable();
    fillTree();
    showInspector();
    updateWorkerButtons();
}

static bool passes(const ProcView& p, const QString& f, bool onlyMine) {
#ifndef _WIN32
    if (onlyMine && p.uid != (int)getuid()) return false;
#else
    (void)onlyMine;
#endif
    if (f.isEmpty()) return true;
    return QString::fromStdString(p.name).contains(f, Qt::CaseInsensitive) || QString::number(p.pid) == f;
}

void SystemMonitorPage::fillTable() {
    QString f = filter_->text().trimmed();
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    int r = 0;
    for (const ProcView& p : last_.procs) {
        if (!passes(p, f, onlyMine_->isChecked())) continue;
        table_->insertRow(r);
        auto* pid = new NumItem(QString::number(p.pid), p.pid);
        pid->setData(Qt::UserRole, p.pid);
        table_->setItem(r, 0, pid);
        table_->setItem(r, 1, new NumItem(QString::number(p.ppid), p.ppid));
        table_->setItem(r, 2, new QTableWidgetItem(q(p.name) + (p.pid == workerPid_ && workerRunning() ? "  [OS-Sim demo worker]" : "")));
        table_->setItem(r, 3, new QTableWidgetItem(q(p.state)));
        table_->setItem(r, 4, new NumItem(p.cpuValid ? QString::number(p.cpuPercent, 'f', 1) : (p.accessible ? "warming up" : "n/a"), p.cpuValid ? p.cpuPercent : -1));
        table_->setItem(r, 5, new NumItem(q(formatKb(p.rssKb)), (double)p.rssKb));
        table_->setItem(r, 6, new NumItem(p.threads >= 0 ? QString::number(p.threads) : "n/a", p.threads));
        if (p.pid == workerPid_ && workerRunning())
            for (int k = 0; k < 7; ++k) ui::shade(table_->item(r, k), ui::lightGreen());
        if (p.pid == selectedPid_) table_->selectRow(r);
        ++r;
    }
    table_->setSortingEnabled(true);
}

void SystemMonitorPage::fillTree() {
    QString f = filter_->text().trimmed();
    tree_->clear();
    std::map<int, const ProcView*> byPid;
    std::map<int, std::vector<const ProcView*>> kids;
    for (const ProcView& p : last_.procs) byPid[p.pid] = &p;
    for (const ProcView& p : last_.procs) kids[byPid.count(p.ppid) && p.ppid != p.pid ? p.ppid : 0].push_back(&p);
    std::function<bool(const ProcView*)> anyMatch = [&](const ProcView* p) {
        if (passes(*p, f, onlyMine_->isChecked())) return true;
        for (const ProcView* c : kids[p->pid]) if (anyMatch(c)) return true;
        return false;
    };
    std::function<void(QTreeWidgetItem*, const ProcView*, int)> add = [&](QTreeWidgetItem* parent, const ProcView* p, int depth) {
        if (depth > 64 || !anyMatch(p)) return;
        QStringList cols = {q(p->name), QString::number(p->pid), p->cpuValid ? QString::number(p->cpuPercent, 'f', 1) : "-", q(formatKb(p->rssKb))};
        auto* it = parent ? new QTreeWidgetItem(parent, cols) : new QTreeWidgetItem(tree_, cols);
        if (p->pid == workerPid_ && workerRunning()) it->setText(0, it->text(0) + "  [OS-Sim demo worker]");
        for (const ProcView* c : kids[p->pid]) add(it, c, depth + 1);
    };
    for (const ProcView* p : kids[0]) add(nullptr, p, 0);
    tree_->expandToDepth(f.isEmpty() ? 1 : 64);
    tree_->resizeColumnToContents(0);
}

void SystemMonitorPage::showInspector() {
    const ProcView* sel = nullptr;
    for (const ProcView& p : last_.procs) if (p.pid == selectedPid_) sel = &p;
    if (!sel) { inspect_->setText(selectedPid_ < 0 ? "Select a process in the table or tree." : QString("PID %1 has exited (no longer in the latest sample).").arg(selectedPid_)); return; }
    QString cpu = sel->cpuValid ? QString("%1 % of one core = %2 % of the machine (%3 logical CPUs)").arg(sel->cpuPercent, 0, 'f', 1)
                                      .arg(sel->cpuPercent / std::max(1, last_.logicalCpus), 0, 'f', 1).arg(last_.logicalCpus)
                                : QString("not yet defined (new process / PID reuse / warming up)");
    inspect_->setText(QString("<b>%1</b> (PID %2, parent %3)<br>state: %4<br>CPU: %5<br>resident: %6 (shared pages included)<br>threads: %7<br>"
                              "user id: %8<br>start time (clock ticks since boot): %9%10%11")
                          .arg(q(sel->name).toHtmlEscaped()).arg(sel->pid).arg(sel->ppid).arg(q(sel->state)).arg(cpu).arg(q(formatKb(sel->rssKb)))
                          .arg(sel->threads >= 0 ? QString::number(sel->threads) : "n/a").arg(sel->uid >= 0 ? QString::number(sel->uid) : "n/a")
                          .arg(sel->startTime).arg(sel->accessible ? QString() : "<br><i>details not readable (permissions)</i>")
                          .arg(sel->pid == workerPid_ && workerRunning() ? "<br><b>This is OS-Sim's own demo worker (controllable).</b>" : "<br>Read-only: OS-Sim never signals host processes."));
}

void SystemMonitorPage::addAlert(const AlertEvent& e) {
    ++alerts_;
    int r = alertTable_->rowCount();
    alertTable_->insertRow(r);
    QStringList c = {QDateTime::currentDateTime().toString("HH:mm:ss"), QString::number(e.seq), e.raised ? "raised" : "cleared", q(e.text)};
    for (int k = 0; k < 4; ++k) alertTable_->setItem(r, k, ui::roItem(c[k]));
    ui::shade(alertTable_->item(r, 2), e.raised ? ui::lightRed() : ui::lightGreen());
    alertTable_->scrollToBottom();
    emit logMessage("System Monitor alert: " + q(e.text));
}

// ---------------------------------------------------------------- owned demo worker

bool SystemMonitorPage::workerRunning() const { return worker_ && worker_->state() == QProcess::Running; }
qint64 SystemMonitorPage::workerPid() const { return workerRunning() ? workerPid_ : 0; }

void SystemMonitorPage::updateWorkerButtons() {
    bool run = workerRunning();
    wStart_->setEnabled(!run && NativeRunner::available());
    wSuspend_->setEnabled(run && canSuspend());
    wResume_->setEnabled(run && canSuspend());
    wStop_->setEnabled(run);
    wKill_->setEnabled(run);
}

void SystemMonitorPage::applyDemoConfig(int seconds, int mib, int busy, int cpuThr, int memThr, int debounce) {
    wSeconds_->setValue(seconds);
    wMib_->setValue(mib);
    wBusy_->setValue(busy);
    cpuThr_->setValue(cpuThr);
    memThr_->setValue(memThr);
    debounce_->setValue(debounce);
    header_->setStatus(QString("Demo worker prepared: %1 s, %2 MiB, %3 % CPU - press Start worker to run it "
                               "(an application-owned helper process)").arg(seconds).arg(mib).arg(busy));
}

bool SystemMonitorPage::startWorker(int seconds, int mib, int busy) {
    if (workerRunning() || NativeRunner::helperPath().isEmpty()) return false;
    if (!worker_) {
        worker_ = new QProcess(this);
        worker_->setProcessChannelMode(QProcess::MergedChannels);
        connect(worker_, &QProcess::readyRead, this, [this] {
            while (worker_->canReadLine()) {
                QString l = QString::fromUtf8(worker_->readLine()).trimmed();
                int bar = l.indexOf(" | ");
                workerLog_->appendPlainText(bar > 0 ? l.mid(l.indexOf("EVENT=") + 6).replace(" | ", ": ") : l);
            }
        });
        connect(worker_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
            workerLog_->appendPlainText(QString("worker PID %1 ended: %2 %3").arg(workerPid_).arg(st == QProcess::CrashExit ? "killed / crashed," : "exit code").arg(code));
            emit logMessage(QString("System Monitor: demo worker %1 ended").arg(workerPid_));
            updateWorkerButtons();
        });
    }
    worker_->start(NativeRunner::helperPath(), {"worker", QString::number(seconds), QString::number(mib), QString::number(busy)});
    if (!worker_->waitForStarted(3000)) { workerLog_->appendPlainText("could not start the worker: " + worker_->errorString()); return false; }
    workerPid_ = worker_->processId();
    workerLog_->appendPlainText(QString("started OS-Sim demo worker PID %1 (%2 s, %3 MiB, %4 % CPU)").arg(workerPid_).arg(seconds).arg(mib).arg(busy));
    emit logMessage(QString("System Monitor: started demo worker PID %1").arg(workerPid_));
    updateWorkerButtons();
    return true;
}

// Signals are sent ONLY to the worker this page started and that is still running (QProcess has
// not reaped it, so its PID cannot have been reused).
static bool signalOwned(QProcess* p, qint64 pid, int sig) {
#ifndef _WIN32
    if (!p || p->state() != QProcess::Running || p->processId() != pid || pid <= 0) return false;
    return ::kill((pid_t)pid, sig) == 0;
#else
    (void)p; (void)pid; (void)sig;
    return false;
#endif
}

bool SystemMonitorPage::suspendWorker() {
#ifndef _WIN32
    bool ok = signalOwned(worker_, workerPid_, SIGSTOP);
    if (ok) workerLog_->appendPlainText(QString("SIGSTOP sent to own worker %1: state should become 'stopped'").arg(workerPid_));
    return ok;
#else
    return false;
#endif
}

bool SystemMonitorPage::resumeWorker() {
#ifndef _WIN32
    bool ok = signalOwned(worker_, workerPid_, SIGCONT);
    if (ok) workerLog_->appendPlainText(QString("SIGCONT sent to own worker %1").arg(workerPid_));
    return ok;
#else
    return false;
#endif
}

bool SystemMonitorPage::stopWorker() {
    if (!workerRunning()) return false;
#ifndef _WIN32
    signalOwned(worker_, workerPid_, SIGCONT);      // a suspended worker must run to handle SIGTERM
#endif
    worker_->terminate();
    workerLog_->appendPlainText(QString("cooperative stop requested (SIGTERM) for worker %1").arg(workerPid_));
    return true;
}

bool SystemMonitorPage::killWorker(bool confirmed) {
    if (!workerRunning() || !confirmed) return false;
    worker_->kill();
    workerLog_->appendPlainText(QString("worker %1 killed forcefully (confirmed by the user)").arg(workerPid_));
    return true;
}
