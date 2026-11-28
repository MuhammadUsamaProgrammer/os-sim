// SchedulingTab.cpp - GUI for CPU scheduling. All algorithm work is done by core/
// (runScheduler / runAll); this class only collects input and displays results.
#include "SchedulingTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QRegularExpression>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include "PanelLayout.h"
#include "ModuleKit.h"
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <stdexcept>
#include "BarChartWidget.h"
#include "EngineBridge.h"
#include "GanttWidget.h"
#include "Style.h"
#include "io_formats.h"

static const QStringList kProcCols = {"PID", "Arrival", "Burst", "Priority", "Pages"};

// Builds the layout: inputs on the left, Gantt + result tabs on the right.
SchedulingTab::SchedulingTab(QWidget* parent) : QWidget(parent) {
    bridge_ = new EngineBridge(this);

    // ---- left: process table ----
    auto* procBox = new QGroupBox("Processes");
    procTable_ = new QTableWidget(0, kProcCols.size());
    procTable_->setHorizontalHeaderLabels(kProcCols);
    procTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    procTable_->verticalHeader()->setVisible(false);
    auto* addBtn = new QPushButton("Add");
    auto* remBtn = new QPushButton("Remove");
    auto* clrBtn = new QPushButton("Clear");
    auto* loadBtn = new QPushButton("Load CSV...");
    auto* saveBtn = new QPushButton("Save CSV...");
    auto* tblBtns = new QGridLayout;
    tblBtns->setSpacing(8);
    tblBtns->addWidget(addBtn, 0, 0); tblBtns->addWidget(remBtn, 0, 1); tblBtns->addWidget(clrBtn, 0, 2);
    tblBtns->addWidget(loadBtn, 1, 0, 1, 1); tblBtns->addWidget(saveBtn, 1, 1, 1, 1);
    auto* procLay = new QVBoxLayout(procBox);
    procLay->setSpacing(8);
    procLay->addWidget(procTable_);
    procLay->addLayout(tblBtns);

    // ---- left: algorithm controls ----
    auto* algoBox = new QGroupBox("Scheduler");
    algoCombo_ = new QComboBox;
    for (Algo a : allAlgos()) {
        algoCombo_->addItem(algoName(a), (int)a);
        algoCombo_->setItemData(algoCombo_->count() - 1, ui::algoExplanation(a), Qt::ToolTipRole);
    }
    quantumSpin_ = new QSpinBox;
    quantumSpin_->setRange(1, 100);
    quantumSpin_->setValue(4);
    runBtn_ = new QPushButton("Run");
    animBtn_ = new QPushButton("Run animated");
    compareBtn_ = new QPushButton("Compare all");
    pauseBtn_ = new QPushButton("Pause");
    stepBtn_ = new QPushButton("Step");
    stopBtn_ = new QPushButton("Stop");
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(50, 1000);
    speedSlider_->setValue(300);
    speedLabel_ = new QLabel("300 ms/tick");
    auto* g = new QGridLayout(algoBox);
    g->setSpacing(8);
    g->addWidget(new QLabel("Algorithm:"), 0, 0); g->addWidget(algoCombo_, 0, 1, 1, 2);
    g->addWidget(new QLabel("Quantum:"), 1, 0);   g->addWidget(quantumSpin_, 1, 1, 1, 2);
    g->addWidget(runBtn_, 2, 0); g->addWidget(animBtn_, 2, 1); g->addWidget(compareBtn_, 2, 2);
    g->addWidget(pauseBtn_, 3, 0); g->addWidget(stepBtn_, 3, 1); g->addWidget(stopBtn_, 3, 2);
    g->addWidget(new QLabel("Speed:"), 4, 0); g->addWidget(speedSlider_, 4, 1); g->addWidget(speedLabel_, 4, 2);

    // ---- left: integrated run (scheduling + memory + resources) ----
    auto* intBox = new QWidget;
    reqFileEdit_ = new QLineEdit(ui::samplesDir() + "/requests_integrated.csv");
    auto* browseBtn = new QPushButton("...");
    browseBtn->setMaximumWidth(30);
    totalEdit_ = new QLineEdit("1 1 2");
    totalEdit_->setToolTip("Total instances of each resource type (filled from '# total=' in the file)");
    intFramesSpin_ = new QSpinBox;
    intFramesSpin_->setRange(1, 64);
    intFramesSpin_->setValue(6);
    intPageCombo_ = new QComboBox;
    intPageCombo_->addItems({"FIFO", "LRU", "Optimal"});
    intPageCombo_->setCurrentIndex(1);
    avoidCheck_ = new QCheckBox("Deadlock avoidance (Banker's)");
    avoidCheck_->setChecked(true);
    recoverCheck_ = new QCheckBox("Auto-recover");
    recoverCheck_->setChecked(true);
    detectSpin_ = new QSpinBox;
    detectSpin_->setRange(1, 100);
    detectSpin_->setValue(5);
    intRunBtn_ = new QPushButton("Run integrated");
    auto* ig = new QGridLayout(intBox);
    ig->setSpacing(8);
    ig->addWidget(new QLabel("Requests:"), 0, 0);
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(reqFileEdit_); fileRow->addWidget(browseBtn);
    ig->addLayout(fileRow, 0, 1, 1, 3);
    ig->addWidget(new QLabel("Total res.:"), 1, 0); ig->addWidget(totalEdit_, 1, 1);
    ig->addWidget(new QLabel("Detect every"), 1, 2); ig->addWidget(detectSpin_, 1, 3);
    ig->addWidget(new QLabel("Frames:"), 2, 0); ig->addWidget(intFramesSpin_, 2, 1);
    ig->addWidget(new QLabel("Paging:"), 2, 2); ig->addWidget(intPageCombo_, 2, 3);
    ig->addWidget(avoidCheck_, 3, 0, 1, 3); ig->addWidget(recoverCheck_, 3, 3);
    ig->addWidget(new QLabel("<i>Uses the algorithm and quantum of the Scheduler page.</i>"), 4, 0, 1, 4);
    ig->addWidget(intRunBtn_, 5, 0, 1, 4);

    auto* leftTabs = new QTabWidget;
    leftTabs->addTab(algoBox, "Scheduler");
    leftTabs->addTab(intBox, "Integrated run");
    algoBox->setTitle("");
    algoBox->setFlat(true);

    auto* left = new QWidget;
    auto* leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(8);
    leftLay->addWidget(procBox, 1);
    leftLay->addWidget(leftTabs);

    connect(intRunBtn_, &QPushButton::clicked, this, [this] { runIntegrated(); });
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Scripted requests", ui::samplesDir(), "CSV files (*.csv)");
        if (!f.isEmpty()) setIntegratedOptions(f, avoidCheck_->isChecked(), recoverCheck_->isChecked(),
                                               intFramesSpin_->value(), detectSpin_->value());
    });

    // ---- right: Gantt chart (scrollable) ----
    auto* ganttBox = new QGroupBox("Gantt chart");
    gantt_ = new GanttWidget;
    auto* scroll = new QScrollArea;
    scroll->setWidget(gantt_);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(150);
    auto* gl = new QVBoxLayout(ganttBox);
    gl->addWidget(scroll);

    // ---- right: result tabs ----
    outTabs_ = new QTabWidget;
    auto* resWidget = new QWidget;
    resultTable_ = new QTableWidget(0, 7);
    resultTable_->setHorizontalHeaderLabels({"PID", "Arrival", "Burst", "Completion", "Waiting", "Turnaround", "Response"});
    resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable_->verticalHeader()->setVisible(false);
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summaryLabel_ = new QLabel("Run a scheduler to see the results.");
    auto* rl = new QVBoxLayout(resWidget);
    rl->addWidget(resultTable_);
    rl->addWidget(summaryLabel_);
    outTabs_->addTab(resWidget, "Results");

    stateTable_ = new QTableWidget(0, 3);
    stateTable_->setHorizontalHeaderLabels({"PID", "State", "Remaining"});
    stateTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    stateTable_->verticalHeader()->setVisible(false);
    stateTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    outTabs_->addTab(stateTable_, "Live process states");

    auto* cmpWidget = new QWidget;
    compareTable_ = new QTableWidget(0, 5);
    compareTable_->setHorizontalHeaderLabels({"Algorithm", "Avg WT", "Avg TAT", "Avg RT", "Context switches"});
    compareTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    compareTable_->horizontalHeader()->setStretchLastSection(true);
    compareTable_->verticalHeader()->setVisible(false);
    compareTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    chart_ = new BarChartWidget;
    auto* cmpSplit = new QSplitter(Qt::Horizontal);   // table left, chart right
    cmpSplit->addWidget(compareTable_);
    cmpSplit->addWidget(chart_);
    cmpSplit->setSizes({470, 430});
    auto* cl = new QVBoxLayout(cmpWidget);
    cl->addWidget(cmpSplit);
    outTabs_->addTab(cmpWidget, "Comparison");

    auto* rightSplit = new PanelSplitter(Qt::Vertical, "Scheduling/v");
    rightSplit->addPanel(ganttBox, PanelSplitter::Center, 1);
    rightSplit->addPanel(ConfigPanel::wrap(outTabs_, "detailPanel"), PanelSplitter::Detail, 2);

    auto* main = new PanelSplitter(Qt::Horizontal, "Scheduling/h");
    main->addPanel(new ConfigPanel(withSampleSlot(left)), PanelSplitter::Config, 1, 0.42);
    main->addPanel(rightSplit, PanelSplitter::Center);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(main);

    // ---- signals ----
    connect(addBtn, &QPushButton::clicked, this, &SchedulingTab::addRow);
    connect(remBtn, &QPushButton::clicked, this, &SchedulingTab::removeRow);
    connect(clrBtn, &QPushButton::clicked, this, &SchedulingTab::clearTable);
    connect(loadBtn, &QPushButton::clicked, this, &SchedulingTab::onLoadCsv);
    connect(saveBtn, &QPushButton::clicked, this, &SchedulingTab::onSaveCsv);
    connect(algoCombo_, &QComboBox::currentIndexChanged, this, &SchedulingTab::onAlgoChanged);
    connect(runBtn_, &QPushButton::clicked, this, [this] { runOnce(); });
    connect(animBtn_, &QPushButton::clicked, this, [this] { runAnimated(); });
    connect(compareBtn_, &QPushButton::clicked, this, [this] { compareAll(); });
    connect(pauseBtn_, &QPushButton::clicked, this, [this] {
        paused_ = !paused_;
        if (paused_) bridge_->pause(); else bridge_->resume();
        pauseBtn_->setText(paused_ ? "Resume" : "Pause");
    });
    connect(stepBtn_, &QPushButton::clicked, this, [this] {
        paused_ = true;
        pauseBtn_->setText("Resume");
        bridge_->step();
    });
    connect(stopBtn_, &QPushButton::clicked, this, &SchedulingTab::stopAnimation);
    connect(speedSlider_, &QSlider::valueChanged, this, [this](int v) {
        speedLabel_->setText(QString("%1 ms/tick").arg(v));
        bridge_->setDelay(v);
    });
    connect(procTable_, &QTableWidget::itemChanged, this, [this] { emit processesChanged(); });
    connect(bridge_, &EngineBridge::engineEvent, this, &SchedulingTab::onEngineEvent);
    connect(bridge_, &EngineBridge::engineFinished, this, &SchedulingTab::onEngineFinished);

    onAlgoChanged();
    setAnimControls(false);
}

Algo SchedulingTab::currentAlgo() const { return (Algo)algoCombo_->currentData().toInt(); }

void SchedulingTab::setAlgorithm(Algo a) { algoCombo_->setCurrentIndex(algoCombo_->findData((int)a)); }
void SchedulingTab::setQuantum(int q) { quantumSpin_->setValue(q); }

// The quantum only matters for Round Robin.
void SchedulingTab::onAlgoChanged() { quantumSpin_->setEnabled(currentAlgo() == Algo::RR); }

// Adds a row with the next free pid and sensible defaults.
void SchedulingTab::addRow() {
    int maxPid = 0;
    for (int r = 0; r < procTable_->rowCount(); ++r)
        if (auto* it = procTable_->item(r, 0)) maxPid = std::max(maxPid, it->text().toInt());
    int row = procTable_->rowCount();
    procTable_->blockSignals(true);
    procTable_->insertRow(row);
    QStringList vals = {QString::number(maxPid + 1), "0", "5", "1", "3"};
    for (int c = 0; c < vals.size(); ++c) {
        auto* it = new QTableWidgetItem(vals[c]);
        it->setTextAlignment(Qt::AlignCenter);
        procTable_->setItem(row, c, it);
    }
    procTable_->blockSignals(false);
    emit processesChanged();
}

void SchedulingTab::removeRow() {
    int row = procTable_->currentRow();
    if (row < 0) row = procTable_->rowCount() - 1;
    if (row >= 0) procTable_->removeRow(row);
    emit processesChanged();
}

void SchedulingTab::clearTable() {
    procTable_->setRowCount(0);
    emit processesChanged();
}

// Fills the table from PCBs.
void SchedulingTab::setProcesses(const std::vector<PCB>& procs) {
    procTable_->blockSignals(true);
    procTable_->setRowCount(0);
    for (const PCB& p : procs) {
        int row = procTable_->rowCount();
        procTable_->insertRow(row);
        int vals[] = {p.pid, p.arrival, p.burst, p.priority, p.pages};
        for (int c = 0; c < 5; ++c) {
            auto* it = new QTableWidgetItem(QString::number(vals[c]));
            it->setTextAlignment(Qt::AlignCenter);
            procTable_->setItem(row, c, it);
        }
    }
    procTable_->blockSignals(false);
    emit processesChanged();
}

// Converts the table into PCBs, checking every cell. Uses core validation too.
bool SchedulingTab::readProcesses(std::vector<PCB>& out, bool quiet) {
    out.clear();
    auto fail = [&](const QString& msg) { if (!quiet) ui::warn(this, msg); return false; };
    if (procTable_->rowCount() == 0) return fail("The process table is empty. Add processes or load a sample.");
    for (int r = 0; r < procTable_->rowCount(); ++r) {
        int v[5];
        for (int c = 0; c < 5; ++c) {
            QTableWidgetItem* it = procTable_->item(r, c);
            bool ok = false;
            v[c] = it ? it->text().trimmed().toInt(&ok) : 0;
            if (!ok) return fail(QString("Row %1, column %2: please enter a whole number.").arg(r + 1).arg(kProcCols[c]));
            if (v[c] < 0) return fail(QString("Row %1, column %2: negative values are not allowed.").arg(r + 1).arg(kProcCols[c]));
        }
        PCB p;
        p.pid = v[0]; p.arrival = v[1]; p.burst = v[2]; p.remaining = v[2]; p.priority = v[3]; p.pages = v[4];
        out.push_back(p);
    }
    try {
        validateProcesses(out, currentAlgo(), quantumSpin_->value());
    } catch (const std::exception& e) {
        return fail(QString("Invalid input: %1").arg(e.what()));
    }
    return true;
}

void SchedulingTab::clearResults() {
    stopAnimation();
    last_.reset();
    compare_.clear();
    integ_.reset();
    gantt_->setTimeline({});
    resultTable_->setRowCount(0);
    stateTable_->setRowCount(0);
    compareTable_->setRowCount(0);
    summaryLabel_->setText("Run a scheduler to see the results.");
    chart_->clear();
}

bool SchedulingTab::loadCsv(const QString& path) {
    try {
        setProcesses(loadProcessesCsv(path.toStdString()));
        emit logMessage("Loaded processes from " + path);
        return true;
    } catch (const std::exception& e) {
        ui::warn(this, QString("Could not load %1:\n%2").arg(path, e.what()));
        return false;
    }
}

bool SchedulingTab::saveCsv(const QString& path) {
    std::vector<PCB> procs;
    if (!readProcesses(procs)) return false;
    try {
        saveProcessesCsv(path.toStdString(), procs);
        emit logMessage("Saved processes to " + path);
        return true;
    } catch (const std::exception& e) {
        ui::warn(this, e.what());
        return false;
    }
}

void SchedulingTab::onLoadCsv() {
    QString f = QFileDialog::getOpenFileName(this, "Load processes", ui::samplesDir(), "CSV files (*.csv)");
    if (!f.isEmpty()) loadCsv(f);
}

void SchedulingTab::onSaveCsv() {
    QString f = QFileDialog::getSaveFileName(this, "Save processes", "processes.csv", "CSV files (*.csv)");
    if (!f.isEmpty()) saveCsv(f);
}

// Fills the results table (with an Averages row) and the summary line.
void SchedulingTab::showResult(const SchedResult& r) {
    resultTable_->setRowCount(0);
    for (const ProcMetrics& m : r.metrics) {
        int row = resultTable_->rowCount();
        resultTable_->insertRow(row);
        int vals[] = {m.pid, m.arrival, m.burst, m.completion, m.waiting, m.turnaround, m.response};
        for (int c = 0; c < 7; ++c)
            resultTable_->setItem(row, c, ui::roItem(c == 0 ? QString("P%1").arg(vals[0]) : QString::number(vals[c])));
        ui::shade(resultTable_->item(row, 0), ui::pidColor(m.pid).lighter(150));
    }
    int row = resultTable_->rowCount();
    resultTable_->insertRow(row);
    resultTable_->setItem(row, 0, ui::roItem("Averages"));
    for (int c = 1; c < 4; ++c) resultTable_->setItem(row, c, ui::roItem(""));
    resultTable_->setItem(row, 4, ui::roItem(QString::number(r.avgWaiting, 'f', 2)));
    resultTable_->setItem(row, 5, ui::roItem(QString::number(r.avgTurnaround, 'f', 2)));
    resultTable_->setItem(row, 6, ui::roItem(QString::number(r.avgResponse, 'f', 2)));
    QFont bold = font();
    bold.setBold(true);
    for (int c = 0; c < 7; ++c) resultTable_->item(row, c)->setFont(bold);
    summaryLabel_->setText(QString("<b>%1%2</b> &nbsp; avg WT = %3 &nbsp; avg TAT = %4 &nbsp; avg RT = %5 &nbsp; "
                                   "context switches = %6 &nbsp; CPU utilisation = %7%")
                               .arg(algoName(r.algo))
                               .arg(r.algo == Algo::RR ? QString(" (q=%1)").arg(r.quantum) : "")
                               .arg(r.avgWaiting, 0, 'f', 4).arg(r.avgTurnaround, 0, 'f', 4)
                               .arg(r.avgResponse, 0, 'f', 4).arg(r.contextSwitches)
                               .arg(cpuUtilization(r), 0, 'f', 1));
}

// Run: compute the whole schedule with core/ and show it at once.
bool SchedulingTab::runOnce() {
    std::vector<PCB> procs;
    if (!readProcesses(procs)) return false;
    stopAnimation();
    SchedResult r = runScheduler(currentAlgo(), procs, quantumSpin_->value());
    last_ = r;
    lastProcs_ = procs;
    gantt_->setTitle(QString("%1  (total time %2)").arg(algoName(r.algo)).arg(r.timeline.back().end));
    gantt_->setTimeline(r.timeline);
    showResult(r);
    outTabs_->setCurrentIndex(0);
    emit logMessage(QString("Scheduling: %1 -> avg WT %2, avg TAT %3, avg RT %4")
                        .arg(algoName(r.algo)).arg(r.avgWaiting, 0, 'f', 4)
                        .arg(r.avgTurnaround, 0, 'f', 4).arg(r.avgResponse, 0, 'f', 4));
    return true;
}

// Run animated: core/ computes the schedule, the pthread engine replays it tick by tick.
bool SchedulingTab::runAnimated(int delayMs) {
    std::vector<PCB> procs;
    if (!readProcesses(procs)) return false;
    stopAnimation();
    SchedResult r = runScheduler(currentAlgo(), procs, quantumSpin_->value());
    last_ = r;
    lastProcs_ = procs;
    gantt_->setTitle(QString("%1  (animated by the thread engine)").arg(algoName(r.algo)));
    gantt_->setTimeline(r.timeline, 0);
    resultTable_->setRowCount(0);
    summaryLabel_->setText("Animation running...");
    stateTable_->setRowCount(0);
    outTabs_->setCurrentIndex(1);
    paused_ = false;
    pauseBtn_->setText("Pause");
    setAnimControls(true);
    emit logMessage(QString("Engine: replaying %1 with %2 process threads + 1 clock thread")
                        .arg(algoName(r.algo)).arg(procs.size()));
    bridge_->start(procs, r.timeline, delayMs < 0 ? speedSlider_->value() : delayMs);
    refreshStateTable();
    return true;
}

void SchedulingTab::stopAnimation() {
    if (bridge_->running()) bridge_->stop();
    setAnimControls(false);
}

bool SchedulingTab::animationRunning() const { return bridge_->running(); }

// Used when the tab is hidden from the workspace (Modules menu): the engine threads wait on the
// pause condition variable, the state is kept and the user resumes manually.
void SchedulingTab::pauseAnimation() {
    if (!bridge_->running() || paused_) return;
    paused_ = true;
    bridge_->pause();
    pauseBtn_->setText("Resume");
}

void SchedulingTab::setAnimControls(bool running) {
    pauseBtn_->setEnabled(running);
    stepBtn_->setEnabled(running);
    stopBtn_->setEnabled(running);
    runBtn_->setEnabled(!running);
    compareBtn_->setEnabled(!running);
    intRunBtn_->setEnabled(!running);
}

// Shows each process's state and remaining time from a thread-safe engine snapshot.
void SchedulingTab::refreshStateTable() {
    EngineSnapshot s = bridge_->snapshot();
    stateTable_->setRowCount((int)s.procs.size());
    for (int i = 0; i < (int)s.procs.size(); ++i) {
        const ProcSnapshot& p = s.procs[i];
        stateTable_->setItem(i, 0, ui::roItem(QString("P%1").arg(p.pid)));
        auto* st = ui::roItem(stateName(p.state));
        if (p.state == ProcState::Running) ui::shade(st, ui::pidColor(p.pid).lighter(130));
        else if (p.state == ProcState::Terminated) ui::shade(st, ui::idleColor());
        stateTable_->setItem(i, 1, st);
        stateTable_->setItem(i, 2, ui::roItem(QString::number(p.remaining)));
    }
}

// Called in the GUI thread for every engine log line (queued signal).
void SchedulingTab::onEngineEvent(int tick, int pid, int state, const QString& text) {
    emit logMessage(text);
    bool executed = (pid >= 0 && (state == (int)ProcState::Running || state == (int)ProcState::Terminated) &&
                     !text.contains("arrived") && !text.contains("preempted")) || text.contains("CPU idle");
    if (executed) gantt_->setVisibleUntil(tick + 1);
    refreshStateTable();
}

// Animation done: show the metrics and check the engine against core/.
void SchedulingTab::onEngineFinished() {
    setAnimControls(false);
    refreshStateTable();
    if (!last_) return;
    std::vector<PCB> fin = bridge_->pcbs();
    bool complete = true, match = true;
    for (const PCB& p : fin) {
        if (p.state != ProcState::Terminated) complete = false;
        for (const ProcMetrics& m : last_->metrics)
            if (m.pid == p.pid && p.finish != m.completion) match = false;
    }
    if (complete) {
        gantt_->setVisibleUntil(-1);
        showResult(*last_);
        emit logMessage(QString("Engine finished: finish times %1 core/ completion times")
                            .arg(match ? "MATCH" : "DO NOT MATCH"));
    } else {
        summaryLabel_->setText("Animation stopped before the end.");
        emit logMessage("Engine stopped by the user");
    }
    emit animationFinished();
}

// Runs all eight standard algorithms; best value of each column in bold green; bar chart.
bool SchedulingTab::compareAll() {
    std::vector<PCB> procs;
    if (!readProcesses(procs)) return false;
    stopAnimation();
    compare_ = runAll(procs, quantumSpin_->value());
    lastProcs_ = procs;
    compareTable_->setRowCount((int)compare_.size());
    double best[4] = {1e18, 1e18, 1e18, 1e18};
    for (const SchedResult& r : compare_) {
        double v[4] = {r.avgWaiting, r.avgTurnaround, r.avgResponse, (double)r.contextSwitches};
        for (int k = 0; k < 4; ++k) best[k] = std::min(best[k], v[k]);
    }
    QStringList cats;
    std::vector<std::vector<double>> vals(3);
    QFont bold = font();
    bold.setBold(true);
    for (int i = 0; i < (int)compare_.size(); ++i) {
        const SchedResult& r = compare_[i];
        QString name = algoName(r.algo);
        if (r.algo == Algo::RR) name += QString(" q=%1").arg(r.quantum);
        cats << name;
        compareTable_->setItem(i, 0, ui::roItem(name));
        double v[4] = {r.avgWaiting, r.avgTurnaround, r.avgResponse, (double)r.contextSwitches};
        for (int k = 0; k < 4; ++k) {
            auto* it = ui::roItem(k == 3 ? QString::number(r.contextSwitches) : QString::number(v[k], 'f', 2));
            if (std::abs(v[k] - best[k]) < 1e-9) { it->setFont(bold); it->setForeground(ui::hitColor().darker(130)); }
            compareTable_->setItem(i, k + 1, it);
        }
        vals[0].push_back(r.avgWaiting); vals[1].push_back(r.avgTurnaround); vals[2].push_back(r.avgResponse);
    }
    chart_->setData("Average times per algorithm (lower is better)", cats, {"Avg WT", "Avg TAT", "Avg RT"}, vals,
                    {QColor("#4e79a7"), QColor("#f28e2b"), QColor("#59a14f")});
    outTabs_->setCurrentIndex(2);
    emit logMessage(QString("Compared %1 algorithms on %2 processes").arg(compare_.size()).arg(procs.size()));
    return true;
}

// Sets the integrated-run inputs; reads "# total=" from the requests file if present.
void SchedulingTab::setIntegratedOptions(const QString& requestsFile, bool avoidance, bool autoRecover,
                                         int frames, int k) {
    reqFileEdit_->setText(requestsFile);
    avoidCheck_->setChecked(avoidance);
    recoverCheck_->setChecked(autoRecover);
    intFramesSpin_->setValue(frames);
    detectSpin_->setValue(k);
    try {
        Vec total;
        loadRequestsCsv(requestsFile.toStdString(), &total);
        if (!total.empty()) totalEdit_->setText(QString::fromStdString(vecToString(total)));
    } catch (const std::exception&) {}                         // reported when the run starts
}

// Integrated run: core/ simulates scheduling + paging + resources tick by tick.
bool SchedulingTab::runIntegrated() {
    std::vector<PCB> procs;
    if (!readProcesses(procs)) return false;
    stopAnimation();
    IntegratedConfig cfg;
    cfg.algo = currentAlgo();
    cfg.quantum = quantumSpin_->value();
    cfg.numFrames = intFramesSpin_->value();
    cfg.pageAlgo = (PageAlgo)intPageCombo_->currentIndex();
    cfg.avoidance = avoidCheck_->isChecked();
    cfg.autoRecover = recoverCheck_->isChecked();
    cfg.detectInterval = detectSpin_->value();
    try {
        Vec fileTotal;
        cfg.requests = loadRequestsCsv(reqFileEdit_->text().toStdString(), &fileTotal);
        for (const QString& p : totalEdit_->text().simplified().split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts)) {
            bool ok = false;
            int v = p.toInt(&ok);
            if (!ok || v < 0) throw std::invalid_argument("total resources: enter whole numbers >= 0, e.g. 1 1 2");
            cfg.totalRes.push_back(v);
        }
        if (cfg.totalRes.empty()) cfg.totalRes = fileTotal;
        if (cfg.totalRes.empty() && !cfg.requests.empty()) throw std::invalid_argument("enter the total resources");
        if (cfg.requests.empty()) cfg.totalRes.clear();
        integ_ = ::runIntegrated(procs, cfg);
    } catch (const std::exception& e) {
        ui::warn(this, QString("Integrated run: %1").arg(e.what()));
        return false;
    }
    lastProcs_ = procs;
    const IntegratedResult& r = *integ_;
    emit logMessage(QString("---- Integrated run: %1%2, %3 frames %4, avoidance %5, detect every %6 ticks ----")
                        .arg(algoName(cfg.algo)).arg(cfg.algo == Algo::RR ? QString(" q=%1").arg(cfg.quantum) : "")
                        .arg(cfg.numFrames).arg(pageAlgoName(cfg.pageAlgo))
                        .arg(cfg.avoidance ? "ON" : "OFF").arg(cfg.detectInterval));
    for (const std::string& l : r.log) emit logMessage(QString::fromStdString(l));

    gantt_->setTitle(QString("Integrated run (%1)").arg(algoName(cfg.algo)));
    gantt_->setTimeline(r.timeline);
    // Results table: killed processes are marked
    resultTable_->setRowCount(0);
    for (const ProcMetrics& m : r.metrics) {
        int row = resultTable_->rowCount();
        resultTable_->insertRow(row);
        bool killed = std::find(r.killedPids.begin(), r.killedPids.end(), m.pid) != r.killedPids.end();
        QStringList v = {QString("P%1").arg(m.pid), QString::number(m.arrival), QString::number(m.burst),
                         killed ? QString("killed @%1").arg(m.completion) : QString::number(m.completion),
                         killed ? "-" : QString::number(m.waiting), killed ? "-" : QString::number(m.turnaround),
                         QString::number(m.response)};
        for (int c = 0; c < 7; ++c) resultTable_->setItem(row, c, ui::roItem(v[c]));
        ui::shade(resultTable_->item(row, 0), killed ? ui::lightRed() : ui::pidColor(m.pid).lighter(150));
    }
    summaryLabel_->setText(QString("<b>Integrated</b> &nbsp; avg WT = %1 &nbsp; avg TAT = %2 (completed processes) "
                                   "&nbsp;|&nbsp; memory: %3 faults, %4 hits, hit ratio %5 &nbsp;|&nbsp; requests: "
                                   "%6 granted, %7 denied &nbsp;|&nbsp; <span style='color:%8'>deadlocks detected: %9%10</span>")
                               .arg(r.avgWaiting, 0, 'f', 2).arg(r.avgTurnaround, 0, 'f', 2)
                               .arg(r.memFaults).arg(r.memHits).arg(r.hitRatio, 0, 'f', 3)
                               .arg(r.requestsGranted).arg(r.requestsDenied)
                               .arg(r.deadlocksDetected ? ui::faultColor().name() : ui::hitColor().darker(130).name())
                               .arg(r.deadlocksDetected)
                               .arg(r.killedPids.empty() ? QString() : QString(", killed: P%1").arg([&] {
                                   QStringList l; for (int p : r.killedPids) l << QString::number(p);
                                   return l.join(", P"); }())));
    summaryLabel_->setWordWrap(true);
    outTabs_->setCurrentIndex(0);
    if (r.firstDeadlock.tick >= 0) {
        std::vector<int> pids;
        for (const PCB& p : procs) pids.push_back(p.pid);
        emit integratedDeadlock(pids, r.firstDeadlock);
    }
    return true;
}
