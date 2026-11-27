// RtSchedTab.cpp - Scheduling > Real-Time Scheduling (see RtSchedTab.h).
#include "RtSchedTab.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include "ModuleKit.h"
#include "Style.h"

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}

// ======================================================================= view

RtGanttView::RtGanttView(QWidget* parent) : QWidget(parent) {}

void RtGanttView::setResult(const RtResult* r) {
    r_ = r;
    time_ = r ? r->cfg.horizon : 0;
    update();
}

void RtGanttView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::WindowText);
    if (!r_) { p.drawText(rect(), Qt::AlignCenter, "No valid task set"); return; }
    const RtConfig& c = r_->cfg;
    int lanes = (int)c.tasks.size() + 1;
    double top = title_.isEmpty() ? 6 : 22, left = 86, right = 10, bottom = 36;
    double lh = std::min(46.0, (height() - top - bottom) / lanes);
    double w = width() - left - right;
    double sx = w / c.horizon;
    if (!title_.isEmpty()) { p.setPen(ink); p.drawText(QRectF(4, 2, width() - 8, 18), Qt::AlignLeft | Qt::AlignVCenter, title_); }
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() * 0.8));
    p.setFont(small);
    // lanes
    for (int l = 0; l < lanes; ++l) {
        double y = top + l * lh;
        p.setPen(ink);
        QString name = l == 0 ? "CPU" : QString("%1 (C%2 T%3 D%4)").arg(q(c.tasks[l - 1].name)).arg(c.tasks[l - 1].C).arg(c.tasks[l - 1].T).arg(c.tasks[l - 1].D);
        p.drawText(QRectF(0, y, left - 4, lh), Qt::AlignRight | Qt::AlignVCenter | Qt::TextWordWrap, name);
        p.setPen(QColor("#d0d4d8"));
        p.drawLine(QPointF(left, y + lh - 1), QPointF(left + w, y + lh - 1));
    }
    // executed slots up to time_
    for (int t = 0; t < std::min(time_, (int)r_->slotJob.size()); ++t) {
        int j = r_->slotJob[t];
        if (j < 0) continue;
        const RtJob& job = r_->jobs[j];
        QColor col = ui::pidColor(job.task).lighter(110);
        QRectF cpu(left + t * sx, top + 6, sx, lh - 12), lane(left + t * sx, top + (job.task + 1) * lh + 8, sx, lh - 16);
        bool late = job.missed && t >= job.deadline;
        for (QRectF r : {cpu, lane}) {
            p.setPen(QPen(QColor("#5f6368"), 0.6));
            p.setBrush(col);
            p.drawRect(r);
            if (late) p.fillRect(r, QBrush(ui::faultColor(), Qt::BDiagPattern));
        }
        bool first = t == 0 || r_->slotJob[t - 1] != j;
        if (first && sx * 3 > p.fontMetrics().horizontalAdvance("X#9")) {
            p.setPen(ui::darkInk());
            p.drawText(QRectF(cpu.left() + 1, cpu.top(), sx * 4, cpu.height()), Qt::AlignLeft | Qt::AlignVCenter, QString("%1#%2").arg(q(c.tasks[job.task].name).left(4)).arg(job.index));
        }
    }
    // releases (up arrows), deadlines (down ticks), misses (red X) up to time_
    for (const RtJob& j : r_->jobs) {
        double y = top + (j.task + 1) * lh;
        if (j.release <= time_) {
            double x = left + j.release * sx;
            p.setPen(QPen(ink, 1.3));
            p.drawLine(QPointF(x, y + lh - 2), QPointF(x, y + 2));
            p.drawLine(QPointF(x, y + 2), QPointF(x - 3, y + 7));
            p.drawLine(QPointF(x, y + 2), QPointF(x + 3, y + 7));
        }
        if (j.release <= time_ && j.deadline <= c.horizon) {
            double x = left + j.deadline * sx;
            p.setPen(QPen(QColor("#c5221f"), 1.3));
            p.drawLine(QPointF(x, y + 2), QPointF(x, y + lh - 4));
            p.drawLine(QPointF(x, y + lh - 4), QPointF(x - 3, y + lh - 9));
            p.drawLine(QPointF(x, y + lh - 4), QPointF(x + 3, y + lh - 9));
        }
        if (j.missed && j.deadline <= time_) {
            double x = left + j.deadline * sx, yc = y + lh / 2;
            p.setPen(QPen(ui::faultColor(), 3));
            p.drawLine(QPointF(x - 6, yc - 6), QPointF(x + 6, yc + 6));
            p.drawLine(QPointF(x - 6, yc + 6), QPointF(x + 6, yc - 6));
        }
    }
    // time axis and cursor
    double ay = top + lanes * lh + 2;
    p.setPen(ink);
    int step = std::max(1, (int)std::ceil(28.0 / sx));
    for (int t = 0; t <= c.horizon; t += step) {
        double x = left + t * sx;
        p.drawLine(QPointF(x, ay), QPointF(x, ay + 4));
        p.drawText(QRectF(x - 15, ay + 4, 30, 14), Qt::AlignCenter, QString::number(t));
    }
    if (time_ < c.horizon) {
        double x = left + time_ * sx;
        p.setPen(QPen(QColor("#1a73e8"), 2, Qt::DashLine));
        p.drawLine(QPointF(x, top), QPointF(x, ay));
    }
    p.drawText(QRectF(4, ay + 18, width() - 8, 14), Qt::AlignLeft | Qt::AlignVCenter, "up arrow = release   red down arrow = deadline   red X = deadline miss   hatched = running late");
}

// ======================================================================= page

RtSchedPage::RtSchedPage(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Real-Time Scheduling",
        "Periodic tasks on one CPU: Rate Monotonic (fixed priority by period) and Earliest Deadline First (dynamic priority by "
        "absolute deadline), with releases, preemptions and deadline misses. Observed trace vs mathematical guarantee.");
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const RtPreset& p : rtPresets()) preset_->addItem(q(p.name));
    presetText_ = new QLabel;
    presetText_->setWordWrap(true);
    tasks_ = new QTableWidget(0, 5);
    tasks_->setHorizontalHeaderLabels({"Name", "C", "T", "D", "Offset"});
    tasks_->verticalHeader()->setVisible(false);
    tasks_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tasks_->setMinimumHeight(130);
    tasks_->setToolTip("C = execution time, T = period, D = relative deadline (C <= D <= T), Offset = first release");
    auto* add = new QPushButton("Add task");
    auto* del = new QPushButton("Remove task");
    horizon_ = new QSpinBox;
    horizon_->setRange(1, 1000);
    horizon_->setValue(35);
    horizon_->setToolTip("Simulated time units [0, horizon)");
    algo_ = new QComboBox;
    algo_->addItems({rtAlgoName(RtAlgo::RM), rtAlgoName(RtAlgo::EDF)});
    policy_ = new QComboBox;
    policy_->addItems({"Late job continues", "Late job is aborted at its deadline"});
    policy_->setToolTip("What happens to a job that misses its deadline (counted once either way)");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow("Preset:", preset_);
    cf->addRow(presetText_);
    cf->addRow(new QLabel("<b>Periodic tasks</b>"));
    cf->addRow(tasks_);
    auto* bw = new QWidget;
    auto* bl = new QGridLayout(bw);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(add, 0, 0);
    bl->addWidget(del, 0, 1);
    cf->addRow(bw);
    cf->addRow("Horizon:", horizon_);
    cf->addRow("Algorithm:", algo_);
    cf->addRow("Missed deadline:", policy_);

    gantt_ = new RtGanttView;

    now_ = new QLabel;
    now_->setWordWrap(true);
    ready_ = new QListWidget;
    ready_->setMaximumHeight(120);
    analysis_ = new QLabel;
    analysis_->setWordWrap(true);
    analysis_->setTextFormat(Qt::RichText);
    stats_ = new QTableWidget(0, 7);
    stats_->setHorizontalHeaderLabels({"Task", "Released", "Completed", "Missed", "Aborted", "Worst resp.", "Avg resp."});
    stats_->verticalHeader()->setVisible(false);
    stats_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stats_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>At the cursor</b>"));
    il->addWidget(now_);
    il->addWidget(new QLabel("Ready jobs (highest priority first):"));
    il->addWidget(ready_);
    il->addWidget(new QLabel("<b>Schedulability analysis</b>"));
    il->addWidget(analysis_);
    il->addWidget(new QLabel("<b>Per-task results (whole horizon)</b>"));
    il->addWidget(stats_, 1);

    bar_ = new PlaybackBar;
    auto* bottom = new QTabWidget;
    events_ = new QTableWidget(0, 3);
    events_->setHorizontalHeaderLabels({"Time", "Event", "Details"});
    events_->verticalHeader()->setVisible(false);
    events_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    events_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    events_->horizontalHeader()->setStretchLastSection(true);
    bottom->addTab(events_, "Event log");
    cmp_ = new QTableWidget(0, 7);
    cmp_->setHorizontalHeaderLabels({"Algorithm", "Deadline misses", "First miss", "Preemptions", "Context switches", "Idle slots", "Worst response per task"});
    cmp_->verticalHeader()->setVisible(false);
    cmp_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cmp_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    cmp_->horizontalHeader()->setStretchLastSection(true);
    cmpRmView_ = new RtGanttView;
    cmpEdfView_ = new RtGanttView;
    auto* cw = new QWidget;
    auto* cl = new QVBoxLayout(cw);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(cmp_);
    auto* gl = new QHBoxLayout;
    gl->addWidget(cmpRmView_);
    gl->addWidget(cmpEdfView_);
    cl->addLayout(gl, 1);
    bottom->addTab(cw, "RM vs EDF (same input)");

    assembleModule(this, header_, cfgW, gantt_, titledBox("Inspector", insW), bar_, bottom);

    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    connect(add, &QPushButton::clicked, this, [this] {
        RtConfig c;
        QString err;
        std::vector<RtTask> t;
        for (int r = 0; r < tasks_->rowCount(); ++r)
            t.push_back(RtTask{tasks_->item(r, 0) ? tasks_->item(r, 0)->text().toStdString() : "", tasks_->item(r, 1) ? tasks_->item(r, 1)->text().toInt() : 1,
                               tasks_->item(r, 2) ? tasks_->item(r, 2)->text().toInt() : 4, tasks_->item(r, 3) ? tasks_->item(r, 3)->text().toInt() : 4,
                               tasks_->item(r, 4) ? tasks_->item(r, 4)->text().toInt() : 0});
        if (t.size() >= 8) { ui::warn(this, "At most 8 tasks"); return; }
        t.push_back(RtTask{"T" + std::to_string(t.size() + 1), 1, 10, 10, 0});
        setTasks(t, horizon_->value());
    });
    connect(del, &QPushButton::clicked, this, [this] {
        int r = tasks_->currentRow() >= 0 ? tasks_->currentRow() : tasks_->rowCount() - 1;
        if (r < 0 || tasks_->rowCount() <= 1) return;
        tasks_->removeRow(r);
        rebuild();
    });
    connect(tasks_, &QTableWidget::itemChanged, this, [this] { if (!loading_) { loading_ = true; preset_->setCurrentIndex(0); loading_ = false; rebuild(); } });
    connect(horizon_, &QSpinBox::valueChanged, this, [this] { if (!loading_) rebuild(); });
    for (QComboBox* c : {algo_, policy_}) connect(c, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) rebuild(); });
    connect(bar_, &PlaybackBar::positionChanged, this, &RtSchedPage::onStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });
    loadPreset(0);
}

void RtSchedPage::fillTasks(const std::vector<RtTask>& t) {
    bool old = loading_;
    loading_ = true;
    tasks_->setRowCount((int)t.size());
    for (int i = 0; i < (int)t.size(); ++i) {
        QStringList c = {q(t[i].name), QString::number(t[i].C), QString::number(t[i].T), QString::number(t[i].D), QString::number(t[i].offset)};
        for (int k = 0; k < 5; ++k) {
            auto* it = new QTableWidgetItem(c[k]);
            it->setTextAlignment(Qt::AlignCenter);
            if (k == 0) { it->setBackground(ui::pidColor(i).lighter(150)); it->setForeground(ui::darkInk()); }
            tasks_->setItem(i, k, it);
        }
    }
    loading_ = old;
}

bool RtSchedPage::readConfig(RtConfig& c, QString& err) const {
    c.tasks.clear();
    for (int r = 0; r < tasks_->rowCount(); ++r) {
        RtTask t;
        t.name = tasks_->item(r, 0) ? tasks_->item(r, 0)->text().trimmed().toStdString() : "";
        int* f[4] = {&t.C, &t.T, &t.D, &t.offset};
        for (int k = 0; k < 4; ++k) {
            bool ok = false;
            *f[k] = tasks_->item(r, k + 1) ? tasks_->item(r, k + 1)->text().trimmed().toInt(&ok) : 0;
            if (!ok) { err = QString("row %1: '%2' is not a whole number").arg(r + 1).arg(tasks_->item(r, k + 1) ? tasks_->item(r, k + 1)->text() : ""); return false; }
        }
        c.tasks.push_back(t);
    }
    c.horizon = horizon_->value();
    c.algo = (RtAlgo)algo_->currentIndex();
    c.policy = (MissPolicy)policy_->currentIndex();
    try { validateRtConfig(c); } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

bool RtSchedPage::loadPreset(int i) {
    if (i < 0 || i >= (int)rtPresets().size()) return false;
    RtPreset p = rtPresets()[i];
    loading_ = true;
    preset_->setCurrentIndex(i + 1);
    presetText_->setText(q(p.description));
    horizon_->setValue(p.cfg.horizon);
    fillTasks(p.cfg.tasks);
    loading_ = false;
    bool ok = rebuild();
    emit logMessage("Real-time scheduling: preset '" + q(p.name) + "'");
    return ok;
}

bool RtSchedPage::setTasks(const std::vector<RtTask>& t, int horizon) {
    loading_ = true;
    fillTasks(t);
    horizon_->setValue(horizon);
    preset_->setCurrentIndex(0);
    presetText_->clear();
    loading_ = false;
    return rebuild();
}

void RtSchedPage::setAlgorithm(RtAlgo a) { algo_->setCurrentIndex((int)a); }
void RtSchedPage::setPolicy(MissPolicy p) { policy_->setCurrentIndex((int)p); }

bool RtSchedPage::rebuild() {
    RtConfig c;
    QString err;
    gantt_->setResult(nullptr);
    cmpRmView_->setResult(nullptr);
    cmpEdfView_->setResult(nullptr);
    if (!readConfig(c, err)) {
        result_.reset();
        cmpRm_.reset();
        cmpEdf_.reset();
        bar_->setLength(0);
        header_->setStatus("Invalid task set: " + err, ui::semError());
        analysis_->setText("Fix the task table: " + err);
        return false;
    }
    result_ = runRt(c);
    RtConfig a = c, b = c;
    a.algo = RtAlgo::RM;
    b.algo = RtAlgo::EDF;
    cmpRm_ = runRt(a);
    cmpEdf_ = runRt(b);
    gantt_->setResult(&*result_);
    RtAnalysis an = analyseRt(c);
    analysis_->setText(q(an.text).toHtmlEscaped() + QString("<br><b>Observed over [0, %1)</b>: %2 deadline miss(es) with %3. A clean trace over a finite horizon is an "
                                            "observation, not a proof (unless the horizon covers the hyperperiod from a synchronous start).")
                                        .arg(c.horizon).arg(result_->misses).arg(rtAlgoName(c.algo)));
    stats_->setRowCount((int)c.tasks.size());
    for (int i = 0; i < (int)c.tasks.size(); ++i) {
        const RtTaskStats& s = result_->stats[i];
        QStringList cells = {q(c.tasks[i].name), QString::number(s.released), QString::number(s.completed), QString::number(s.missed),
                             QString::number(s.dropped), s.worstResponse >= 0 ? QString::number(s.worstResponse) : "-",
                             s.completed ? QString::number(s.avgResponse, 'f', 2) : "-"};
        for (int k = 0; k < 7; ++k) stats_->setItem(i, k, ui::roItem(cells[k]));
        if (s.missed) ui::shade(stats_->item(i, 3), ui::lightRed());
    }
    events_->setRowCount((int)result_->events.size());
    for (int i = 0; i < (int)result_->events.size(); ++i) {
        const RtEvent& e = result_->events[i];
        QStringList cells = {QString::number(e.time), rtEventName(e.kind), q(e.text)};
        for (int k = 0; k < 3; ++k) {
            auto* it = ui::roItem(cells[k]);
            if (k == 2) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (e.kind == RtEventKind::DeadlineMiss || e.kind == RtEventKind::Drop) ui::shade(it, ui::lightRed());
            events_->setItem(i, k, it);
        }
    }
    fillComparison();
    bar_->setLength(c.horizon);
    bar_->setPosition(c.horizon);
    header_->setStatus(QString("%1: U = %2 %, %3 deadline miss(es), %4 preemption(s) over %5 time units")
                           .arg(rtAlgoName(c.algo)).arg(result_->utilization * 100, 0, 'f', 1).arg(result_->misses).arg(result_->preemptions).arg(c.horizon),
                       result_->misses ? ui::semError() : ui::hitColor().darker(130));
    return true;
}

void RtSchedPage::fillComparison() {
    cmp_->setRowCount(2);
    int i = 0;
    for (const std::optional<RtResult>* r : {&cmpRm_, &cmpEdf_}) {
        const RtResult& x = **r;
        QStringList worst;
        for (size_t k = 0; k < x.cfg.tasks.size(); ++k)
            worst << QString("%1: %2").arg(q(x.cfg.tasks[k].name)).arg(x.stats[k].worstResponse >= 0 ? QString::number(x.stats[k].worstResponse) : "-");
        QStringList cells = {rtAlgoName(x.cfg.algo), QString::number(x.misses), x.firstMiss >= 0 ? QString("t = %1").arg(x.firstMiss) : "none",
                             QString::number(x.preemptions), QString::number(x.contextSwitches), QString::number(x.idleSlots), worst.join(", ")};
        for (int k = 0; k < 7; ++k) cmp_->setItem(i, k, ui::roItem(cells[k]));
        ui::shade(cmp_->item(i, 1), x.misses ? ui::lightRed() : ui::lightGreen());
        ++i;
    }
    cmpRmView_->setResult(&*cmpRm_);
    cmpRmView_->setTitle(QString("RM: %1 miss(es)").arg(cmpRm_->misses));
    cmpEdfView_->setResult(&*cmpEdf_);
    cmpEdfView_->setTitle(QString("EDF: %1 miss(es)").arg(cmpEdf_->misses));
}

void RtSchedPage::onStep(int pos, bool) {
    if (!result_) return;
    gantt_->setTime(pos);
    const RtResult& r = *result_;
    // jobs present at instant pos (released, not completed / aborted before pos)
    std::vector<int> act;
    for (int j = 0; j < (int)r.jobs.size(); ++j) {
        const RtJob& x = r.jobs[j];
        bool gone = (x.finish >= 0 && x.finish <= pos);
        if (x.dropped) for (const RtEvent& e : r.events) if (e.kind == RtEventKind::Drop && e.job == j && e.time <= pos) gone = true;
        if (x.release <= pos && !gone && pos < r.cfg.horizon) act.push_back(j);
    }
    const RtConfig& c = r.cfg;
    std::sort(act.begin(), act.end(), [&](int a, int b) {
        const RtJob &A = r.jobs[a], &B = r.jobs[b];
        if (c.algo == RtAlgo::RM) {
            if (c.tasks[A.task].T != c.tasks[B.task].T) return c.tasks[A.task].T < c.tasks[B.task].T;
            if (A.task != B.task) return A.task < B.task;
            return A.release < B.release;
        }
        if (A.deadline != B.deadline) return A.deadline < B.deadline;
        if (A.release != B.release) return A.release < B.release;
        return A.task < B.task;
    });
    ready_->clear();
    for (int j : act) {
        const RtJob& x = r.jobs[j];
        auto* it = new QListWidgetItem(QString("%1#%2  release %3, deadline %4%5").arg(q(c.tasks[x.task].name)).arg(x.index).arg(x.release).arg(x.deadline)
                                           .arg(c.algo == RtAlgo::RM ? QString(", period %1").arg(c.tasks[x.task].T) : QString()));
        it->setBackground(ui::pidColor(x.task).lighter(150));
        it->setForeground(ui::darkInk());
        ready_->addItem(it);
    }
    if (ready_->count() == 0) ready_->addItem(pos >= c.horizon ? "(end of the horizon)" : "(no ready job: CPU idle)");
    int run = pos < (int)r.slotJob.size() ? r.slotJob[pos] : -1;
    now_->setText(pos >= c.horizon ? QString("t = %1: end of the horizon. %2 deadline miss(es).").arg(pos).arg(r.misses)
                                   : QString("t = %1: %2 runs in the slot [%1, %3).").arg(pos)
                                         .arg(run >= 0 ? QString("%1#%2").arg(q(c.tasks[r.jobs[run].task].name)).arg(r.jobs[run].index) : QString("nothing (idle)"))
                                         .arg(pos + 1));
    for (int i = 0; i < events_->rowCount(); ++i) {
        bool past = r.events[i].time <= pos;
        for (int k = 0; k < 3; ++k) if (auto* it = events_->item(i, k)) { QFont f = it->font(); f.setBold(r.events[i].time == pos); it->setFont(f); it->setForeground(past ? palette().text() : QBrush(ui::semIdle())); }
    }
}

QString RtSchedPage::reportText() const {
    if (!result_) return "Real-time scheduling: (no valid task set)\n";
    QString t = QString("Real-time scheduling, horizon %1, U = %2:\n").arg(result_->cfg.horizon).arg(result_->utilization, 0, 'f', 4);
    for (const RtTask& k : result_->cfg.tasks) t += QString("  %1: C=%2 T=%3 D=%4 offset=%5\n").arg(q(k.name)).arg(k.C).arg(k.T).arg(k.D).arg(k.offset);
    for (const std::optional<RtResult>* r : {&cmpRm_, &cmpEdf_})
        t += QString("  %1: %2 miss(es), first %3, %4 preemptions\n").arg(rtAlgoName((*r)->cfg.algo)).arg((*r)->misses).arg((*r)->firstMiss).arg((*r)->preemptions);
    return t;
}
