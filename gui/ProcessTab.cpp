// ProcessTab.cpp - Process Management and lifecycle module (M2).
#include "ProcessTab.h"
#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <sstream>
#include "LifecycleViews.h"
#include "ModuleKit.h"
#include "NativeRunner.h"
#include "Style.h"

static const char* kDemoText[][2] = {
    {"fork", "fork(): one process becomes two; fork returns 0 in the child and the child's PID in the parent; wait() reaps it."},
    {"exec", "exec(): the child replaces its program image with a harmless helper program - the PID stays the same."},
    {"wait", "wait(): three children end at different times; the parent reaps each one and reads its exit status."},
    {"sleep", "sleep(): the child is blocked for about one second (real wall-clock time, varies) while the parent waits."},
    {"zombie", "Zombie: the child exits but the parent delays wait(); /proc shows state Z until it is reaped."},
    {"orphan", "Orphan: the parent exits first; the child is re-parented to a subreaper (this helper) - on other systems init or a session subreaper."},
    {"signal", "Signal: the parent sends SIGUSR1 to its own child; the child's handler only sets a flag (async-signal-safe)."},
};

ProcessTab::ProcessTab(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Process Management and Lifecycle",
        "What happens to a process before, during and after CPU scheduling: the 5-state model, the PCB, "
        "process creation and termination. The automatic mode reuses the Scheduling tab's dispatcher (schedulerPick).");
    native_ = new NativeRunner(this);

    // ---- configuration
    mode_ = new QComboBox;
    mode_->addItems({"Automatic (scheduler decides)", "Manual (you trigger every transition)"});
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const LcPreset& p : lifecyclePresets()) preset_->addItem(QString::fromStdString(p.name));
    policy_ = new QComboBox;
    for (Algo a : allAlgos()) policy_->addItem(algoName(a), (int)a);   // data = enum value
    quantum_ = new QSpinBox;
    quantum_->setRange(1, 20);
    quantum_->setValue(2);
    work_ = new QTableWidget(0, 7);
    work_->setHorizontalHeaderLabels({"PID", "PPID", "Name", "Arrival", "Priority", "Bursts CPU IO CPU..", "Pages"});
    work_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    work_->horizontalHeader()->setStretchLastSection(true);
    work_->verticalHeader()->setVisible(false);
    work_->setMinimumHeight(140);
    auto* addRow = new QPushButton("Add");
    auto* delRow = new QPushButton("Remove");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->setSpacing(6);
    cf->addRow("Mode:", mode_);
    cf->addRow("Preset:", preset_);
    cf->addRow("Dispatch policy:", policy_);
    cf->addRow("Quantum (RR):", quantum_);
    cf->addRow(new QLabel("<b>Workload</b> (bursts alternate CPU and I/O)"));
    cf->addRow(work_);
    auto* rb = new QHBoxLayout;
    rb->addWidget(addRow);
    rb->addWidget(delRow);
    cf->addRow(rb);
    manualBox_ = new QGroupBox("Manual transitions (validated)");
    target_ = new QComboBox;
    ioTicks_ = new QSpinBox;
    ioTicks_->setRange(1, 20);
    ioTicks_->setValue(3);
    auto* mg = new QGridLayout(manualBox_);
    mg->addWidget(new QLabel("Process:"), 0, 0);
    mg->addWidget(target_, 0, 1, 1, 2);
    QStringList cmds = {"Admit", "Dispatch", "Preempt", "Block I/O", "Complete I/O", "Exit", "Kill", "Tick +1", "Create child"};
    for (int i = 0; i < cmds.size(); ++i) {
        auto* b = new QPushButton(cmds[i]);
        b->setAccessibleName(cmds[i]);
        mg->addWidget(b, 1 + i / 3, i % 3);
        QString c = cmds[i];
        connect(b, &QPushButton::clicked, this, [this, c] {
            QString e = command(c, selectedPid(), ioTicks_->value());
            if (!e.isEmpty()) header_->setStatus("Refused: " + e, ui::semError());
        });
    }
    mg->addWidget(new QLabel("I/O or child CPU ticks:"), 4, 0, 1, 2);
    mg->addWidget(ioTicks_, 4, 2);
    cf->addRow(manualBox_);

    // ---- centre
    twoState_ = new QCheckBox("Two-state view (Running / Not Running)");
    view_ = new LifecycleView;
    auto* centerW = new QWidget;
    auto* cl = new QVBoxLayout(centerW);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(twoState_);
    cl->addWidget(view_, 1);

    // ---- inspector
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Process tree", "State"});
    tree_->setMinimumHeight(110);
    pcb_ = new QTableWidget(0, 2);
    pcb_->setHorizontalHeaderLabels({"PCB field", "Value"});
    pcb_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    pcb_->horizontalHeader()->setStretchLastSection(true);
    pcb_->verticalHeader()->setVisible(false);
    pcb_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    explain_ = new QLabel;
    explain_->setWordWrap(true);
    auto* ins = new QSplitter(Qt::Vertical);
    ins->addWidget(tree_);
    ins->addWidget(pcb_);
    ins->addWidget(explain_);
    ins->setSizes({120, 260, 90});

    // ---- bottom
    bar_ = new PlaybackBar;
    bottom_ = new QTabWidget;
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    bottom_->addTab(log_, "Event log");
    timeline_ = new LifecycleTimeline;
    timelineScroll_ = new QScrollArea;
    timelineScroll_->setWidget(timeline_);
    bottom_->addTab(timelineScroll_, "State timeline");
    // Live Linux demo page
    demo_ = new QComboBox;
    for (auto& d : kDemoText) demo_->addItem(d[0]);
    demoStart_ = new QPushButton("Start live demo");
    demoStop_ = new QPushButton("Stop");
    demoStatus_ = new QLabel;
    demoStatus_->setWordWrap(true);
    demoTable_ = new QTableWidget;
    NativeRunner::showEvents(demoTable_, {});
    demoTree_ = new QTreeWidget;
    demoTree_->setHeaderLabels({"Observed processes (PID)", "Role"});
    demoTree_->setMaximumWidth(260);
    auto* dW = new QWidget;
    auto* dl = new QVBoxLayout(dW);
    auto* dtop = new QHBoxLayout;
    auto* badge = new QLabel("<b>Live Linux Demo</b> - real fork/exec/wait/signal on the helper's own children; times are measured and vary.");
    badge->setWordWrap(true);
    dtop->addWidget(demo_);
    dtop->addWidget(demoStart_);
    dtop->addWidget(demoStop_);
    dtop->addWidget(badge, 1);
    dl->addLayout(dtop);
    dl->addWidget(demoStatus_);
    auto* dsplit = new QHBoxLayout;
    dsplit->addWidget(demoTable_, 3);
    dsplit->addWidget(demoTree_, 1);
    dl->addLayout(dsplit, 1);
    bottom_->addTab(dW, "Live Linux demo");

    assembleModule(this, header_, cfgW, titledBox("State diagram, CPU and queues", centerW),
                   titledBox("Process tree and PCB inspector", ins), bar_, bottom_);

    // ---- connections
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) rebuild(); });
    connect(policy_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) rebuild(); });
    connect(quantum_, &QSpinBox::valueChanged, this, [this] { if (!loading_) rebuild(); });
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    connect(work_, &QTableWidget::itemChanged, this, [this] {
        if (loading_) return;
        loading_ = true;
        preset_->setCurrentIndex(0);
        loading_ = false;
        rebuild();
    });
    connect(addRow, &QPushButton::clicked, this, [this] {
        std::vector<LcProcSpec> v;
        QString e;
        readWorkload(v, e);
        int pid = 2;
        for (const LcProcSpec& s : v) pid = std::max(pid, s.pid + 1);
        v.push_back({pid, 1, "new", 0, 1, {2}, 2});
        fillWorkload(v);
        rebuild();
    });
    connect(delRow, &QPushButton::clicked, this, [this] {
        int r = work_->currentRow() >= 0 ? work_->currentRow() : work_->rowCount() - 1;
        if (r < 0) return;
        work_->removeRow(r);
        rebuild();
    });
    connect(bottom_, &QTabWidget::currentChanged, this, [this](int i) { header_->setMode(i == 2); });
    connect(twoState_, &QCheckBox::toggled, view_, &LifecycleView::setTwoState);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (!it || loading_) return;
        int pid = it->data(0, Qt::UserRole).toInt();
        int i = target_->findData(pid);
        if (i >= 0) target_->setCurrentIndex(i);
        if (const LcSnapshot* s = shown()) updatePcb(*s);
    });
    connect(target_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_ && shown()) updatePcb(*shown()); });
    connect(bar_, &PlaybackBar::positionChanged, this, &ProcessTab::onStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });
    connect(demoStart_, &QPushButton::clicked, this, [this] { startDemo(demo_->currentText()); });
    connect(demoStop_, &QPushButton::clicked, this, &ProcessTab::stopDemo);
    connect(demo_, &QComboBox::currentIndexChanged, this, [this](int i) { demoStatus_->setText(kDemoText[i][1]); });
    connect(native_, &NativeRunner::event, this, [this](const NativeEvent&) {
        NativeRunner::showEvents(demoTable_, native_->events());
        // process tree from the observed PID / PPID pairs
        demoTree_->clear();
        QMap<int, QTreeWidgetItem*> items;
        QMap<int, QPair<int, QString>> seen;
        for (const NativeEvent& e : native_->events()) if (!seen.contains(e.pid)) seen[e.pid] = {e.ppid, e.role};
        for (auto it = seen.begin(); it != seen.end(); ++it) {
            auto* item = new QTreeWidgetItem({QString("PID %1").arg(it.key()), it.value().second});
            items[it.key()] = item;
        }
        for (auto it = seen.begin(); it != seen.end(); ++it) {
            if (items.contains(it.value().first)) items[it.value().first]->addChild(items[it.key()]);
            else demoTree_->addTopLevelItem(items[it.key()]);
        }
        demoTree_->expandAll();
    });
    connect(native_, &NativeRunner::finished, this, [this](int code, bool crashed) {
        demoStart_->setEnabled(true);
        demoStop_->setEnabled(false);
        QString t = crashed ? "the helper was stopped" : QString("helper exited with code %1").arg(code);
        demoStatus_->setText(demoStatus_->text() + "\nFinished: " + t + "; all children were reaped by the helper.");
        emit logMessage("Process live demo finished: " + t);
        emit demoFinished();
    });
    if (!NativeRunner::available()) {
        demoStart_->setEnabled(false);
        demoStatus_->setText("Live Linux demos are not available on this platform (needs Linux and the ossim_helper program). "
                             "The simulation above is unaffected.");
    } else {
        demoStatus_->setText(kDemoText[0][1]);
    }
    demoStop_->setEnabled(false);
    loadPreset(0);
}

// ---------------------------------------------------------------- workload

void ProcessTab::fillWorkload(const std::vector<LcProcSpec>& v) {
    bool old = loading_;
    loading_ = true;
    work_->setRowCount((int)v.size());
    for (int r = 0; r < (int)v.size(); ++r) {
        const LcProcSpec& s = v[r];
        QStringList b;
        for (int x : s.bursts) b << QString::number(x);
        QStringList c = {QString::number(s.pid), QString::number(s.ppid), QString::fromStdString(s.name), QString::number(s.arrival),
                         QString::number(s.priority), b.join(' '), QString::number(s.pages)};
        for (int k = 0; k < 7; ++k) {
            auto* it = new QTableWidgetItem(c[k]);
            it->setTextAlignment(Qt::AlignCenter);
            work_->setItem(r, k, it);
        }
    }
    loading_ = old;
}

bool ProcessTab::readWorkload(std::vector<LcProcSpec>& v, QString& err) const {
    v.clear();
    for (int r = 0; r < work_->rowCount(); ++r) {
        auto cell = [&](int k) { return work_->item(r, k) ? work_->item(r, k)->text().trimmed() : QString(); };
        bool ok[6];
        LcProcSpec s;
        s.pid = cell(0).toInt(&ok[0]);
        s.ppid = cell(1).toInt(&ok[1]);
        s.name = cell(2).toStdString();
        s.arrival = cell(3).toInt(&ok[2]);
        s.priority = cell(4).toInt(&ok[3]);
        s.pages = cell(6).toInt(&ok[4]);
        ok[5] = true;
        for (const QString& t : cell(5).split(' ', Qt::SkipEmptyParts)) {
            bool b = false;
            s.bursts.push_back(t.toInt(&b));
            ok[5] = ok[5] && b;
        }
        for (int k = 0; k < 6; ++k)
            if (!ok[k]) { err = QString("row %1: enter whole numbers (bursts separated by spaces)").arg(r + 1); return false; }
        v.push_back(s);
    }
    if (v.empty()) { err = "the workload is empty: add a process"; return false; }
    return true;
}

// New simulation from the inputs; the old history is discarded (playback back to step 0).
bool ProcessTab::rebuild() {
    std::vector<LcProcSpec> v;
    QString err;
    bool manual = mode_->currentIndex() == 1;
    manualBox_->setEnabled(manual);
    view_->setSnapshot(nullptr, nullptr, 0);
    timeline_->setTrace(nullptr, 0);
    sim_.reset();
    if (!readWorkload(v, err)) {
        bar_->setLength(0);
        header_->setStatus("Invalid workload: " + err, ui::semError());
        return false;
    }
    try {
        sim_ = std::make_unique<LifecycleSim>(v, (Algo)policy_->currentData().toInt(), quantum_->value(), manual);
    } catch (const std::exception& e) {
        bar_->setLength(0);
        header_->setStatus(QString("Invalid workload: %1").arg(e.what()), ui::semError());
        return false;
    }
    if (!manual) sim_->runAuto();
    log_->clear();
    for (const LcSnapshot& s : sim_->trace()) log_->appendPlainText(QString::fromStdString(s.text));
    bar_->setLength((int)sim_->trace().size() - 1);
    lastPcb_.clear();
    showSnapshot(manual ? bar_->length() : 0, false);
    if (manual) bar_->setPosition(bar_->length());
    emit logMessage(QString("Process lifecycle: %1 processes, %2, %3 mode, %4 events")
                        .arg(v.size()).arg(algoName(sim_->policy())).arg(manual ? "manual" : "automatic").arg(sim_->trace().size() - 1));
    return true;
}

void ProcessTab::onStep(int pos, bool forward) { showSnapshot(pos, forward); }

const LcSnapshot* ProcessTab::shown() const {
    if (!sim_) return nullptr;
    int pos = std::clamp(bar_->position(), 0, (int)sim_->trace().size() - 1);
    return &sim_->trace()[pos];
}

void ProcessTab::showSnapshot(int pos, bool forward) {
    if (!sim_) return;
    const auto& tr = sim_->trace();
    pos = std::clamp(pos, 0, (int)tr.size() - 1);
    const LcSnapshot& s = tr[pos];
    view_->setSnapshot(&s, pos > 0 ? &tr[pos - 1] : nullptr, forward ? bar_->animMs() : 0);
    timeline_->setTrace(&tr, pos);
    timelineScroll_->horizontalScrollBar()->setValue(timelineScroll_->horizontalScrollBar()->maximum());
    updateTree(s);
    updatePcb(s);
    QString ctx;
    if (s.event == LcEvent::Dispatch || s.event == LcEvent::Preempt || s.event == LcEvent::BlockIO)
        ctx = "\nContext switch: the CPU state of the old process (PC and registers - modelled values) is saved into its PCB, "
              "and the new process's PCB is loaded. This costs time but does no useful work.";
    explain_->setText("<b>Current event:</b> " + QString::fromStdString(s.text).toHtmlEscaped() + ctx.toHtmlEscaped());
    int running = 0, ready = 0, waiting = 0, done = 0;
    for (const LcPCB& p : s.pcbs) {
        running += p.state == ProcState::Running;
        ready += p.state == ProcState::Ready;
        waiting += p.state == ProcState::Waiting;
        done += p.state == ProcState::Terminated;
    }
    QString st = QString("t = %1 | event %2 of %3: %4 | Running %5, Ready %6, Waiting %7, Terminated %8 of %9")
                     .arg(s.clock).arg(pos).arg(tr.size() - 1).arg(lcEventName(s.event)).arg(running).arg(ready)
                     .arg(waiting).arg(done).arg(s.pcbs.size());
    if (pos == (int)tr.size() - 1 && sim_->allTerminated()) st += " | all processes terminated";
    header_->setStatus(st);
}

void ProcessTab::updateTree(const LcSnapshot& s) {
    bool old = loading_;
    loading_ = true;
    int keep = selectedPid();
    tree_->clear();
    QMap<int, QTreeWidgetItem*> items;
    auto* root = new QTreeWidgetItem({"init / shell (PID 1, modelled)", ""});
    tree_->addTopLevelItem(root);
    for (const LcPCB& p : s.pcbs) {
        auto* it = new QTreeWidgetItem({QString("P%1 %2").arg(p.pid).arg(QString::fromStdString(p.name)),
                                        QString(stateName(p.state)) + (p.killed ? " (killed)" : "")});
        it->setData(0, Qt::UserRole, p.pid);
        it->setForeground(1, lcStateColor(p.state).darker(110));
        items[p.pid] = it;
    }
    for (const LcPCB& p : s.pcbs) (items.contains(p.ppid) ? items[p.ppid] : root)->addChild(items[p.pid]);
    tree_->expandAll();
    target_->clear();
    for (const LcPCB& p : s.pcbs) target_->addItem(QString("P%1 (%2)").arg(p.pid).arg(stateName(p.state)), p.pid);
    int i = target_->findData(keep);
    if (i >= 0) target_->setCurrentIndex(i);
    else if (s.pid >= 0 && target_->findData(s.pid) >= 0) target_->setCurrentIndex(target_->findData(s.pid));
    loading_ = old;
}

int ProcessTab::selectedPid() const { return target_->currentIndex() >= 0 ? target_->currentData().toInt() : -1; }

// PCB of the selected process; fields that changed since the previous view are highlighted.
void ProcessTab::updatePcb(const LcSnapshot& s) {
    int pid = selectedPid();
    const LcPCB* p = nullptr;
    for (const LcPCB& x : s.pcbs) if (x.pid == pid) p = &x;
    if (!p) { pcb_->setRowCount(0); return; }
    QStringList bursts;
    for (size_t i = 0; i < p->bursts.size(); ++i)
        bursts << (int(i) == p->burstIndex ? QString("[%1%2]").arg(i % 2 ? "IO " : "CPU ").arg(p->bursts[i])
                                           : QString("%1%2").arg(i % 2 ? "IO " : "CPU ").arg(p->bursts[i]));
    std::vector<std::pair<QString, QString>> rows = {
        {"PID", QString::number(p->pid)}, {"PPID", QString::number(p->ppid)}, {"Name", QString::fromStdString(p->name)},
        {"State (5-state)", stateName(p->state)}, {"Two-state view", twoStateName(p->state)},
        {"Priority", QString::number(p->priority)}, {"Arrival", QString::number(p->arrival)},
        {"CPU used / total", QString("%1 / %2").arg(p->cpuUsed).arg(p->cpuTotal)},
        {"CPU burst left", QString::number(p->burstLeft)}, {"I/O left", QString::number(p->ioLeft)},
        {"Bursts", bursts.join(' ')},
        {"Program counter (modelled)", QString::number(p->pc)},
        {"Registers (modelled)", QString("R0=%1 R1=0x%2 R2=%3 SP=0x%4").arg(p->regs[0]).arg(p->regs[1], 0, 16).arg(p->regs[2]).arg(p->regs[3], 0, 16)},
        {"Waiting reason", p->waitReason.empty() ? "-" : QString::fromStdString(p->waitReason)},
        {"Memory (modelled)", QString("%1 pages").arg(p->pages)},
        {"Time in Ready / Waiting", QString("%1 / %2 ticks").arg(p->readyTicks).arg(p->waitTicks)},
        {"Dispatches", QString::number(p->dispatches)},
        {"First run / finish", QString("%1 / %2").arg(p->firstRun).arg(p->finish)},
    };
    if (pid != lastPcbPid_) lastPcb_.clear();
    pcb_->setRowCount((int)rows.size());
    QMap<QString, QString> now;
    for (int r = 0; r < (int)rows.size(); ++r) {
        auto* k = ui::roItem(rows[r].first);
        k->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* v = ui::roItem(rows[r].second);
        if (lastPcb_.contains(rows[r].first) && lastPcb_[rows[r].first] != rows[r].second) ui::shade(v, QColor("#fff1a8"));
        now[rows[r].first] = rows[r].second;
        pcb_->setItem(r, 0, k);
        pcb_->setItem(r, 1, v);
    }
    lastPcb_ = now;
    lastPcbPid_ = pid;
}

// ---------------------------------------------------------------- manual mode

QString ProcessTab::command(const QString& name, int pid, int arg) {
    if (!sim_) return "no valid workload";
    if (mode_->currentIndex() != 1) return "switch to Manual mode first";
    // A command after stepping back discards the later history (a new branch).
    if (bar_->position() < bar_->length()) sim_->truncate(bar_->position());
    std::string e;
    if (name == "Admit") e = sim_->admit(pid);
    else if (name == "Dispatch") e = sim_->dispatch(pid);
    else if (name == "Preempt") e = sim_->preempt();
    else if (name == "Block I/O") e = sim_->blockIO(arg);
    else if (name == "Complete I/O") e = sim_->completeIO(pid);
    else if (name == "Exit") e = sim_->exitRunning();
    else if (name == "Kill") e = sim_->kill(pid);
    else if (name == "Tick +1") e = sim_->tick();
    else if (name == "Create child") {
        int newPid = 2;
        for (const LcPCB& p : sim_->current().pcbs) newPid = std::max(newPid, p.pid + 1);
        LcProcSpec s{newPid, pid >= 2 ? pid : 1, "child", sim_->current().clock, 1, {arg}, 2};
        e = sim_->create(s);
    } else e = "unknown command";
    afterManual(QString::fromStdString(e));
    return QString::fromStdString(e);
}

void ProcessTab::afterManual(const QString& err) {
    log_->clear();
    for (const LcSnapshot& s : sim_->trace()) log_->appendPlainText(QString::fromStdString(s.text));
    int n = (int)sim_->trace().size() - 1;
    bar_->setLength(n);
    bar_->setPosition(n);
    if (!err.isEmpty()) header_->setStatus("Refused: " + err, ui::semError());
    else emit logMessage("Process (manual): " + QString::fromStdString(sim_->current().text));
}

bool ProcessTab::selectLiveDemo(const QString& key) {
    int i = demo_->findText(key);
    if (i < 0) return false;
    demo_->setCurrentIndex(i);
    return true;
}

QString ProcessTab::liveDemoText(const QString& key) {
    for (auto& d : kDemoText) if (key == d[0]) return d[1];
    return QString();
}

void ProcessTab::setManual(bool manual) { mode_->setCurrentIndex(manual ? 1 : 0); }

void ProcessTab::setPolicy(Algo a, int quantum) {
    loading_ = true;
    policy_->setCurrentIndex(std::max(0, policy_->findData((int)a)));
    quantum_->setValue(quantum);
    loading_ = false;
    rebuild();
}

bool ProcessTab::loadPreset(int index) {
    std::vector<LcPreset> p = lifecyclePresets();
    if (index < 0 || index >= (int)p.size()) return false;
    loading_ = true;
    fillWorkload(p[index].procs);
    policy_->setCurrentIndex(std::max(0, policy_->findData((int)p[index].policy)));
    quantum_->setValue(p[index].quantum);
    preset_->setCurrentIndex(index + 1);
    loading_ = false;
    bool ok = rebuild();
    if (ok) header_->setStatus(QString::fromStdString(p[index].description));
    return ok;
}

bool ProcessTab::loadFile(const QString& path) {
    try {
        std::vector<LcProcSpec> v = loadLifecycleCsv(path.toStdString());
        loading_ = true;
        fillWorkload(v);
        preset_->setCurrentIndex(0);
        loading_ = false;
        emit logMessage("Process lifecycle: loaded " + path);
        return rebuild();
    } catch (const std::exception& e) {
        ui::warn(this, QString("Could not load %1:\n%2").arg(path, e.what()));
        return false;
    }
}

// ---------------------------------------------------------------- live demo

bool ProcessTab::startDemo(const QString& demo) {
    int i = demo_->findText(demo);
    if (i < 0) return false;
    demo_->setCurrentIndex(i);
    if (!native_->start({demo})) return false;
    NativeRunner::showEvents(demoTable_, {});
    demoTree_->clear();
    demoStart_->setEnabled(false);
    demoStop_->setEnabled(true);
    demoStatus_->setText(kDemoText[i][1]);
    bottom_->setCurrentIndex(2);
    emit logMessage("Process live demo started: " + demo);
    return true;
}

void ProcessTab::stopDemo() { native_->stop(); }
bool ProcessTab::demoRunning() const { return native_->running(); }
int ProcessTab::demoExitCode() const { return native_->lastExitCode(); }
QString ProcessTab::statusText() const { return header_->status(); }

// ---------------------------------------------------------------- report

QString ProcessTab::reportText() const {
    if (!sim_) return "(no valid workload)\n";
    QString out = QString("Policy %1 (quantum %2), %3 events\n").arg(algoName(sim_->policy())).arg(sim_->quantum()).arg(sim_->trace().size() - 1);
    for (const LcPCB& p : sim_->current().pcbs)
        out += QString("P%1 (ppid %2, %3): %4, CPU %5/%6, ready %7, waiting %8, finish %9%10\n")
                   .arg(p.pid).arg(p.ppid).arg(QString::fromStdString(p.name)).arg(stateName(p.state)).arg(p.cpuUsed)
                   .arg(p.cpuTotal).arg(p.readyTicks).arg(p.waitTicks).arg(p.finish).arg(p.killed ? " (killed)" : "");
    out += "Transitions:\n";
    for (const LcSnapshot& s : sim_->trace())
        if (s.event != LcEvent::Execute && s.event != LcEvent::Idle) out += "  " + QString::fromStdString(s.text) + "\n";
    if (!native_->events().isEmpty()) {
        out += "Last Live Linux demo:\n";
        for (const NativeEvent& e : native_->events())
            out += QString("  %1 ms %2 pid %3 ppid %4 %5: %6\n").arg(e.ms).arg(e.role).arg(e.pid).arg(e.ppid).arg(e.event, e.detail);
    }
    return out;
}

QString ProcessTab::csvSection() const {
    QString out = "## Process lifecycle\nclock,event,pid,from,to,text\n";
    if (sim_)
        for (const LcSnapshot& s : sim_->trace()) {
            if (s.event == LcEvent::Execute || s.event == LcEvent::Idle) continue;
            QString t = QString::fromStdString(s.text);
            t.replace('"', '\'');
            out += QString("%1,%2,%3,%4,%5,\"%6\"\n").arg(s.clock).arg(lcEventName(s.event)).arg(s.pid)
                       .arg(stateName(s.from)).arg(stateName(s.to)).arg(t);
        }
    return out;
}
