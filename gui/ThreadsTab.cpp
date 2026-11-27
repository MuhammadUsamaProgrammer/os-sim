// ThreadsTab.cpp - Multithreading and threading-models module (M6).
#include "ThreadsTab.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include "BarChartWidget.h"
#include "ModuleKit.h"
#include "NativeRunner.h"
#include "Style.h"

static QColor ink(const QWidget* w) { return w->palette().text().color(); }
static QColor faint(const QWidget* w, int a) { QColor c = ink(w); c.setAlpha(a); return c; }
static QColor tColor(TState s) {
    switch (s) {
        case TState::New: return ui::semIdle();
        case TState::Ready: return ui::semReady();
        case TState::Running: return ui::semRunning();
        case TState::Blocked: return ui::semWaiting();
        case TState::Joining: return QColor("#edc948");
        case TState::Completed: return ui::semDone();
    }
    return ui::semIdle();
}

// ================================================================ ThreadMapView

ThreadMapView::ThreadMapView(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t_ = v.toDouble(); update(); });
    setAccessibleName("User threads, kernel contexts and CPU cores");
}

void ThreadMapView::setData(const ThreadResult* r, const std::vector<TSnap>* th, const ThreadTick* tick, int animMs) {
    anim_.stop();
    r_ = r; th_ = th; tick_ = tick;
    t_ = 1;
    if (animMs > 0 && tick) { t_ = 0; anim_.setDuration(animMs); anim_.start(); }
    update();
}

void ThreadMapView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!r_ || !th_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Enter a valid thread workload"); return; }
    const ThreadConfig& c = r_->cfg;
    const int n = (int)c.threads.size(), K = effectiveContexts(c);
    const double W = width(), H = height();
    QFont small = font(), bold = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    bold.setBold(true);
    double yU = H * 0.16, yK = H * 0.5, yC = H * 0.82;
    auto xs = [&](int count, int i) { return (W - 120) * (i + 0.5) / std::max(1, count) + 110; };
    p.setFont(bold);
    p.setPen(ink(this));
    p.drawText(QRectF(4, yU - 12, 104, 24), Qt::AlignLeft | Qt::AlignVCenter, "User threads");
    p.drawText(QRectF(4, yK - 12, 104, 24), Qt::AlignLeft | Qt::AlignVCenter, QString("Kernel contexts (%1)").arg(K));
    p.drawText(QRectF(4, yC - 12, 104, 24), Qt::AlignLeft | Qt::AlignVCenter, QString("CPU cores (%1)").arg(c.cores));
    p.setFont(small);
    // links for running threads: user -> context -> core (they grow with the animation)
    for (int i = 0; i < n; ++i) {
        const TSnap& s = (*th_)[i];
        if (s.state != TState::Running || s.context < 0 || s.core < 0) continue;
        QPointF u(xs(n, i), yU + 16), k(xs(K, s.context), yK - 14), kb(xs(K, s.context), yK + 14), core(xs(c.cores, s.core), yC - 16);
        QColor col = ui::pidColor(c.threads[i].id);
        p.setPen(QPen(col, 3));
        double f1 = std::min(1.0, t_ * 2), f2 = std::max(0.0, t_ * 2 - 1);
        p.drawLine(u, u + (k - u) * f1);
        if (f2 > 0) p.drawLine(kb, kb + (core - kb) * f2);
    }
    // blocked threads keep their context (dashed link)
    for (int i = 0; i < n; ++i) {
        const TSnap& s = (*th_)[i];
        if (s.state != TState::Blocked || s.context < 0 || s.context >= K) continue;
        p.setPen(QPen(ui::semWaiting(), 2, Qt::DashLine));
        p.drawLine(QPointF(xs(n, i), yU + 16), QPointF(xs(K, s.context), yK - 14));
    }
    // user threads
    for (int i = 0; i < n; ++i) {
        const TSnap& s = (*th_)[i];
        QPointF ctr(xs(n, i), yU);
        p.setPen(QPen(tColor(s.state), s.state == TState::Running ? 3 : 1.6));
        p.setBrush(ui::pidColor(c.threads[i].id));
        p.drawEllipse(ctr, 17, 17);
        p.setPen(ui::darkInk());
        p.drawText(QRectF(ctr.x() - 17, ctr.y() - 9, 34, 18), Qt::AlignCenter, QString("T%1").arg(c.threads[i].id));
        p.setPen(ink(this));
        p.drawText(QRectF(ctr.x() - 50, ctr.y() + 18, 100, 14), Qt::AlignCenter,
                   QString("%1 %2/%3").arg(tStateName(s.state)).arg(s.done).arg(c.threads[i].work));
    }
    // kernel contexts
    for (int k = 0; k < K; ++k) {
        QRectF r(xs(K, k) - 34, yK - 14, 68, 28);
        bool blocked = tick_ && k < (int)tick_->contextBlocked.size() && tick_->contextBlocked[k];
        int holder = -1;
        for (int i = 0; i < n; ++i) if ((*th_)[i].context == k && ((*th_)[i].state == TState::Running || (*th_)[i].state == TState::Blocked)) holder = i;
        p.setPen(QPen(blocked ? ui::semWaiting() : holder >= 0 ? ui::semRunning() : faint(this, 140), blocked || holder >= 0 ? 2.5 : 1.2));
        p.setBrush(blocked ? QBrush(ui::semWaiting(), Qt::BDiagPattern) : QBrush(faint(this, 18)));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(ink(this));
        p.drawText(r, Qt::AlignCenter, QString("K%1%2").arg(k).arg(blocked ? " blocked" : holder >= 0 ? QString(" T%1").arg(c.threads[holder].id) : QString("")));
    }
    // cores
    for (int x = 0; x < c.cores; ++x) {
        QRectF r(xs(c.cores, x) - 30, yC - 16, 60, 32);
        int i = tick_ && x < (int)tick_->coreThread.size() ? tick_->coreThread[x] : -1;
        p.setPen(QPen(i >= 0 ? ui::semRunning() : faint(this, 140), i >= 0 ? 2.5 : 1.2));
        p.setBrush(i >= 0 ? ui::semRunning().lighter(160) : faint(this, 15));
        p.drawRect(r);
        p.setPen(i >= 0 ? ui::darkInk() : ink(this));
        p.drawText(r, Qt::AlignCenter, i >= 0 ? QString("CPU%1: T%2").arg(x).arg(c.threads[i].id) : QString("CPU%1 idle").arg(x));
    }
    p.setPen(ink(this));
    p.drawText(QRectF(4, H - 18, W - 8, 16), Qt::AlignLeft,
               QString("%1: each context runs <= 1 thread, each core <= 1 context. Logical ticks (work units), not wall-clock time.").arg(threadModelName(c.model)));
}

// ================================================================ ThreadTimeline

static const int kLab = 150, kCol = 18, kRow = 20, kHead = 18;

ThreadTimeline::ThreadTimeline(QWidget* parent) : QWidget(parent) { setAccessibleName("Thread and core timeline"); }

void ThreadTimeline::setResults(const std::vector<const ThreadResult*>& rs, const QStringList& labels, int upto) {
    rs_ = rs;
    labels_ = labels;
    upto_ = upto;
    setMinimumSize(sizeHint());
    resize(sizeHint());
    update();
}

QSize ThreadTimeline::sizeHint() const {
    int ticks = 0, rows = 0;
    for (const ThreadResult* r : rs_) {
        ticks = std::max(ticks, (int)r->ticks.size());
        rows += r->cfg.cores + (rs_.size() == 1 ? (int)r->cfg.threads.size() : 0) + 1;
    }
    return QSize(kLab + std::max(30, ticks + 2) * kCol, kHead + rows * kRow + 30);
}

void ThreadTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (rs_.empty()) return;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p.setFont(small);
    int ticks = 0;
    for (const ThreadResult* r : rs_) ticks = std::max(ticks, (int)r->ticks.size());
    for (int t = 0; t <= ticks; t += 2) {
        p.setPen(ink(this));
        p.drawText(QRect(kLab + t * kCol, 0, 30, kHead), Qt::AlignLeft | Qt::AlignVCenter, QString::number(t));
    }
    int row = 0;
    auto label = [&](const QString& s) { p.setPen(ink(this)); p.drawText(QRect(4, kHead + row * kRow, kLab - 8, kRow), Qt::AlignVCenter, s); };
    for (size_t ri = 0; ri < rs_.size(); ++ri) {
        const ThreadResult& r = *rs_[ri];
        int lim = rs_.size() == 1 ? std::min(upto_, (int)r.ticks.size()) : (int)r.ticks.size();
        QString pre = labels_.value((int)ri);
        if (rs_.size() == 1) {
            for (size_t i = 0; i < r.cfg.threads.size(); ++i) {
                label(QString("T%1 %2").arg(r.cfg.threads[i].id).arg(QString::fromStdString(r.cfg.threads[i].name)));
                for (int t = 0; t < lim; ++t) {
                    const TSnap& s = r.ticks[t].threads[i];
                    if (s.state == TState::New || s.state == TState::Completed) continue;
                    QRect c(kLab + t * kCol + 1, kHead + row * kRow + 2, kCol - 2, kRow - 4);
                    QColor col = tColor(s.state);
                    if (s.state != TState::Running) col.setAlpha(120);
                    p.fillRect(c, col);
                    p.setPen(ui::darkInk());
                    if (s.state == TState::Running) p.drawText(c, Qt::AlignCenter, QString::number(s.core));
                    else if (s.state == TState::Blocked) p.drawText(c, Qt::AlignCenter, "B");
                    else if (s.state == TState::Joining) p.drawText(c, Qt::AlignCenter, "J");
                }
                ++row;
            }
        }
        for (int x = 0; x < r.cfg.cores; ++x) {
            label(QString("%1CPU%2").arg(pre.isEmpty() ? QString() : pre + " ").arg(x));
            for (int t = 0; t < lim; ++t) {
                int i = r.ticks[t].coreThread[x];
                if (i < 0) continue;
                QRect c(kLab + t * kCol + 1, kHead + row * kRow + 2, kCol - 2, kRow - 4);
                p.fillRect(c, ui::pidColor(r.cfg.threads[i].id));
                p.setPen(ui::darkInk());
                p.drawText(c, Qt::AlignCenter, QString::number(r.cfg.threads[i].id));
            }
            ++row;
        }
        if (r.m.completion > 0 && (rs_.size() > 1 || upto_ >= (int)r.ticks.size())) {
            p.setPen(QPen(ui::semError(), 2));
            int x = kLab + r.m.completion * kCol;
            p.drawLine(x, kHead + (row - r.cfg.cores - (rs_.size() == 1 ? (int)r.cfg.threads.size() : 0)) * kRow, x, kHead + row * kRow);
        }
        ++row;
    }
    p.setPen(ink(this));
    p.drawText(QRect(4, kHead + row * kRow, width(), 16), Qt::AlignLeft,
               "thread rows: number = core it runs on, B = blocked, J = joining;  CPU rows: number = thread id;  red bar = all threads completed");
}

// ================================================================ ThreadsTab

ThreadsTab::ThreadsTab(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Multithreading and Threading Models",
        "User threads are mapped onto kernel execution contexts, which run on CPU cores. Educational models - on Linux, "
        "pthreads are one-to-one; this simulation does not change that.");
    native_ = new NativeRunner(this);
    model_ = new QComboBox;
    for (ThreadModel m : {ThreadModel::ManyToOne, ThreadModel::OneToOne, ThreadModel::ManyToMany}) model_->addItem(threadModelName(m));
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const ThreadPreset& p : threadPresets()) preset_->addItem(QString::fromStdString(p.name));
    contexts_ = new QSpinBox; contexts_->setRange(1, 8); contexts_->setValue(2);
    cores_ = new QSpinBox; cores_->setRange(1, 8); cores_->setValue(2);
    quantum_ = new QSpinBox; quantum_->setRange(1, 20); quantum_->setValue(1);
    threads_ = new QTableWidget(0, 8);
    threads_->setHorizontalHeaderLabels({"ID", "Name", "Arrival", "Work", "Block at", "Block for", "Joins", "Arg"});
    threads_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    threads_->verticalHeader()->setVisible(false);
    threads_->setMinimumHeight(160);
    auto* add = new QPushButton("Create thread");
    auto* del = new QPushButton("Remove");
    auto* cfgW = new QWidget;
    auto* f = new QFormLayout(cfgW);
    f->addRow("Preset:", preset_);
    f->addRow("Mapping model:", model_);
    f->addRow("Kernel contexts (M:M):", contexts_);
    f->addRow("Logical cores:", cores_);
    f->addRow("Quantum (ticks):", quantum_);
    f->addRow(new QLabel("<b>User threads</b> (Block at = blocking call after N ticks of work, -1 = none; Joins = thread id it joins)"));
    f->addRow(threads_);
    auto* rb = new QHBoxLayout;
    rb->addWidget(add);
    rb->addWidget(del);
    f->addRow(rb);

    map_ = new ThreadMapView;
    inspector_ = new QTableWidget(0, 7);
    inspector_->setHorizontalHeaderLabels({"Thread", "Owner", "State", "Context", "Core", "Work", "Waiting reason / join"});
    inspector_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    inspector_->horizontalHeader()->setStretchLastSection(true);
    inspector_->verticalHeader()->setVisible(false);
    inspector_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    bar_ = new PlaybackBar;
    bottom_ = new QTabWidget;
    timeline_ = new ThreadTimeline;
    timelineScroll_ = new QScrollArea;
    timelineScroll_->setWidget(timeline_);
    bottom_->addTab(timelineScroll_, "Thread and core timeline");
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    bottom_->addTab(log_, "Event log");
    cmpLanes_ = new ThreadTimeline;
    auto* cmpScroll = new QScrollArea;
    cmpScroll->setWidget(cmpLanes_);
    cmpTable_ = new QTableWidget;
    cmpTable_->verticalHeader()->setVisible(false);
    cmpTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cmpChart_ = new BarChartWidget;
    auto* cmpW = new QWidget;
    auto* cl = new QVBoxLayout(cmpW);
    auto* ch = new QHBoxLayout;
    ch->addWidget(cmpTable_, 3);
    ch->addWidget(cmpChart_, 2);
    cl->addWidget(cmpScroll, 2);
    cl->addLayout(ch, 2);
    cmpW->setMinimumHeight(420);
    auto* cmpOuter = new QScrollArea;
    cmpOuter->setWidget(cmpW);
    cmpOuter->setWidgetResizable(true);
    bottom_->addTab(cmpOuter, "Compare the three models");
    parLanes_ = new ThreadTimeline;
    auto* parScroll = new QScrollArea;
    parScroll->setWidget(parLanes_);
    parLabel_ = new QLabel;
    parLabel_->setWordWrap(true);
    auto* parW = new QWidget;
    auto* pl = new QVBoxLayout(parW);
    pl->addWidget(parLabel_);
    pl->addWidget(parScroll, 1);
    bottom_->addTab(parW, "Concurrency vs parallelism");
    dThreads_ = new QSpinBox; dThreads_->setRange(1, 16); dThreads_->setValue(4);
    dBase_ = new QSpinBox; dBase_->setRange(1, 1000); dBase_->setValue(10);
    demoStart_ = new QPushButton("Start live demo");
    demoStop_ = new QPushButton("Stop");
    demoStatus_ = new QLabel("pthread_create with a per-thread argument struct, each thread computes sum(1..arg x 1000) and returns a "
                             "heap result (odd threads via pthread_exit), the main thread pthread_joins them in order.");
    demoStatus_->setWordWrap(true);
    demoTable_ = new QTableWidget;
    NativeRunner::showEvents(demoTable_, {});
    auto* dW = new QWidget;
    auto* dl = new QVBoxLayout(dW);
    auto* dt = new QHBoxLayout;
    dt->addWidget(new QLabel("Threads:"));
    dt->addWidget(dThreads_);
    dt->addWidget(new QLabel("Base argument:"));
    dt->addWidget(dBase_);
    dt->addWidget(demoStart_);
    dt->addWidget(demoStop_);
    auto* lab = new QLabel("<b>Live Linux Demo</b> (real pthreads, one-to-one on Linux; Linux TIDs are platform specific; overlapping "
                           "lifetimes do not prove parallel execution)");
    lab->setWordWrap(true);
    dt->addWidget(lab, 1);
    dl->addLayout(dt);
    dl->addWidget(demoStatus_);
    dl->addWidget(demoTable_, 1);
    bottom_->addTab(dW, "Live Linux demo");

    assembleModule(this, header_, cfgW, titledBox("Mapping: user threads -> kernel contexts -> cores", map_),
                   titledBox("Thread inspector", inspector_), bar_, bottom_);

    auto changed = [this] {
        if (loading_) return;
        loading_ = true;
        preset_->setCurrentIndex(0);
        loading_ = false;
        rebuild();
    };
    connect(model_, &QComboBox::currentIndexChanged, this, changed);
    for (QSpinBox* s : {contexts_, cores_, quantum_}) connect(s, &QSpinBox::valueChanged, this, changed);
    connect(threads_, &QTableWidget::itemChanged, this, changed);
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    connect(add, &QPushButton::clicked, this, [this, changed] {
        ThreadConfig c;
        QString e;
        readConfig(c, e);
        int id = 0;
        for (const UThreadSpec& t : c.threads) id = std::max(id, t.id + 1);
        c.threads.push_back({id, "T" + std::to_string(id), 0, 3, -1, 0, -1, id});
        loading_ = true;
        fillThreads(c.threads);
        loading_ = false;
        changed();
    });
    connect(del, &QPushButton::clicked, this, [this, changed] {
        int r = threads_->currentRow() >= 0 ? threads_->currentRow() : threads_->rowCount() - 1;
        if (r >= 0) { threads_->removeRow(r); changed(); }
    });
    connect(bar_, &PlaybackBar::positionChanged, this, &ThreadsTab::showStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });
    connect(bottom_, &QTabWidget::currentChanged, this, [this](int i) { header_->setMode(i == 4); });
    connect(demoStart_, &QPushButton::clicked, this, [this] { startDemo(dThreads_->value(), dBase_->value()); });
    connect(demoStop_, &QPushButton::clicked, this, &ThreadsTab::stopDemo);
    connect(native_, &NativeRunner::event, this, [this](const NativeEvent&) { NativeRunner::showEvents(demoTable_, native_->events()); });
    connect(native_, &NativeRunner::finished, this, [this](int code, bool crashed) {
        demoStart_->setEnabled(true);
        demoStop_->setEnabled(false);
        QString t = crashed ? "stopped" : QString("exit code %1, %2 correct joined results").arg(code).arg(demoCorrectResults());
        demoStatus_->setText("Finished: " + t);
        emit logMessage("Threads live demo: " + t);
    });
    demoStop_->setEnabled(false);
    if (!NativeRunner::available()) {
        demoStart_->setEnabled(false);
        demoStatus_->setText("Live Linux demos are not available here (needs Linux and ossim_helper). The simulation is unaffected.");
    }
    loadPreset(2);
}

void ThreadsTab::fillThreads(const std::vector<UThreadSpec>& v) {
    threads_->setRowCount((int)v.size());
    for (int r = 0; r < (int)v.size(); ++r) {
        const UThreadSpec& t = v[r];
        QStringList c = {QString::number(t.id), QString::fromStdString(t.name), QString::number(t.arrival), QString::number(t.work),
                         QString::number(t.blockAt), QString::number(t.blockFor), QString::number(t.joinTarget), QString::number(t.arg)};
        for (int k = 0; k < 8; ++k) {
            auto* it = new QTableWidgetItem(c[k]);
            it->setTextAlignment(Qt::AlignCenter);
            threads_->setItem(r, k, it);
        }
    }
}

bool ThreadsTab::readConfig(ThreadConfig& c, QString& err) const {
    c.model = (ThreadModel)model_->currentIndex();
    c.contexts = contexts_->value();
    c.cores = cores_->value();
    c.quantum = quantum_->value();
    c.threads.clear();
    for (int r = 0; r < threads_->rowCount(); ++r) {
        auto cell = [&](int k) { return threads_->item(r, k) ? threads_->item(r, k)->text().trimmed() : QString(); };
        bool ok[7];
        UThreadSpec t;
        t.id = cell(0).toInt(&ok[0]);
        t.name = cell(1).toStdString();
        t.arrival = cell(2).toInt(&ok[1]);
        t.work = cell(3).toInt(&ok[2]);
        t.blockAt = cell(4).toInt(&ok[3]);
        t.blockFor = cell(5).toInt(&ok[4]);
        t.joinTarget = cell(6).toInt(&ok[5]);
        t.arg = cell(7).toInt(&ok[6]);
        for (bool b : ok) if (!b) { err = QString("thread row %1: enter whole numbers").arg(r + 1); return false; }
        c.threads.push_back(t);
    }
    return true;
}

bool ThreadsTab::loadPreset(int index) {
    std::vector<ThreadPreset> v = threadPresets();
    if (index < 0 || index >= (int)v.size()) return false;
    const ThreadConfig& c = v[index].cfg;
    loading_ = true;
    model_->setCurrentIndex((int)c.model);
    contexts_->setValue(c.contexts);
    cores_->setValue(c.cores);
    quantum_->setValue(c.quantum);
    fillThreads(c.threads);
    preset_->setCurrentIndex(index + 1);
    loading_ = false;
    bool ok = rebuild();
    if (ok) header_->setStatus(QString::fromStdString(v[index].description));
    return ok;
}

bool ThreadsTab::rebuild() {
    ThreadConfig c;
    QString err;
    map_->setData(nullptr, nullptr, nullptr, 0);
    contexts_->setEnabled(model_->currentIndex() == 2);
    try {
        if (!readConfig(c, err)) throw std::invalid_argument(err.toStdString());
        result_ = runThreads(c);
        compare_.clear();
        for (ThreadModel m : {ThreadModel::ManyToOne, ThreadModel::OneToOne, ThreadModel::ManyToMany}) {
            ThreadConfig x = c;
            x.model = m;
            compare_.push_back(runThreads(x));
        }
        par_.clear();
        ThreadConfig one = c, many = c;
        one.model = many.model = ThreadModel::OneToOne;
        one.cores = 1;
        many.cores = std::max<int>(2, (int)c.threads.size() > 8 ? 8 : (int)c.threads.size());
        par_.push_back(runThreads(one));
        par_.push_back(runThreads(many));
    } catch (const std::exception& e) {
        result_.reset();
        compare_.clear();
        par_.clear();
        bar_->setLength(0);
        header_->setStatus(QString("Invalid workload: %1").arg(e.what()), ui::semError());
        return false;
    }
    log_->clear();
    for (const ThreadTick& k : result_->ticks)
        for (const std::string& e : k.events) log_->appendPlainText(QString("[t=%1] %2").arg(k.t).arg(QString::fromStdString(e)));
    bar_->setLength((int)result_->ticks.size() + 1);       // initial, one step per tick, final state
    // comparison
    QStringList cols = {"Model", "Contexts", "Completion (ticks)", "Max parallel", "Core occupancy %", "Waiting", "Stalled", "Blocked", "Migrations"};
    cmpTable_->setColumnCount(cols.size());
    cmpTable_->setHorizontalHeaderLabels(cols);
    cmpTable_->setRowCount(3);
    QStringList cats;
    std::vector<std::vector<double>> vals(2);
    std::vector<const ThreadResult*> rs;
    for (int i = 0; i < 3; ++i) {
        const ThreadResult& r = compare_[i];
        QStringList cl = {threadModelName(r.cfg.model), QString::number(effectiveContexts(r.cfg)), QString::number(r.m.completion),
                          QString::number(r.m.maxParallel), QString::number(r.m.occupancy(r.cfg.cores), 'f', 1),
                          QString::number(r.m.waitingTicks), QString::number(r.m.stalledTicks), QString::number(r.m.blockedTicks),
                          QString::number(r.m.migrations)};
        for (int k = 0; k < cl.size(); ++k) cmpTable_->setItem(i, k, ui::roItem(cl[k]));
        cats << threadModelName(r.cfg.model);
        vals[0].push_back(r.m.completion);
        vals[1].push_back(r.m.maxParallel);
        rs.push_back(&r);
    }
    cmpTable_->resizeColumnsToContents();
    cmpChart_->setData(QString("Same workload, %1 cores").arg(c.cores), cats, {"completion (ticks)", "max parallel"}, vals,
                       {ui::semReady(), ui::semRunning()});
    cmpLanes_->setResults(rs, {"M:1", "1:1", "M:M"}, 0);
    parLanes_->setResults({&par_[0], &par_[1]}, {"1 core", QString("%1 cores").arg(par_[1].cfg.cores)}, 0);
    parLabel_->setText(QString("<b>Concurrency</b> (1 core): the threads take turns - %1 ticks, at most %2 running at once.<br>"
                               "<b>Parallelism</b> (%3 cores, one-to-one): %4 ticks, up to %5 running in the same tick, core occupancy %6%.")
                           .arg(par_[0].m.completion).arg(par_[0].m.maxParallel).arg(par_[1].cfg.cores).arg(par_[1].m.completion)
                           .arg(par_[1].m.maxParallel).arg(par_[1].m.occupancy(par_[1].cfg.cores), 0, 'f', 0));
    showStep(0, false);
    emit logMessage(QString("Threads: %1, %2 threads, %3 cores -> completed in %4 ticks (M:1 %5, 1:1 %6, M:M %7)")
                        .arg(threadModelName(c.model)).arg(c.threads.size()).arg(c.cores).arg(result_->m.completion)
                        .arg(compare_[0].m.completion).arg(compare_[1].m.completion).arg(compare_[2].m.completion));
    return true;
}

void ThreadsTab::showStep(int pos, bool forward) {
    if (!result_) return;
    const ThreadResult& r = *result_;
    int T = (int)r.ticks.size();
    const ThreadTick* tick = pos >= 1 && pos <= T ? &r.ticks[pos - 1] : nullptr;
    initial_.assign(r.cfg.threads.size(), TSnap{});
    const std::vector<TSnap>* th = pos == 0 ? &initial_ : pos > T ? &r.finalState : &tick->threads;
    map_->setData(&r, th, tick, forward ? bar_->animMs() : 0);
    timeline_->setResults({&r}, {}, std::min(pos, T));
    timelineScroll_->horizontalScrollBar()->setValue(std::max(0, pos * 18 - timelineScroll_->viewport()->width() / 2));
    inspector_->setRowCount((int)r.cfg.threads.size());
    for (int i = 0; i < (int)r.cfg.threads.size(); ++i) {
        const UThreadSpec& sp = r.cfg.threads[i];
        const TSnap& s = (*th)[i];
        QStringList c = {QString("T%1 %2 (arg %3)").arg(sp.id).arg(QString::fromStdString(sp.name)).arg(sp.arg), "process P1",
                         tStateName(s.state), s.context >= 0 ? QString("K%1").arg(s.context) : "-", s.core >= 0 ? QString("CPU%1").arg(s.core) : "-",
                         QString("%1 / %2").arg(s.done).arg(sp.work),
                         QString::fromStdString(s.reason) + (sp.joinTarget >= 0 ? QString("  [joins T%1]").arg(sp.joinTarget) : QString()) +
                             (s.finish >= 0 ? QString("  finished t=%1").arg(s.finish) : QString())};
        for (int k = 0; k < 7; ++k) {
            auto* it = ui::roItem(c[k]);
            if (k == 2) ui::shade(it, tColor(s.state).lighter(160));
            if (k == 0 || k == 6) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            inspector_->setItem(i, k, it);
        }
    }
    QString st;
    if (pos == 0) st = QString("%1: before tick 0").arg(threadModelName(r.cfg.model));
    else if (pos > T) st = QString("%1: all threads completed in %2 ticks, max parallel %3, core occupancy %4%, completion order %5")
                               .arg(threadModelName(r.cfg.model)).arg(r.m.completion).arg(r.m.maxParallel)
                               .arg(r.m.occupancy(r.cfg.cores), 0, 'f', 0)
                               .arg([&] { QStringList l; for (int id : r.completionOrder) l << QString("T%1").arg(id); return l.join(", "); }());
    else {
        QStringList ev;
        for (const std::string& e : tick->events) ev << QString::fromStdString(e);
        int running = 0;
        for (const TSnap& s : tick->threads) running += s.state == TState::Running;
        st = QString("tick %1: %2 running%3").arg(tick->t).arg(running).arg(ev.isEmpty() ? QString() : " | " + ev.join("; "));
    }
    header_->setStatus(st, pos > T ? ui::hitColor().darker(130) : QColor());
}

bool ThreadsTab::startDemo(int threads, int base) {
    if (!native_->start({"threads", QString::number(threads), QString::number(base)})) return false;
    NativeRunner::showEvents(demoTable_, {});
    demoStart_->setEnabled(false);
    demoStop_->setEnabled(true);
    bottom_->setCurrentIndex(4);
    emit logMessage(QString("Threads live demo started: %1 threads").arg(threads));
    return true;
}

void ThreadsTab::stopDemo() { native_->stop(); }
bool ThreadsTab::demoRunning() const { return native_->running(); }
int ThreadsTab::demoExitCode() const { return native_->lastExitCode(); }
int ThreadsTab::demoCorrectResults() const {
    int n = 0;
    for (const NativeEvent& e : native_->events()) n += e.event == "joined" && e.detail.contains("(correct)");
    return n;
}
QString ThreadsTab::statusText() const { return header_->status(); }

QString ThreadsTab::reportText() const {
    if (!result_) return "(invalid workload)\n";
    QString out;
    for (const ThreadResult& r : compare_)
        out += QString("%1 (%2 contexts, %3 cores): completion %4 ticks, max parallel %5, occupancy %6%, waiting %7, stalled %8\n")
                   .arg(threadModelName(r.cfg.model)).arg(effectiveContexts(r.cfg)).arg(r.cfg.cores).arg(r.m.completion)
                   .arg(r.m.maxParallel).arg(r.m.occupancy(r.cfg.cores), 0, 'f', 1).arg(r.m.waitingTicks).arg(r.m.stalledTicks);
    if (!native_->events().isEmpty()) out += QString("Last Live Linux demo: %1 correct joined results\n").arg(demoCorrectResults());
    return out;
}

QString ThreadsTab::csvSection() const {
    QString out = "## Threading models\nmodel,contexts,cores,completion,max_parallel,occupancy,waiting,stalled,blocked,migrations\n";
    for (const ThreadResult& r : compare_)
        out += QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10\n").arg(threadModelName(r.cfg.model)).arg(effectiveContexts(r.cfg)).arg(r.cfg.cores)
                   .arg(r.m.completion).arg(r.m.maxParallel).arg(r.m.occupancy(r.cfg.cores), 0, 'f', 1).arg(r.m.waitingTicks)
                   .arg(r.m.stalledTicks).arg(r.m.blockedTicks).arg(r.m.migrations);
    return out;
}
