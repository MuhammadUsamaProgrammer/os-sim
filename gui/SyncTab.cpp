// SyncTab.cpp - controls, process cards, animation loop, comparison and real demo page.
#include "SyncTab.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <stdexcept>
#include "BarChartWidget.h"
#include "Style.h"
#include "SyncBridge.h"
#include "SyncViews.h"

static QSpinBox* makeSpin(int lo, int hi, int v, const QString& tip = QString()) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    if (!tip.isEmpty()) s->setToolTip(tip);
    return s;
}

static QString colorText(const QString& text, const QColor& c) {
    return QString("<span style='color:%1'>%2</span>").arg(c.name(), text.toHtmlEscaped());
}

static QColor stateColor(SyncProcState s) {
    switch (s) {
        case SyncProcState::Running: return ui::hitColor();
        case SyncProcState::Spinning: return QColor("#f28e2b");
        case SyncProcState::Blocked: return ui::faultColor();
        case SyncProcState::Done: return ui::idleColor();
        default: return QColor("#4e79a7");
    }
}

static bool isBuffer(SyncAlgo a) { return a == SyncAlgo::PRODUCER_CONSUMER || a == SyncAlgo::MONITOR; }

SyncTab::SyncTab(QWidget* parent) : QWidget(parent) {
    timer_ = new QTimer(this);
    bridge_ = new SyncBridge(this);

    // ================= left column: algorithm, parameters, controls, explanation
    algoCombo_ = new QComboBox;
    for (int a = 0; a <= (int)SyncAlgo::MONITOR; ++a) algoCombo_->addItem(syncAlgoName((SyncAlgo)a));
    scenarioCombo_ = new QComboBox;
    scenarioDesc_ = new QLabel;
    scenarioDesc_->setWordWrap(true);
    auto* algoBox = new QGroupBox("Algorithm and sample scenario");
    auto* al = new QVBoxLayout(algoBox);
    al->setSpacing(6);
    al->addWidget(algoCombo_);
    al->addWidget(new QLabel("Sample scenario:"));
    al->addWidget(scenarioCombo_);
    al->addWidget(scenarioDesc_);

    procSpin_ = makeSpin(1, 8, 2, "Number of processes");
    iterSpin_ = makeSpin(1, 50, 2, "How many times each process runs its code (items per producer)");
    resSpin_ = makeSpin(1, 8, 2, "Counting semaphore: initial value S = number of resource instances");
    prodSpin_ = makeSpin(1, 6, 1);
    consSpin_ = makeSpin(1, 6, 1);
    capSpin_ = makeSpin(1, 12, 3, "Buffer capacity N");
    prodSpeed_ = makeSpin(1, 20, 1, "Producer speed: instructions per turn");
    consSpeed_ = makeSpin(1, 20, 1, "Consumer speed: instructions per turn");
    sliceSpin_ = makeSpin(1, 20, 1, "Instructions a process runs before the next process gets the CPU");
    orderCombo_ = new QComboBox;
    orderCombo_->addItems({"Round robin", "Random (seeded)"});
    seedSpin_ = makeSpin(1, 99999, 1, "Seed of the random order (same seed = same run)");
    auto* paramBox = new QGroupBox("Parameters");
    paramForm_ = new QFormLayout(paramBox);
    paramForm_->setSpacing(6);
    paramForm_->addRow("Processes:", procSpin_);
    paramForm_->addRow("Iterations / items:", iterSpin_);
    paramForm_->addRow("Resource instances (S):", resSpin_);
    paramForm_->addRow("Producers:", prodSpin_);
    paramForm_->addRow("Consumers:", consSpin_);
    paramForm_->addRow("Buffer capacity N:", capSpin_);
    paramForm_->addRow("Producer speed:", prodSpeed_);
    paramForm_->addRow("Consumer speed:", consSpeed_);
    paramForm_->addRow("Instructions per turn:", sliceSpin_);
    paramForm_->addRow("Interleaving:", orderCombo_);
    paramForm_->addRow("Seed:", seedSpin_);

    runBtn_ = new QPushButton("Run");
    pauseBtn_ = new QPushButton("Pause");
    resumeBtn_ = new QPushButton("Resume");
    stepBtn_ = new QPushButton("Step");
    stopBtn_ = new QPushButton("Stop");
    resetBtn_ = new QPushButton("Reset");
    endBtn_ = new QPushButton("Run to end");
    stepBtn_->setToolTip("Execute ONE instruction of the process chosen by the scheduler.\n"
                         "Use a process card's Step button to choose the process yourself.");
    endBtn_->setToolTip("Execute all remaining instructions at once");
    speed_ = new QSlider(Qt::Horizontal);
    speed_->setRange(50, 1000);
    speed_->setValue(450);
    speedLabel_ = new QLabel;
    auto* ctlBox = new QGroupBox("Controls");
    auto* cg = new QGridLayout(ctlBox);
    cg->setSpacing(6);
    cg->addWidget(runBtn_, 0, 0); cg->addWidget(pauseBtn_, 0, 1); cg->addWidget(resumeBtn_, 0, 2);
    cg->addWidget(stepBtn_, 1, 0); cg->addWidget(stopBtn_, 1, 1); cg->addWidget(resetBtn_, 1, 2);
    cg->addWidget(endBtn_, 2, 0, 1, 3);
    cg->addWidget(new QLabel("Speed:"), 3, 0);
    cg->addWidget(speed_, 3, 1);
    cg->addWidget(speedLabel_, 3, 2);

    explain_ = new QTextBrowser;
    explain_->setMinimumHeight(170);
    auto* expBox = new QGroupBox("How it works");
    (new QVBoxLayout(expBox))->addWidget(explain_);

    auto* leftW = new QWidget;
    auto* ll = new QVBoxLayout(leftW);
    ll->setContentsMargins(0, 0, 4, 0);
    ll->setSpacing(8);
    ll->addWidget(algoBox);
    ll->addWidget(paramBox);
    ll->addWidget(ctlBox);
    ll->addWidget(expBox, 1);
    auto* leftScroll = new QScrollArea;
    leftScroll->setWidget(leftW);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setMinimumWidth(300);
    leftScroll->setMaximumWidth(380);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // ================= right: status, process cards, scene / code / variables, bottom pages
    status_ = new QLabel;
    status_->setWordWrap(true);
    QFont big = status_->font();
    big.setBold(true);
    status_->setFont(big);
    result_ = new QLabel;
    result_->setWordWrap(true);
    result_->setTextFormat(Qt::RichText);

    auto* cardHost = new QWidget;
    cardRow_ = new QHBoxLayout(cardHost);
    cardRow_->setContentsMargins(0, 0, 0, 0);
    cardRow_->setSpacing(8);
    cardRow_->addStretch();
    auto* cardScroll = new QScrollArea;
    cardScroll->setWidget(cardHost);
    cardScroll->setWidgetResizable(true);
    cardScroll->setFrameShape(QFrame::NoFrame);
    cardScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cardScroll->setMinimumHeight(78);
    cardScroll->setMaximumHeight(92);

    auto* topW = new QWidget;
    auto* tl = new QVBoxLayout(topW);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(4);
    tl->addWidget(status_);
    tl->addWidget(result_);

    view_ = new SyncView;
    auto* viewBox = new QGroupBox("Critical section, waiting queues and shared resources");
    (new QVBoxLayout(viewBox))->addWidget(view_);
    code_ = new CodeView;
    auto* codeScroll = new QScrollArea;
    codeScroll->setWidget(code_);
    codeScroll->setWidgetResizable(true);
    codeScroll->setFrameShape(QFrame::NoFrame);
    auto* codeBox = new QGroupBox("Pseudocode (chip = next line, colour = line just executed)");
    (new QVBoxLayout(codeBox))->addWidget(codeScroll);
    vars_ = new QTableWidget(0, 2);
    vars_->setHorizontalHeaderLabels({"Variable", "Value"});
    vars_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    vars_->horizontalHeader()->setStretchLastSection(true);
    vars_->verticalHeader()->setVisible(false);
    vars_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* varBox = new QGroupBox("Shared variables (live)");
    (new QVBoxLayout(varBox))->addWidget(vars_);
    // Left: process cards above the animated scene. Right: pseudocode above the variables.
    auto* sceneW = new QWidget;
    auto* scl = new QVBoxLayout(sceneW);
    scl->setContentsMargins(0, 0, 0, 0);
    scl->setSpacing(4);
    scl->addWidget(cardScroll);
    scl->addWidget(viewBox, 1);
    auto* codeVars = new QSplitter(Qt::Vertical);
    codeVars->addWidget(codeBox);
    codeVars->addWidget(varBox);
    codeVars->setStretchFactor(0, 3);
    codeVars->setStretchFactor(1, 2);
    codeVars->setSizes({330, 170});
    auto* middle = new QSplitter(Qt::Horizontal);
    middle->addWidget(sceneW);
    middle->addWidget(codeVars);
    middle->setStretchFactor(0, 3);
    middle->setStretchFactor(1, 2);
    middle->setSizes({580, 400});

    // ---- bottom pages
    bottom_ = new QTabWidget;
    timeline_ = new SyncTimeline;
    timelineScroll_ = new QScrollArea;
    timelineScroll_->setWidget(timeline_);
    timelineScroll_->setWidgetResizable(false);
    bottom_->addTab(timelineScroll_, "Execution timeline");

    events_ = new QPlainTextEdit;
    events_->setReadOnly(true);
    events_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    events_->setMaximumBlockCount(20000);
    bottom_->addTab(events_, "Event log");

    stats_ = new QTableWidget(0, 2);
    stats_->setHorizontalHeaderLabels({"Measure", "Value"});
    stats_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    stats_->horizontalHeader()->setStretchLastSection(true);
    stats_->verticalHeader()->setVisible(false);
    stats_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    guarantee_ = new QLabel;
    guarantee_->setWordWrap(true);
    guarantee_->setTextFormat(Qt::RichText);
    auto* statW = new QWidget;
    auto* sl = new QHBoxLayout(statW);
    sl->addWidget(stats_, 3);
    sl->addWidget(guarantee_, 2);
    bottom_->addTab(statW, "Statistics: expected vs actual");

    // Comparison page
    cmpIter_ = makeSpin(1, 20, 3, "Increments per process");
    cmpSlice_ = makeSpin(1, 10, 1, "Instructions per turn");
    cmpOrder_ = new QComboBox;
    cmpOrder_->addItems({"Round robin", "Random (seeded)"});
    cmpSeed_ = makeSpin(1, 99999, 1);
    auto* cmpBtn = new QPushButton("Compare");
    auto* cmpTop = new QHBoxLayout;
    cmpTop->setSpacing(8);
    cmpTop->addWidget(new QLabel("2 processes, increments each:"));
    cmpTop->addWidget(cmpIter_);
    cmpTop->addWidget(new QLabel("Instructions per turn:"));
    cmpTop->addWidget(cmpSlice_);
    cmpTop->addWidget(cmpOrder_);
    cmpTop->addWidget(new QLabel("Seed:"));
    cmpTop->addWidget(cmpSeed_);
    cmpTop->addWidget(cmpBtn);
    cmpTop->addStretch();
    cmpTable_ = new QTableWidget(0, 11);
    cmpTable_->setHorizontalHeaderLabels({"Mechanism", "Expected", "Actual", "Lost updates", "CS violations",
                                          "Waiting steps", "Busy-wait", "Blocked", "CS entries", "Completed",
                                          "Theoretical guarantee (every interleaving)"});
    cmpTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    cmpTable_->horizontalHeader()->setStretchLastSection(true);
    cmpTable_->verticalHeader()->setVisible(false);
    cmpTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cmpChart_ = new BarChartWidget;
    auto* cmpNote = new QLabel(
        "<i>Observed</i> columns come from ONE deterministic interleaving of the same workload. "
        "The <i>theoretical guarantee</i> holds for every possible interleaving. An unsynchronized run "
        "that happens to lose no update is still incorrect: another interleaving loses updates.");
    cmpNote->setWordWrap(true);
    auto* cmpW = new QWidget;
    auto* cl = new QVBoxLayout(cmpW);
    cl->addLayout(cmpTop);
    auto* cmpSplit = new QHBoxLayout;
    cmpSplit->addWidget(cmpTable_, 3);
    cmpSplit->addWidget(cmpChart_, 2);
    cl->addLayout(cmpSplit, 1);
    cl->addWidget(cmpNote);
    auto scrolled = [](QWidget* w) {        // pages scroll instead of squeezing the scene above
        auto* sa = new QScrollArea;
        sa->setWidget(w);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        return sa;
    };
    cmpW->setMinimumHeight(300);
    bottom_->addTab(scrolled(cmpW), "Comparison");

    // Real POSIX demo page
    demoKind_ = new QComboBox;
    for (int k = 0; k <= (int)SyncDemoKind::MONITOR; ++k) demoKind_->addItem(syncDemoName((SyncDemoKind)k));
    dThreads_ = makeSpin(1, 16, 4);
    dIter_ = makeSpin(1, 1000000, 20000);
    dRes_ = makeSpin(1, 16, 2);
    dProd_ = makeSpin(1, 8, 2);
    dCons_ = makeSpin(1, 8, 2);
    dCap_ = makeSpin(1, 64, 3);
    dItems_ = makeSpin(1, 500, 8);
    dDelay_ = makeSpin(0, 200, 0, "Sleep (ms) inside the critical section / after every item");
    demoRun_ = new QPushButton("Run real threads");
    demoStop_ = new QPushButton("Stop");
    auto* demoForm = new QWidget;
    demoForm_ = new QFormLayout(demoForm);
    demoForm_->setSpacing(6);
    demoForm_->addRow("Primitive:", demoKind_);
    demoForm_->addRow("Threads:", dThreads_);
    demoForm_->addRow("Iterations per thread:", dIter_);
    demoForm_->addRow("Semaphore value S:", dRes_);
    demoForm_->addRow("Producers:", dProd_);
    demoForm_->addRow("Consumers:", dCons_);
    demoForm_->addRow("Buffer capacity:", dCap_);
    demoForm_->addRow("Items per producer:", dItems_);
    demoForm_->addRow("Delay (ms):", dDelay_);
    auto* db = new QHBoxLayout;
    db->addWidget(demoRun_);
    db->addWidget(demoStop_);
    demoForm_->addRow(db);
    demoResult_ = new QLabel(
        "Runs the primitive with real POSIX threads (pthread_create / join, pthread_mutex, sem_init / sem_wait / "
        "sem_post / sem_destroy, pthread_cond, std::atomic seq_cst). The interleaving is chosen by the OS, so it "
        "differs between runs; the correctness checks must hold every time.");
    demoResult_->setWordWrap(true);
    demoResult_->setTextFormat(Qt::RichText);
    demoLog_ = new QPlainTextEdit;
    demoLog_->setReadOnly(true);
    demoLog_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    demoLog_->setMaximumBlockCount(5000);
    auto* demoW = new QWidget;
    auto* dl = new QHBoxLayout(demoW);
    dl->addWidget(demoForm, 2);
    auto* dr = new QVBoxLayout;
    dr->addWidget(demoResult_);
    dr->addWidget(demoLog_, 1);
    dl->addLayout(dr, 3);
    demoW->setMinimumHeight(300);
    bottom_->addTab(scrolled(demoW), "Real POSIX demo");

    auto* right = new QSplitter(Qt::Vertical);
    right->addWidget(topW);
    right->addWidget(middle);
    right->addWidget(bottom_);
    right->setStretchFactor(0, 0);
    right->setStretchFactor(1, 3);
    right->setStretchFactor(2, 2);
    right->setSizes({44, 500, 170});

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(leftScroll);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(split);

    // ================= connections
    connect(algoCombo_, &QComboBox::currentIndexChanged, this, &SyncTab::onAlgoChanged);
    connect(scenarioCombo_, &QComboBox::currentIndexChanged, this, &SyncTab::onScenarioChanged);
    for (QSpinBox* s : {procSpin_, iterSpin_, resSpin_, prodSpin_, consSpin_, capSpin_, prodSpeed_, consSpeed_,
                        sliceSpin_, seedSpin_})
        connect(s, &QSpinBox::valueChanged, this, &SyncTab::onParamChanged);
    connect(orderCombo_, &QComboBox::currentIndexChanged, this, &SyncTab::onParamChanged);
    connect(runBtn_, &QPushButton::clicked, this, &SyncTab::run);
    connect(pauseBtn_, &QPushButton::clicked, this, &SyncTab::pause);
    connect(resumeBtn_, &QPushButton::clicked, this, &SyncTab::resume);
    connect(stepBtn_, &QPushButton::clicked, this, [this] { stepOnce(); });
    connect(stopBtn_, &QPushButton::clicked, this, &SyncTab::stop);
    connect(resetBtn_, &QPushButton::clicked, this, [this] { resetSim(false); });
    connect(endBtn_, &QPushButton::clicked, this, [this] { runToEnd(); });
    connect(speed_, &QSlider::valueChanged, this, [this](int v) {
        speedLabel_->setText(QString("%1 ms").arg(v));
        timer_->setInterval(v);
    });
    connect(timer_, &QTimer::timeout, this, &SyncTab::onTimer);
    connect(cmpBtn, &QPushButton::clicked, this, [this] { compare(); });
    connect(demoKind_, &QComboBox::currentIndexChanged, this, [this](int k) { setDemoDefaults(k); });
    connect(demoRun_, &QPushButton::clicked, this, [this] { runRealDemo(); });
    connect(demoStop_, &QPushButton::clicked, this, &SyncTab::stopRealDemo);
    connect(bridge_, &SyncBridge::line, this, &SyncTab::onDemoLine);
    connect(bridge_, &SyncBridge::finished, this, &SyncTab::onDemoFinished);

    speedLabel_->setText(QString("%1 ms").arg(speed_->value()));
    timer_->setInterval(speed_->value());
    onAlgoChanged();
}

SyncTab::~SyncTab() {
    timer_->stop();
    bridge_->stop();
    view_->setSim(nullptr);
    code_->setSim(nullptr);
    timeline_->setSim(nullptr);
}

SyncAlgo SyncTab::algorithm() const { return (SyncAlgo)algoCombo_->currentIndex(); }

// ---------------------------------------------------------------- configuration

// Fills the scenario list, shows only the parameters this algorithm uses, sets the text.
void SyncTab::prepareAlgo(SyncAlgo a) {
    scenarios_ = syncScenarios(a);
    loading_ = true;
    algoCombo_->setCurrentIndex((int)a);
    scenarioCombo_->clear();
    scenarioCombo_->addItem("Custom settings");
    for (const SyncScenario& s : scenarios_) scenarioCombo_->addItem(QString::fromStdString(s.name));
    loading_ = false;
    updateParamVisibility();
    QString text = QString::fromStdString(syncExplanation(a)).toHtmlEscaped();
    QStringList paras = text.split('\n');
    QString html = "<h3>" + paras.value(0) + "</h3>";
    for (int i = 1; i < paras.size(); ++i) html += "<p>" + paras[i] + "</p>";
    html += "<p><b>Guarantee:</b> " + QString::fromStdString(syncGuarantee(a)).toHtmlEscaped() + "</p>";
    explain_->setHtml(html);
    demoKind_->setCurrentIndex((int)a);                   // same order as SyncAlgo
    setDemoDefaults((int)a);
}

void SyncTab::updateParamVisibility() {
    SyncAlgo a = algorithm();
    bool buf = isBuffer(a);
    paramForm_->setRowVisible(procSpin_, !buf);
    procSpin_->setEnabled(a != SyncAlgo::PETERSON);
    procSpin_->setToolTip(a == SyncAlgo::PETERSON ? "Peterson's solution is for exactly 2 processes"
                                                  : "Number of processes");
    paramForm_->setRowVisible(resSpin_, a == SyncAlgo::COUNTING_SEM);
    for (QSpinBox* s : {prodSpin_, consSpin_, capSpin_, prodSpeed_, consSpeed_}) paramForm_->setRowVisible(s, buf);
    paramForm_->setRowVisible(sliceSpin_, !buf);
    seedSpin_->setEnabled(orderCombo_->currentIndex() == 1);
}

SyncConfig SyncTab::configFromControls() const {
    SyncConfig c;
    c.algo = algorithm();
    c.processes = c.algo == SyncAlgo::PETERSON ? 2 : procSpin_->value();
    c.iterations = iterSpin_->value();
    c.resources = resSpin_->value();
    c.producers = prodSpin_->value();
    c.consumers = consSpin_->value();
    c.capacity = capSpin_->value();
    c.producerSpeed = prodSpeed_->value();
    c.consumerSpeed = consSpeed_->value();
    c.slice = sliceSpin_->value();
    c.randomOrder = orderCombo_->currentIndex() == 1;
    c.seed = (unsigned)seedSpin_->value();
    c.script = script_;
    return c;
}

void SyncTab::setControls(const SyncConfig& c) {
    loading_ = true;
    procSpin_->setValue(c.processes);
    iterSpin_->setValue(c.iterations);
    resSpin_->setValue(c.resources);
    prodSpin_->setValue(c.producers);
    consSpin_->setValue(c.consumers);
    capSpin_->setValue(c.capacity);
    prodSpeed_->setValue(c.producerSpeed);
    consSpeed_->setValue(c.consumerSpeed);
    sliceSpin_->setValue(c.slice);
    orderCombo_->setCurrentIndex(c.randomOrder ? 1 : 0);
    seedSpin_->setValue((int)c.seed);
    loading_ = false;
    updateParamVisibility();
}

void SyncTab::setAlgorithm(SyncAlgo a) {
    if (algorithm() == a && !scenarios_.empty()) { loadScenario(0); return; }
    algoCombo_->setCurrentIndex((int)a);                 // -> onAlgoChanged
}

void SyncTab::onAlgoChanged() {
    if (loading_) return;
    timer_->stop();
    prepareAlgo(algorithm());
    loadScenario(0);                                     // start with the textbook example
}

void SyncTab::onScenarioChanged() {
    if (loading_) return;
    int i = scenarioCombo_->currentIndex();
    if (i <= 0) {
        script_.clear();
        scenarioDesc_->setText("Custom settings: choose the parameters, then Run or Step.");
        resetSim(true);
    } else {
        loadScenario(i - 1);
    }
}

// Any parameter edit leaves the sample scenario ("Custom settings") and restarts.
void SyncTab::onParamChanged() {
    if (loading_) return;
    loading_ = true;
    scenarioCombo_->setCurrentIndex(0);
    loading_ = false;
    script_.clear();
    scenarioDesc_->setText("Custom settings: choose the parameters, then Run or Step.");
    updateParamVisibility();
    resetSim(true);
}

bool SyncTab::loadScenario(int index) {
    if (index < 0 || index >= (int)scenarios_.size()) return false;
    const SyncScenario& s = scenarios_[index];
    loading_ = true;
    scenarioCombo_->setCurrentIndex(index + 1);
    loading_ = false;
    setControls(s.cfg);
    script_ = s.cfg.script;
    QString desc = QString::fromStdString(s.description);
    if (!script_.empty()) desc += " (fixed order for the first " + QString::number(script_.size()) + " steps)";
    scenarioDesc_->setText(desc);
    return resetSim(false);
}

bool SyncTab::applyConfig(const SyncConfig& c) {
    try {
        validateSyncConfig(c);
    } catch (const std::exception& e) {
        ui::warn(this, QString("Synchronization: %1").arg(e.what()));
        status_->setText(QString("Invalid settings: %1").arg(e.what()));
        return false;
    }
    if (algorithm() != c.algo) prepareAlgo(c.algo);
    loading_ = true;
    scenarioCombo_->setCurrentIndex(0);
    loading_ = false;
    setControls(c);
    script_ = c.script;
    scenarioDesc_->setText("Custom settings.");
    return resetSim(false);
}

// Builds a fresh simulation from the controls ("Reset"). Bad settings: message, no crash.
bool SyncTab::resetSim(bool quiet) {
    timer_->stop();
    paused_ = stopped_ = false;
    view_->setSim(nullptr);
    code_->setSim(nullptr);
    timeline_->setSim(nullptr);
    try {
        sim_ = std::make_unique<SyncSim>(configFromControls());
    } catch (const std::exception& e) {
        sim_.reset();
        rebuildCards();
        status_->setText(QString("Invalid settings: %1").arg(e.what()));
        status_->setStyleSheet(QString("color:%1").arg(ui::faultColor().name()));
        result_->clear();
        if (!quiet) ui::warn(this, QString("Synchronization: %1").arg(e.what()));
        updateButtons();
        return false;
    }
    status_->setStyleSheet("");
    logShown_ = 0;
    lastVars_.clear();
    events_->clear();
    view_->setSim(sim_.get());
    code_->setSim(sim_.get());
    timeline_->setSim(sim_.get());
    rebuildCards();
    refresh(0);
    return true;
}

// ---------------------------------------------------------------- run controls

void SyncTab::run() {
    if (!sim_ || sim_->finished() || stopped_)
        if (!resetSim(false)) return;
    paused_ = false;
    timer_->start(speed_->value());
    emit logMessage(QString("Sync: run %1").arg(syncAlgoName(algorithm())));
    updateButtons();
}

void SyncTab::pause() {
    if (!timer_->isActive()) return;
    timer_->stop();
    paused_ = true;
    updateButtons();
}

void SyncTab::resume() {
    if (!sim_ || stopped_ || sim_->finished()) return;
    paused_ = false;
    timer_->start(speed_->value());
    updateButtons();
}

bool SyncTab::stepOnce() {
    if (!sim_ && !resetSim(false)) return false;
    if (stopped_ || sim_->finished()) return false;
    if (timer_->isActive()) pause();
    else paused_ = sim_->stats().steps > 0;
    bool ok = sim_->stepAuto();
    refresh(std::min(300, speed_->value()));
    return ok;
}

// Manual interleaving: the user decides which process executes its next instruction.
bool SyncTab::stepProcess(int pid) {
    if (!sim_ || stopped_) return false;
    if (!sim_->canRun(pid)) {
        QString why = pid >= 0 && pid < (int)sim_->procs().size()
                          ? QString::fromStdString(sim_->procs()[pid].name) + " cannot run: it is " +
                                syncStateName(sim_->procs()[pid].state)
                          : QString("no such process");
        status_->setText(why);
        return false;
    }
    if (timer_->isActive()) pause();
    else paused_ = true;
    sim_->step(pid);
    refresh(std::min(300, speed_->value()));
    return true;
}

void SyncTab::stop() {
    timer_->stop();
    if (sim_ && sim_->stats().steps > 0 && !sim_->finished()) stopped_ = true;
    paused_ = false;
    updateButtons();
    if (stopped_) status_->setText("Stopped. Press Reset (or Run) to start again.");
}

int SyncTab::runToEnd() {
    if ((!sim_ || stopped_) && !resetSim(false)) return 0;
    timer_->stop();
    paused_ = false;
    int n = sim_->runToEnd();
    refresh(0);
    return n;
}

void SyncTab::setSpeed(int ms) { speed_->setValue(ms); }
bool SyncTab::animating() const { return timer_->isActive(); }
QString SyncTab::statusText() const { return status_->text(); }

// Timer tick: one instruction per tick; the chips glide during the tick.
void SyncTab::onTimer() {
    if (!sim_ || sim_->finished()) {
        timer_->stop();
        updateButtons();
        return;
    }
    sim_->stepAuto();
    refresh(std::min(350, speed_->value() * 4 / 5));
    if (sim_->finished()) timer_->stop();
    updateButtons();
}

// ---------------------------------------------------------------- views

void SyncTab::refresh(int animMs) {
    if (!sim_) return;
    view_->refresh(animMs);
    code_->update();
    timeline_->setSim(sim_.get());
    timelineScroll_->horizontalScrollBar()->setValue(timelineScroll_->horizontalScrollBar()->maximum());
    updateCards();
    updateVariables();
    updateStats();
    appendNewLog();
    updateButtons();
    const auto& h = sim_->history();
    if (h.empty()) status_->setText("Ready. Press Run, Step, or the Step button of a process card to choose who runs next.");
    else status_->setText(QString::fromStdString(h.back().text));
    status_->setStyleSheet("");
}

void SyncTab::rebuildCards() {
    for (Card& c : cards_) {
        cardRow_->removeWidget(c.frame);
        c.frame->hide();                   // deleted later by the event loop: hide it now
        c.frame->deleteLater();
    }
    cards_.clear();
    if (!sim_) return;
    for (const SyncProc& p : sim_->procs()) {
        Card c;
        c.frame = new QFrame;
        c.frame->setObjectName("syncCard");
        c.frame->setMinimumWidth(190);
        c.frame->setMaximumWidth(250);
        auto* v = new QVBoxLayout(c.frame);
        v->setContentsMargins(8, 4, 8, 4);
        v->setSpacing(2);
        auto* head = new QHBoxLayout;
        c.title = new QLabel(QString("<b>%1</b>").arg(QString::fromStdString(p.name)));
        c.state = new QLabel;
        c.state->setAlignment(Qt::AlignCenter);
        c.step = new QToolButton;
        c.step->setText(QString::fromUtf8("Step ▶"));
        c.step->setToolTip("Execute the next instruction of " + QString::fromStdString(p.name));
        head->addWidget(c.title);
        head->addWidget(c.state);
        head->addStretch();
        head->addWidget(c.step);
        c.info = new QLabel;
        QFont f = c.info->font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        c.info->setFont(f);
        int pid = p.id;
        connect(c.step, &QToolButton::clicked, this, [this, pid] { stepProcess(pid); });
        v->addLayout(head);
        v->addWidget(c.info);
        cardRow_->insertWidget(cardRow_->count() - 1, c.frame);
        cards_.push_back(c);
    }
}

void SyncTab::updateCards() {
    if (!sim_) return;
    for (size_t i = 0; i < cards_.size() && i < sim_->procs().size(); ++i) {
        const SyncProc& p = sim_->procs()[i];
        Card& c = cards_[i];
        QColor pc = ui::pidColor((int)i), sc = stateColor(p.state);
        c.frame->setStyleSheet(QString("QFrame#syncCard { border: %1px solid %2; border-radius: 6px; }")
                                   .arg(p.inCS ? 3 : 1).arg(p.inCS ? pc.name() : pc.darker(110).name()));
        c.state->setText(QString(syncStateName(p.state)) + (p.inCS ? "  IN CS" : ""));
        c.state->setStyleSheet(QString("background:%1; color:%2; border-radius:4px; padding:1px 5px; font-weight:bold;")
                                   .arg(sc.name(), ui::darkInk().name()));
        const auto& prog = sim_->programs()[p.program];
        QString next = p.state == SyncProcState::Done ? "finished"
                       : QString("next: line %1  %2").arg(p.pc + 1)
                             .arg(QString::fromStdString(prog[p.pc].code).trimmed().section("//", 0, 0).trimmed().left(26));
        QString data = isBuffer(sim_->config().algo) ? QString("item = %1").arg(p.item < 0 ? QString("-") : QString::number(p.item))
                       : sim_->config().algo == SyncAlgo::COUNTING_SEM ? QString("round %1 of %2").arg(std::min(p.itersDone + 1, p.iterations)).arg(p.iterations)
                       : QString("reg = %1").arg(p.reg);
        QString wait = p.waitingOn >= 0 ? "waiting on " + QString::fromStdString(sim_->objects()[p.waitingOn].name)
                                        : QString("loop %1/%2").arg(std::min(p.itersDone + 1, p.iterations)).arg(p.iterations);
        c.info->setText(QString("%1\n%2 | %3 | CS %4, wait %5")
                            .arg(next, data, wait).arg(p.csEntries).arg(p.busyWaitSteps + p.blockedSteps));
        c.step->setEnabled(sim_->canRun((int)i) && !stopped_);
    }
}

void SyncTab::updateVariables() {
    auto vars = sim_->variables();
    vars_->setRowCount((int)vars.size());
    QMap<QString, QString> now;
    for (int r = 0; r < (int)vars.size(); ++r) {
        QString k = QString::fromStdString(vars[r].first), v = QString::fromStdString(vars[r].second);
        now[k] = v;
        auto* name = ui::roItem(k);
        name->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* val = ui::roItem(v);
        if (lastVars_.contains(k) && lastVars_[k] != v) ui::shade(val, QColor("#fff1a8"));   // changed this step
        vars_->setItem(r, 0, name);
        vars_->setItem(r, 1, val);
    }
    lastVars_ = now;
}

// Expected vs actual, every counter, and theory vs observation.
void SyncTab::updateStats() {
    SyncStats s = sim_->stats();
    SyncAlgo a = sim_->config().algo;
    QString what = a == SyncAlgo::COUNTING_SEM ? "resource uses" : isBuffer(a) ? "items consumed" : "counter";
    std::vector<std::pair<QString, QString>> rows = {
        {"Steps executed", QString::number(s.steps)},
        {"Expected " + what, QString::number(s.expected)},
        {"Actual " + what + " (so far)", QString::number(s.actual)},
        {"Lost updates", QString::number(s.lostUpdates)},
        {"Critical-section violations", QString::number(s.csViolations)},
        {"Max inside at once (allowed)", QString("%1 (%2)").arg(s.maxInCS).arg(sim_->csCapacity())},
        {"Critical-section entries", QString::number(s.csEntries)},
        {"Busy-wait steps (CPU wasted)", QString::number(s.busyWaitSteps)},
        {"Blocked steps (asleep)", QString::number(s.blockedSteps)},
        {"Waiting steps (total)", QString::number(s.waitingSteps())},
    };
    if (isBuffer(a)) {
        rows.push_back({"Produced / consumed", QString("%1 / %2").arg(s.produced).arg(s.consumed)});
        rows.push_back({"Overflows / underflows", QString("%1 / %2").arg(s.overflows).arg(s.underflows)});
    }
    rows.push_back({"Completed", s.completed ? "yes" : s.stuck ? "STUCK" : "not yet"});
    stats_->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); ++r) {
        auto* n = ui::roItem(rows[r].first);
        n->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* v = ui::roItem(rows[r].second);
        bool bad = (rows[r].first == "Lost updates" || rows[r].first == "Critical-section violations") && rows[r].second != "0";
        if (bad) ui::shade(v, ui::lightRed());
        stats_->setItem(r, 0, n);
        stats_->setItem(r, 1, v);
    }
    bool wrong = s.lostUpdates > 0 || s.csViolations > 0 || s.overflows > 0 || s.underflows > 0;
    QString res = QString("Expected %1 = %2, actual = %3").arg(what).arg(s.expected).arg(s.actual);
    if (s.lostUpdates > 0) res += QString(" | %1 lost update(s)").arg(s.lostUpdates);
    if (s.csViolations > 0) res += QString(" | %1 critical-section violation(s)").arg(s.csViolations);
    res += QString(" | waiting steps %1 | CS entries %2").arg(s.waitingSteps()).arg(s.csEntries);
    if (s.completed) res += wrong ? " | finished: WRONG result" : " | finished: correct";
    if (s.stuck) res += " | STUCK";
    result_->setText(colorText(res, wrong ? ui::faultColor() : s.completed ? ui::hitColor().darker(130)
                                                                     : palette().text().color()));
    guarantee_->setText("<b>Theory</b> (holds for every interleaving):<br>" +
                        QString::fromStdString(syncGuarantee(a)).toHtmlEscaped() +
                        "<br><br><b>Observed</b> (this one run):<br>" + res.toHtmlEscaped() +
                        "<br><br><i>One run can only show a problem, never prove correctness.</i>");
}

void SyncTab::appendNewLog() {
    const auto& log = sim_->log();
    for (; logShown_ < log.size(); ++logShown_) {
        QString l = QString::fromStdString(log[logShown_]);
        events_->appendPlainText(l);
        emit logMessage("SYNC " + l);
    }
}

void SyncTab::updateButtons() {
    bool has = sim_ != nullptr, fin = has && sim_->finished(), anim = timer_->isActive();
    runBtn_->setEnabled(!anim);
    pauseBtn_->setEnabled(anim);
    resumeBtn_->setEnabled(has && !anim && paused_ && !fin && !stopped_);
    stepBtn_->setEnabled(has && !fin && !stopped_);
    stopBtn_->setEnabled(anim || (paused_ && !fin));
    endBtn_->setEnabled(has && !fin && !stopped_);
    for (size_t i = 0; i < cards_.size(); ++i)
        cards_[i].step->setEnabled(has && sim_->canRun((int)i) && !stopped_);
    bool demo = bridge_->running();
    demoRun_->setEnabled(!demo);
    demoStop_->setEnabled(demo);
}

// ---------------------------------------------------------------- comparison

bool SyncTab::compare() {
    try {
        compare_ = compareSync(cmpIter_->value(), cmpSlice_->value(), cmpOrder_->currentIndex() == 1,
                               (unsigned)cmpSeed_->value());
    } catch (const std::exception& e) {
        ui::warn(this, QString("Comparison failed: %1").arg(e.what()));
        return false;
    }
    cmpTable_->setRowCount((int)compare_.size());
    QStringList cats;
    std::vector<std::vector<double>> vals(3);
    for (int r = 0; r < (int)compare_.size(); ++r) {
        const SyncComparisonRow& row = compare_[r];
        const SyncStats& s = row.stats;
        QStringList cells = {QString::fromStdString(row.label), QString::number(s.expected), QString::number(s.actual),
                             QString::number(s.lostUpdates), QString::number(s.csViolations),
                             QString::number(s.waitingSteps()), QString::number(s.busyWaitSteps),
                             QString::number(s.blockedSteps), QString::number(s.csEntries),
                             s.completed ? (s.lostUpdates == 0 && s.csViolations == 0 ? "yes, correct" : "yes, WRONG result")
                                         : "no",
                             QString::fromStdString(row.guarantee)};
        for (int c = 0; c < cells.size(); ++c) {
            auto* it = ui::roItem(cells[c]);
            if (c == 0 || c == 10) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if ((c == 3 && s.lostUpdates > 0) || (c == 4 && s.csViolations > 0) || (c == 9 && cells[c].contains("WRONG")))
                ui::shade(it, ui::lightRed());
            if (c == 9 && cells[c] == "yes, correct") ui::shade(it, ui::lightGreen());
            cmpTable_->setItem(r, c, it);
        }
        cats << QString::fromStdString(row.label);
        vals[0].push_back(s.lostUpdates);
        vals[1].push_back(s.csViolations);
        vals[2].push_back(s.waitingSteps());
        emit logMessage(QString("Sync comparison: %1 expected %2 actual %3 lost %4 violations %5 waiting %6")
                            .arg(QString::fromStdString(row.label)).arg(s.expected).arg(s.actual).arg(s.lostUpdates)
                            .arg(s.csViolations).arg(s.waitingSteps()));
    }
    cmpChart_->setData("Same 2-process workload (lower is better)", cats,
                       {"Lost updates", "CS violations", "Waiting steps"}, vals,
                       {ui::faultColor(), QColor("#f28e2b"), QColor("#4e79a7")});
    bottom_->setCurrentIndex(3);                         // the Comparison page
    return true;
}

// ---------------------------------------------------------------- real POSIX demo

// Sensible defaults for each primitive (counting / buffer demos sleep so blocking is visible).
void SyncTab::setDemoDefaults(int kind) {
    SyncDemoKind k = (SyncDemoKind)kind;
    dThreads_->setValue(k == SyncDemoKind::PETERSON ? 2 : k == SyncDemoKind::COUNTING_SEM ? 5 : 4);
    dIter_->setValue(k == SyncDemoKind::COUNTING_SEM ? 4 : k == SyncDemoKind::PETERSON ? 5000 : 20000);
    dRes_->setValue(2);
    dProd_->setValue(2);
    dCons_->setValue(2);
    dCap_->setValue(3);
    dItems_->setValue(8);
    dDelay_->setValue(k == SyncDemoKind::COUNTING_SEM ? 30 : (k == SyncDemoKind::PRODUCER_CONSUMER || k == SyncDemoKind::MONITOR) ? 40 : 0);
    updateDemoVisibility();
}

void SyncTab::updateDemoVisibility() {
    SyncDemoKind k = (SyncDemoKind)demoKind_->currentIndex();
    bool buf = k == SyncDemoKind::PRODUCER_CONSUMER || k == SyncDemoKind::MONITOR;
    demoForm_->setRowVisible(dThreads_, !buf);
    dThreads_->setEnabled(k != SyncDemoKind::PETERSON);
    demoForm_->setRowVisible(dIter_, !buf);
    demoForm_->setRowVisible(dRes_, k == SyncDemoKind::COUNTING_SEM);
    for (QSpinBox* s : {dProd_, dCons_, dCap_, dItems_}) demoForm_->setRowVisible(s, buf);
}

bool SyncTab::runRealDemo() {
    if (bridge_->running()) return false;
    SyncDemoKind k = (SyncDemoKind)demoKind_->currentIndex();
    SyncDemoParams p;
    p.threads = dThreads_->value();
    p.iterations = dIter_->value();
    p.resources = dRes_->value();
    p.producers = dProd_->value();
    p.consumers = dCons_->value();
    p.capacity = dCap_->value();
    p.items = dItems_->value();
    p.delayUs = dDelay_->value() * 1000;
    try {
        bridge_->start(k, p);
    } catch (const std::exception& e) {
        ui::warn(this, QString("Real POSIX demo: %1").arg(e.what()));
        return false;
    }
    demoLog_->clear();
    demoResult_->setText(QString("Running %1 ...").arg(syncDemoName(k)));
    emit logMessage(QString("Sync POSIX demo started: %1").arg(syncDemoName(k)));
    updateButtons();
    return true;
}

void SyncTab::stopRealDemo() { bridge_->stop(); }
bool SyncTab::realDemoRunning() const { return bridge_->running(); }

void SyncTab::onDemoLine(const QString& line) { demoLog_->appendPlainText(line); }

void SyncTab::onDemoFinished() {
    demo_ = bridge_->result();
    QString text = QString("%1: %2 (%3 ms, %4 threads)")
                       .arg(syncDemoName(demo_->kind), QString::fromStdString(demo_->summary))
                       .arg(demo_->millis, 0, 'f', 1).arg(demo_->threadsCreated);
    bool raceDemo = demo_->kind == SyncDemoKind::RACE;
    QColor c = demo_->cancelled ? palette().text().color()
               : raceDemo ? (demo_->actual < demo_->expected ? ui::faultColor() : palette().text().color())
               : demo_->ok ? ui::hitColor().darker(130) : ui::faultColor();
    demoResult_->setText(colorText((demo_->ok && !raceDemo ? "CHECKS PASSED - " : "") + text, c));
    emit logMessage("Sync POSIX demo: " + text);
    updateButtons();
    emit demoFinished();
}

// ---------------------------------------------------------------- report

static QString csvQ(QString s) {
    if (s.contains(',') || s.contains('"')) s = '"' + s.replace('"', "\"\"") + '"';
    return s;
}

QString SyncTab::reportText() const {
    QString out;
    if (sim_) {
        const SyncConfig& c = sim_->config();
        SyncStats s = sim_->stats();
        out += QString("Algorithm: %1\n").arg(syncAlgoName(c.algo));
        out += isBuffer(c.algo)
                   ? QString("Producers %1, consumers %2, capacity %3, items per producer %4, speeds %5/%6\n")
                         .arg(c.producers).arg(c.consumers).arg(c.capacity).arg(c.iterations)
                         .arg(c.producerSpeed).arg(c.consumerSpeed)
                   : QString("Processes %1, iterations %2, instructions per turn %3%4\n")
                         .arg(c.processes).arg(c.iterations).arg(c.slice)
                         .arg(c.algo == SyncAlgo::COUNTING_SEM ? QString(", S = %1").arg(c.resources) : QString());
        out += QString("Order: %1\n").arg(c.randomOrder ? QString("random, seed %1").arg(c.seed) : QString("round robin"));
        out += QString("Steps %1; expected %2, actual %3; lost updates %4; CS violations %5; max inside %6; "
                       "CS entries %7; busy-wait steps %8; blocked steps %9; completed %10\n")
                   .arg(s.steps).arg(s.expected).arg(s.actual).arg(s.lostUpdates).arg(s.csViolations)
                   .arg(s.maxInCS).arg(s.csEntries).arg(s.busyWaitSteps).arg(s.blockedSteps)
                   .arg(s.completed ? "yes" : "no");
        out += "Guarantee: " + QString::fromStdString(syncGuarantee(c.algo)) + "\n";
    } else {
        out += "(not run)\n";
    }
    out += "\nComparison (2 processes, same workload):\n";
    if (compare_.empty()) out += "(not run)\n";
    for (const SyncComparisonRow& r : compare_)
        out += QString("%1 expected=%2 actual=%3 lost=%4 violations=%5 waiting=%6 entries=%7 completed=%8\n    theory: %9\n")
                   .arg(QString::fromStdString(r.label), -18).arg(r.stats.expected).arg(r.stats.actual)
                   .arg(r.stats.lostUpdates).arg(r.stats.csViolations).arg(r.stats.waitingSteps())
                   .arg(r.stats.csEntries).arg(r.stats.completed ? "yes" : "no")
                   .arg(QString::fromStdString(r.guarantee));
    out += "\nReal POSIX demo: ";
    out += demo_ ? QString("%1: %2 (%3 ms)\n").arg(syncDemoName(demo_->kind), QString::fromStdString(demo_->summary))
                       .arg(demo_->millis, 0, 'f', 1)
                 : QString("(not run)\n");
    return out;
}

QString SyncTab::csvSection() const {
    QString out = "## Synchronization\nkey,value\n";
    if (sim_) {
        const SyncConfig& c = sim_->config();
        SyncStats s = sim_->stats();
        out += QString("algorithm,%1\n").arg(csvQ(syncAlgoName(c.algo)));
        out += QString("steps,%1\nexpected,%2\nactual,%3\nlost_updates,%4\ncs_violations,%5\nmax_in_cs,%6\n"
                       "cs_entries,%7\nbusy_wait_steps,%8\nblocked_steps,%9\ncompleted,%10\n")
                   .arg(s.steps).arg(s.expected).arg(s.actual).arg(s.lostUpdates).arg(s.csViolations)
                   .arg(s.maxInCS).arg(s.csEntries).arg(s.busyWaitSteps).arg(s.blockedSteps)
                   .arg(s.completed ? "yes" : "no");
    } else {
        out += "status,not run\n";
    }
    out += "\n## Synchronization comparison\nmechanism,expected,actual,lost_updates,cs_violations,waiting_steps,"
           "busy_wait_steps,blocked_steps,cs_entries,completed,guarantee\n";
    for (const SyncComparisonRow& r : compare_)
        out += QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11\n")
                   .arg(csvQ(QString::fromStdString(r.label))).arg(r.stats.expected).arg(r.stats.actual)
                   .arg(r.stats.lostUpdates).arg(r.stats.csViolations).arg(r.stats.waitingSteps())
                   .arg(r.stats.busyWaitSteps).arg(r.stats.blockedSteps).arg(r.stats.csEntries)
                   .arg(r.stats.completed ? "yes" : "no").arg(csvQ(QString::fromStdString(r.guarantee)));
    out += "\n## Synchronization POSIX demo\nprimitive,expected,actual,max_concurrent,ok,millis\n";
    if (demo_)
        out += QString("%1,%2,%3,%4,%5,%6\n").arg(csvQ(syncDemoName(demo_->kind))).arg(demo_->expected)
                   .arg(demo_->actual).arg(demo_->maxConcurrent).arg(demo_->ok ? "yes" : "no").arg(demo_->millis);
    return out;
}
