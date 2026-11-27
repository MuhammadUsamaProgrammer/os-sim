// MlqTab.cpp - Scheduling > Multilevel Queues (see MlqTab.h).
#include "MlqTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include "GanttWidget.h"
#include "ModuleKit.h"
#include "Style.h"

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QTableWidget* editTable(const QStringList& h) {
    auto* t = new QTableWidget(0, h.size());
    t->setHorizontalHeaderLabels(h);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    return t;
}
void cell(QTableWidget* t, int r, int c, const QString& v) {
    auto* it = new QTableWidgetItem(v);
    it->setTextAlignment(Qt::AlignCenter);
    t->setItem(r, c, it);
}
}

// ======================================================================= queue view

QueueLevelView::QueueLevelView(QWidget* parent) : QWidget(parent) {}

void QueueLevelView::setTick(const MlqResult* r, int tick) { r_ = r; tick_ = tick; update(); }

void QueueLevelView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::WindowText);
    if (!r_) return;
    int nq = (int)r_->cfg.queues.size();
    const MlqTick* tk = tick_ > 0 && tick_ <= (int)r_->ticks.size() ? &r_->ticks[tick_ - 1] : nullptr;
    double top = 20, lh = std::min(40.0, (height() - top - 4) / nq), left = 170, cpuW = 90;
    p.setPen(ink);
    p.drawText(QRectF(4, 0, width() - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
               tk ? QString("Ready queues after the dispatch of tick %1 (highest priority on top)").arg(tk->t) : QString("Before the first tick"));
    for (int k = 0; k < nq; ++k) {
        double y = top + k * lh;
        p.setPen(ink);
        p.drawText(QRectF(4, y, left - 8, lh), Qt::AlignLeft | Qt::AlignVCenter, q(r_->cfg.queues[k].name));
        QRectF lane(left, y + 4, width() - left - cpuW - 16, lh - 8);
        p.setPen(QPen(QColor("#9aa0a6"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(lane, 4, 4);
        if (!tk) continue;
        double x = lane.left() + 4;
        for (int pid : tk->queues[k]) {
            QRectF chip(x, lane.top() + 3, 40, lane.height() - 6);
            p.setPen(Qt::NoPen);
            p.setBrush(ui::pidColor(pid));
            p.drawRoundedRect(chip, 4, 4);
            p.setPen(Qt::white);
            p.drawText(chip, Qt::AlignCenter, QString("P%1").arg(pid));
            x += 44;
        }
        if (tk->pid >= 0 && tk->queue == k) {
            QRectF cpu(width() - cpuW - 8, y + 4, cpuW, lh - 8);
            p.setPen(QPen(ink, 1.5));
            p.setBrush(ui::pidColor(tk->pid).lighter(130));
            p.drawRoundedRect(cpu, 4, 4);
            p.setPen(ui::darkInk());
            p.drawText(cpu, Qt::AlignCenter, QString("CPU: P%1").arg(tk->pid));
        }
    }
}

// ======================================================================= page

MlqPage::MlqPage(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Multilevel Queues",
        "Multilevel Queue (fixed queue per process) and Multilevel Feedback Queue (processes move between levels: demotion after "
        "a full quantum, optional priority boost). Higher queues always run first.");
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const MlqPreset& p : mlqPresets()) preset_->addItem(q(p.name));
    presetText_ = new QLabel;
    presetText_->setWordWrap(true);
    mode_ = new QComboBox;
    mode_->addItems({"Multilevel Queue (MLQ)", "Multilevel Feedback Queue (MLFQ)"});
    nQueues_ = new QSpinBox;
    nQueues_->setRange(1, 4);
    nQueues_->setValue(3);
    queues_ = editTable({"Queue", "Policy (RR/FCFS)", "Quantum"});
    queues_->setMinimumHeight(110);
    boost_ = new QSpinBox;
    boost_->setRange(0, 1000);
    boost_->setToolTip("MLFQ: every S ticks all processes return to queue 0 (0 = off)");
    preempt_ = new QCheckBox("A higher queue preempts a lower one");
    preempt_->setChecked(true);
    procs_ = editTable({"PID", "Arrival", "Burst", "Queue (MLQ)"});
    procs_->setMinimumHeight(140);
    procs_->setToolTip("Queue = fixed queue index for MLQ (ignored by MLFQ: every process starts in queue 0)");
    auto* add = new QPushButton("Add process");
    auto* del = new QPushButton("Remove");
    auto* copy = new QPushButton("Copy from CPU Scheduling");
    copy->setToolTip("Uses the process table of the CPU Scheduling page (priority -> queue)");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow("Preset:", preset_);
    cf->addRow(presetText_);
    cf->addRow("Mode:", mode_);
    cf->addRow("Queues:", nQueues_);
    cf->addRow(queues_);
    cf->addRow("Boost every:", boost_);
    cf->addRow(preempt_);
    cf->addRow(new QLabel("<b>Processes</b>"));
    cf->addRow(procs_);
    auto* bw = new QWidget;
    auto* bl = new QGridLayout(bw);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(add, 0, 0);
    bl->addWidget(del, 0, 1);
    bl->addWidget(copy, 1, 0, 1, 2);
    cf->addRow(bw);

    gantt_ = new GanttWidget;
    levels_ = new QueueLevelView;
    auto* center = new QSplitter(Qt::Vertical);
    center->addWidget(titledBox("Gantt chart", gantt_));
    center->addWidget(titledBox("Queue levels", levels_));

    now_ = new QLabel;
    now_->setWordWrap(true);
    events_ = new QListWidget;
    events_->setWordWrap(true);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(now_);
    il->addWidget(new QLabel("<b>Events of this tick</b>"));
    il->addWidget(events_, 1);
    il->addWidget(summary_);

    bar_ = new PlaybackBar;
    metrics_ = new QTableWidget(0, 8);
    metrics_->setHorizontalHeaderLabels({"PID", "Arrival", "Burst", "Final queue", "Completion", "Waiting", "Turnaround", "Response"});
    metrics_->verticalHeader()->setVisible(false);
    metrics_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metrics_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    assembleModule(this, header_, cfgW, center, titledBox("Inspector", insW), bar_, metrics_);

    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    auto changed = [this] { if (!loading_) { loading_ = true; preset_->setCurrentIndex(0); loading_ = false; rebuild(); } };
    connect(mode_, &QComboBox::currentIndexChanged, this, changed);
    connect(boost_, &QSpinBox::valueChanged, this, changed);
    connect(preempt_, &QCheckBox::toggled, this, changed);
    connect(queues_, &QTableWidget::itemChanged, this, changed);
    connect(procs_, &QTableWidget::itemChanged, this, changed);
    connect(nQueues_, &QSpinBox::valueChanged, this, [this, changed](int n) {
        if (loading_) return;
        std::vector<MlqQueue> qs;
        for (int k = 0; k < n; ++k) {
            if (k < queues_->rowCount() && queues_->item(k, 0)) {
                MlqQueue m;
                m.name = queues_->item(k, 0)->text().toStdString();
                m.policy = queues_->item(k, 1) && queues_->item(k, 1)->text().trimmed().toUpper() == "FCFS" ? QueuePolicy::FCFS : QueuePolicy::RR;
                m.quantum = queues_->item(k, 2) ? queues_->item(k, 2)->text().toInt() : 2;
                qs.push_back(m);
            } else qs.push_back({"Q" + std::to_string(k), k + 1 == n ? QueuePolicy::FCFS : QueuePolicy::RR, 2 << k});
        }
        fillQueues(qs);
        changed();
    });
    connect(add, &QPushButton::clicked, this, [this, changed] {
        int r = procs_->rowCount();
        int pid = 1;
        for (int i = 0; i < r; ++i) if (procs_->item(i, 0)) pid = std::max(pid, procs_->item(i, 0)->text().toInt() + 1);
        loading_ = true;
        procs_->insertRow(r);
        cell(procs_, r, 0, QString::number(pid));
        cell(procs_, r, 1, "0");
        cell(procs_, r, 2, "3");
        cell(procs_, r, 3, "0");
        loading_ = false;
        changed();
    });
    connect(del, &QPushButton::clicked, this, [this, changed] {
        int r = procs_->currentRow() >= 0 ? procs_->currentRow() : procs_->rowCount() - 1;
        if (r < 0) return;
        procs_->removeRow(r);
        changed();
    });
    connect(copy, &QPushButton::clicked, this, [this] { copyFromScheduling(); });
    connect(bar_, &PlaybackBar::positionChanged, this, &MlqPage::onStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });
    loadPreset(1);
}

void MlqPage::fillQueues(const std::vector<MlqQueue>& qs) {
    bool old = loading_;
    loading_ = true;
    queues_->setRowCount((int)qs.size());
    for (int k = 0; k < (int)qs.size(); ++k) {
        cell(queues_, k, 0, q(qs[k].name));
        cell(queues_, k, 1, qs[k].policy == QueuePolicy::RR ? "RR" : "FCFS");
        cell(queues_, k, 2, qs[k].policy == QueuePolicy::RR ? QString::number(qs[k].quantum) : "-");
    }
    nQueues_->setValue((int)qs.size());
    loading_ = old;
}

void MlqPage::fillProcs(const std::vector<PCB>& ps) {
    bool old = loading_;
    loading_ = true;
    procs_->setRowCount((int)ps.size());
    for (int i = 0; i < (int)ps.size(); ++i) {
        cell(procs_, i, 0, QString::number(ps[i].pid));
        cell(procs_, i, 1, QString::number(ps[i].arrival));
        cell(procs_, i, 2, QString::number(ps[i].burst));
        cell(procs_, i, 3, QString::number(ps[i].priority));
    }
    loading_ = old;
}

bool MlqPage::readConfig(MlqConfig& c, std::vector<PCB>& ps, QString& err) const {
    c.feedback = mode_->currentIndex() == 1;
    c.boostInterval = boost_->value();
    c.preemptive = preempt_->isChecked();
    c.queues.clear();
    for (int k = 0; k < queues_->rowCount(); ++k) {
        MlqQueue m;
        m.name = queues_->item(k, 0) ? queues_->item(k, 0)->text().trimmed().toStdString() : "Q" + std::to_string(k);
        QString pol = queues_->item(k, 1) ? queues_->item(k, 1)->text().trimmed().toUpper() : "RR";
        if (pol != "RR" && pol != "FCFS") { err = QString("queue %1: policy must be RR or FCFS").arg(k); return false; }
        m.policy = pol == "RR" ? QueuePolicy::RR : QueuePolicy::FCFS;
        bool ok = true;
        m.quantum = m.policy == QueuePolicy::RR ? (queues_->item(k, 2) ? queues_->item(k, 2)->text().toInt(&ok) : 0) : 0;
        if (!ok) { err = QString("queue %1: quantum is not a number").arg(k); return false; }
        c.queues.push_back(m);
    }
    ps.clear();
    for (int r = 0; r < procs_->rowCount(); ++r) {
        PCB p;
        int* f[4] = {&p.pid, &p.arrival, &p.burst, &p.priority};
        for (int k = 0; k < 4; ++k) {
            bool ok = false;
            *f[k] = procs_->item(r, k) ? procs_->item(r, k)->text().trimmed().toInt(&ok) : 0;
            if (!ok) { err = QString("process row %1: not a whole number").arg(r + 1); return false; }
        }
        ps.push_back(p);
    }
    try { validateMlqConfig(c, ps); } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

bool MlqPage::loadPreset(int i) {
    if (i < 0 || i >= (int)mlqPresets().size()) return false;
    MlqPreset p = mlqPresets()[i];
    loading_ = true;
    preset_->setCurrentIndex(i + 1);
    presetText_->setText(q(p.description));
    mode_->setCurrentIndex(p.cfg.feedback ? 1 : 0);
    boost_->setValue(p.cfg.boostInterval);
    preempt_->setChecked(p.cfg.preemptive);
    fillQueues(p.cfg.queues);
    fillProcs(p.procs);
    loading_ = false;
    bool ok = rebuild();
    emit logMessage("Multilevel queues: preset '" + q(p.name) + "'");
    return ok;
}

void MlqPage::setFeedback(bool on) { mode_->setCurrentIndex(on ? 1 : 0); }

bool MlqPage::copyFromScheduling() {
    if (!provider_) return false;
    std::vector<PCB> ps = provider_();
    if (ps.empty()) { ui::warn(this, "The CPU Scheduling process table is empty or invalid."); return false; }
    fillProcs(ps);
    loading_ = true;
    preset_->setCurrentIndex(0);
    loading_ = false;
    return rebuild();
}

bool MlqPage::rebuild() {
    MlqConfig c;
    std::vector<PCB> ps;
    QString err;
    if (!readConfig(c, ps, err)) {
        result_.reset();
        gantt_->setTimeline({});
        levels_->setTick(nullptr, 0);
        bar_->setLength(0);
        metrics_->setRowCount(0);
        header_->setStatus("Invalid input: " + err, ui::semError());
        return false;
    }
    result_ = runMlq(c, ps);
    const MlqResult& r = *result_;
    gantt_->setTimeline(r.sched.timeline);
    metrics_->setRowCount((int)r.sched.metrics.size() + 1);
    for (int i = 0; i < (int)r.sched.metrics.size(); ++i) {
        const ProcMetrics& m = r.sched.metrics[i];
        int lvl = 0;
        for (int k = 0; k < (int)ps.size(); ++k) if (ps[k].pid == m.pid) lvl = r.queueOf[k];
        QStringList cells = {QString("P%1").arg(m.pid), QString::number(m.arrival), QString::number(m.burst), q(c.queues[lvl].name),
                             QString::number(m.completion), QString::number(m.waiting), QString::number(m.turnaround), QString::number(m.response)};
        for (int k = 0; k < 8; ++k) metrics_->setItem(i, k, ui::roItem(cells[k]));
    }
    int last = (int)r.sched.metrics.size();
    QStringList avg = {"Average", "", "", "", "", QString::number(r.sched.avgWaiting, 'f', 2), QString::number(r.sched.avgTurnaround, 'f', 2),
                       QString::number(r.sched.avgResponse, 'f', 2)};
    for (int k = 0; k < 8; ++k) { auto* it = ui::roItem(avg[k]); QFont f = it->font(); f.setBold(true); it->setFont(f); metrics_->setItem(last, k, it); }
    summary_->setText(QString("<b>Whole run</b>: average waiting %1, turnaround %2, response %3; %4 context switch(es), %5 demotion(s), "
                              "%6 preemption(s), %7 boost(s).")
                          .arg(r.sched.avgWaiting, 0, 'f', 2).arg(r.sched.avgTurnaround, 0, 'f', 2).arg(r.sched.avgResponse, 0, 'f', 2)
                          .arg(r.sched.contextSwitches).arg(r.demotions).arg(r.preemptions).arg(r.boosts));
    bar_->setLength((int)r.ticks.size());
    bar_->setPosition((int)r.ticks.size());
    header_->setStatus(QString("%1: %2 ticks, average waiting %3").arg(c.feedback ? "MLFQ" : "MLQ").arg(r.ticks.size()).arg(r.sched.avgWaiting, 0, 'f', 2));
    return true;
}

void MlqPage::onStep(int pos, bool) {
    if (!result_) return;
    const MlqResult& r = *result_;
    gantt_->setVisibleUntil(pos);
    levels_->setTick(&r, pos);
    events_->clear();
    if (pos == 0) { now_->setText("t = 0: nothing has run yet. Press Run or Next."); return; }
    const MlqTick& tk = r.ticks[pos - 1];
    now_->setText(tk.pid >= 0 ? QString("<b>Tick %1</b>: P%2 runs from %3").arg(tk.t).arg(tk.pid).arg(q(r.cfg.queues[tk.queue].name))
                              : QString("<b>Tick %1</b>: CPU idle").arg(tk.t));
    for (const std::string& e : tk.events) events_->addItem(q(e));
    if (tk.events.empty()) events_->addItem("(no queue changes)");
}

QString MlqPage::reportText() const {
    if (!result_) return "Multilevel queues: (no valid input)\n";
    const MlqResult& r = *result_;
    QString t = QString("%1 with %2 queue(s):\n").arg(r.cfg.feedback ? "Multilevel Feedback Queue" : "Multilevel Queue").arg(r.cfg.queues.size());
    for (const ProcMetrics& m : r.sched.metrics)
        t += QString("  P%1 completion=%2 waiting=%3 turnaround=%4 response=%5\n").arg(m.pid).arg(m.completion).arg(m.waiting).arg(m.turnaround).arg(m.response);
    t += QString("  averages: waiting %1, turnaround %2; demotions %3, boosts %4\n").arg(r.sched.avgWaiting, 0, 'f', 2)
             .arg(r.sched.avgTurnaround, 0, 'f', 2).arg(r.demotions).arg(r.boosts);
    return t;
}
