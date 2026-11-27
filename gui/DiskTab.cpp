// DiskTab.cpp - Disk Scheduling module (M1).
#include "DiskTab.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include "BarChartWidget.h"
#include "DiskViews.h"
#include "ModuleKit.h"
#include "Style.h"

namespace {
struct Preset { const char* name; int minC, maxC, head; DiskDir dir; std::vector<int> req; };
const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = {
        {"Textbook reference (head 53, higher)", 0, 199, 53, DiskDir::Higher, {98, 183, 37, 122, 14, 124, 65, 67}},
        {"Same queue, direction lower", 0, 199, 53, DiskDir::Lower, {98, 183, 37, 122, 14, 124, 65, 67}},
        {"Edge: head request, duplicates, both ends", 0, 199, 53, DiskDir::Lower, {53, 65, 0, 199, 65, 120}},
        {"Edge: all requests behind the head", 0, 199, 150, DiskDir::Higher, {20, 40, 90, 10}},
        {"Edge: SSTF tie (40 and 60 from 50)", 0, 99, 50, DiskDir::Higher, {60, 40, 90, 10}},
        {"Edge: single request", 0, 199, 53, DiskDir::Higher, {120}},
        {"Edge: empty queue", 0, 199, 53, DiskDir::Higher, {}},
    };
    return p;
}
QSpinBox* spin(int lo, int hi, int v) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    return s;
}
}

DiskTab::DiskTab(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Disk Scheduling",
        "How an HDD request queue is serviced: the algorithm decides the order, and the order decides the "
        "total head movement. Educational cylinder-distance model (no seek time in ms, no SSD behaviour).");

    // ---- configuration
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const Preset& p : presets()) preset_->addItem(p.name);
    algo_ = new QComboBox;
    for (DiskAlgo a : allDiskAlgos()) algo_->addItem(diskAlgoName(a));
    dir_ = new QComboBox;
    dir_->addItems({"toward higher cylinders", "toward lower cylinders"});
    minC_ = spin(0, 99999, 0);
    maxC_ = spin(1, 100000, 199);
    head_ = spin(0, 100000, 53);
    reqTable_ = new QTableWidget(0, 2);
    reqTable_->setHorizontalHeaderLabels({"Request", "Cylinder"});
    reqTable_->horizontalHeader()->setStretchLastSection(true);
    reqTable_->verticalHeader()->setVisible(false);
    reqTable_->setMinimumHeight(170);
    auto* add = new QPushButton("Add");
    auto* del = new QPushButton("Delete");
    auto* up = new QPushButton("Up");
    auto* down = new QPushButton("Down");
    randCount_ = spin(0, 500, 10);
    randSeed_ = spin(1, 99999, 7);
    auto* gen = new QPushButton("Random queue");
    gen->setToolTip("Reproducible random requests (std::mt19937 with the seed)");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->setSpacing(6);
    cf->addRow("Preset:", preset_);
    cf->addRow("Algorithm:", algo_);
    cf->addRow("Initial direction:", dir_);
    cf->addRow("First cylinder:", minC_);
    cf->addRow("Last cylinder:", maxC_);
    cf->addRow("Initial head:", head_);
    cf->addRow(new QLabel("<b>Request queue</b> (edit the cylinder; order = arrival order)"));
    cf->addRow(reqTable_);
    auto* rb = new QHBoxLayout;
    for (QPushButton* b : {add, del, up, down}) rb->addWidget(b);
    cf->addRow(rb);
    auto* rg = new QHBoxLayout;
    rg->addWidget(new QLabel("Count"));
    rg->addWidget(randCount_);
    rg->addWidget(new QLabel("Seed"));
    rg->addWidget(randSeed_);
    cf->addRow(rg);
    cf->addRow(gen);

    // ---- centre: platter + graph
    platter_ = new PlatterWidget;
    graph_ = new SeekGraph;
    auto* center = new QSplitter(Qt::Horizontal);
    center->addWidget(titledBox("Platter and head (schematic)", platter_));
    center->addWidget(titledBox("Head position over the service order", graph_));
    center->setSizes({330, 430});

    // ---- inspector
    calc_ = new QLabel;
    calc_->setWordWrap(true);
    QFont bf = calc_->font();
    bf.setBold(true);
    calc_->setFont(bf);
    reason_ = new QLabel;
    reason_->setWordWrap(true);
    pending_ = new QListWidget;
    pending_->setFlow(QListView::LeftToRight);
    pending_->setWrapping(true);
    pending_->setMaximumHeight(70);
    code_ = new PseudoCodeView;
    code_->setMinimumHeight(120);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>Current movement</b>"));
    il->addWidget(calc_);
    il->addWidget(reason_);
    il->addWidget(new QLabel("<b>Pending requests</b>"));
    il->addWidget(pending_);
    il->addWidget(new QLabel("<b>Pseudocode</b>"));
    il->addWidget(code_, 1);

    // ---- bottom
    bar_ = new PlaybackBar;
    bottom_ = new QTabWidget;
    service_ = new QTableWidget(0, 7);
    service_->setHorizontalHeaderLabels({"Step", "Request", "From", "To", "Distance", "Movement type", "Cumulative"});
    service_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    service_->verticalHeader()->setVisible(false);
    service_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottom_->addTab(service_, "Service order");
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    bottom_->addTab(log_, "Event log");
    cmpTable_ = new QTableWidget(0, 4);
    cmpTable_->setHorizontalHeaderLabels({"Algorithm", "Total movement", "Average / request", "Service order"});
    cmpTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    cmpTable_->horizontalHeader()->setStretchLastSection(true);
    cmpTable_->verticalHeader()->setVisible(false);
    cmpTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cmpChart_ = new BarChartWidget;
    auto* cmpW = new QWidget;
    auto* cl = new QHBoxLayout(cmpW);
    cl->addWidget(cmpTable_, 3);
    cl->addWidget(cmpChart_, 2);
    bottom_->addTab(cmpW, "Compare all six");

    assembleModule(this, header_, cfgW, center, titledBox("Inspector", insW), bar_, bottom_);

    // ---- connections
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    for (QComboBox* c : {algo_, dir_}) connect(c, &QComboBox::currentIndexChanged, this, &DiskTab::onInputsChanged);
    for (QSpinBox* s : {minC_, maxC_, head_}) connect(s, &QSpinBox::valueChanged, this, &DiskTab::onInputsChanged);
    connect(reqTable_, &QTableWidget::itemChanged, this, &DiskTab::onInputsChanged);
    connect(add, &QPushButton::clicked, this, [this] {
        std::vector<int> v;
        for (int r = 0; r < reqTable_->rowCount(); ++r) v.push_back(reqTable_->item(r, 1) ? reqTable_->item(r, 1)->text().toInt() : 0);
        v.push_back(head_->value());
        fillRequests(v);
        onInputsChanged();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        int r = reqTable_->currentRow();
        if (r < 0) r = reqTable_->rowCount() - 1;
        if (r < 0) return;
        std::vector<int> v;
        for (int i = 0; i < reqTable_->rowCount(); ++i) if (i != r) v.push_back(reqTable_->item(i, 1)->text().toInt());
        fillRequests(v);
        onInputsChanged();
    });
    auto moveRow = [this](int d) {
        int r = reqTable_->currentRow();
        if (r < 0 || r + d < 0 || r + d >= reqTable_->rowCount()) return;
        std::vector<int> v;
        for (int i = 0; i < reqTable_->rowCount(); ++i) v.push_back(reqTable_->item(i, 1)->text().toInt());
        std::swap(v[r], v[r + d]);
        fillRequests(v);
        reqTable_->setCurrentCell(r + d, 1);
        onInputsChanged();
    };
    connect(up, &QPushButton::clicked, this, [moveRow] { moveRow(-1); });
    connect(down, &QPushButton::clicked, this, [moveRow] { moveRow(1); });
    connect(gen, &QPushButton::clicked, this, [this] {
        try {
            fillRequests(randomDiskRequests(randCount_->value(), minC_->value(), maxC_->value(), (unsigned)randSeed_->value()));
            onInputsChanged();
        } catch (const std::exception& e) { header_->setStatus(e.what(), ui::semError()); }
    });
    connect(bar_, &PlaybackBar::positionChanged, this, &DiskTab::onStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });

    loadPreset(0);
}

void DiskTab::fillRequests(const std::vector<int>& req) {
    bool old = loading_;
    loading_ = true;
    reqTable_->setRowCount((int)req.size());
    for (int i = 0; i < (int)req.size(); ++i) {
        reqTable_->setItem(i, 0, ui::roItem(QString("R%1").arg(i)));
        auto* it = new QTableWidgetItem(QString::number(req[i]));
        it->setTextAlignment(Qt::AlignCenter);
        reqTable_->setItem(i, 1, it);
    }
    loading_ = old;
}

bool DiskTab::readConfig(DiskConfig& c, QString& err) const {
    c.minCyl = minC_->value();
    c.maxCyl = maxC_->value();
    c.head = head_->value();
    c.dir = dir_->currentIndex() == 0 ? DiskDir::Higher : DiskDir::Lower;
    c.requests.clear();
    for (int r = 0; r < reqTable_->rowCount(); ++r) {
        bool ok = false;
        int v = reqTable_->item(r, 1) ? reqTable_->item(r, 1)->text().trimmed().toInt(&ok) : 0;
        if (!ok) { err = QString("Request R%1: '%2' is not a whole number").arg(r).arg(reqTable_->item(r, 1) ? reqTable_->item(r, 1)->text() : ""); return false; }
        c.requests.push_back(v);
    }
    try { validateDiskConfig(c); } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

void DiskTab::onInputsChanged() {
    if (loading_) return;
    loading_ = true;
    preset_->setCurrentIndex(0);
    loading_ = false;
    rebuild();
}

// Builds the trace from the inputs; old history is discarded (playback back to step 0).
bool DiskTab::rebuild() {
    DiskConfig c;
    QString err;
    platter_->setResult(nullptr);
    graph_->setResult(nullptr);
    if (!readConfig(c, err)) {
        result_.reset();
        bar_->setLength(0);
        service_->setRowCount(0);
        log_->clear();
        header_->setStatus("Invalid input: " + err, ui::semError());
        return false;
    }
    result_ = runDisk((DiskAlgo)algo_->currentIndex(), c);
    platter_->setResult(&*result_);
    graph_->setResult(&*result_);
    QStringList lines;
    for (const std::string& s : diskPseudocode(result_->algo)) lines << QString::fromStdString(s);
    code_->setLines(lines);
    // full service table and log (rows after the current step are dimmed)
    service_->setRowCount((int)result_->moves.size());
    log_->clear();
    for (int i = 0; i < (int)result_->moves.size(); ++i) {
        const DiskMove& m = result_->moves[i];
        QStringList cells = {QString::number(i + 1), m.requestId >= 0 ? QString("R%1").arg(m.requestId) : "-",
                             QString::number(m.from), QString::number(m.to), QString::number(m.distance),
                             QString(moveKindName(m.kind)) + (m.reversed ? ", after reversal" : ""),
                             QString::number(m.cumulative)};
        for (int k = 0; k < cells.size(); ++k) service_->setItem(i, k, ui::roItem(cells[k]));
        log_->appendPlainText(QString("[step %1] %2 -> %3 (%4 cyl, %5) total %6: %7")
                                  .arg(i + 1).arg(m.from).arg(m.to).arg(m.distance).arg(moveKindName(m.kind))
                                  .arg(m.cumulative).arg(QString::fromStdString(m.reason)));
    }
    bar_->setLength((int)result_->moves.size());
    compare();
    onStep(0, false);
    emit logMessage(QString("Disk: %1, head %2, %3 requests -> total head movement %4")
                        .arg(diskAlgoName(result_->algo)).arg(c.head).arg(c.requests.size()).arg(result_->total));
    return true;
}

void DiskTab::onStep(int pos, bool forward) {
    if (!result_) return;
    int anim = forward ? bar_->animMs() : 0;
    platter_->setStep(pos, anim);
    graph_->setStep(pos, anim);
    updateInspector(pos);
}

void DiskTab::updateInspector(int step) {
    const DiskResult& r = *result_;
    if (r.cfg.requests.empty()) {
        calc_->setText("No requests: nothing to service (total 0, average not defined).");
        reason_->clear();
        code_->setCurrent(-1);
    } else if (step == 0) {
        calc_->setText(QString("Head at %1, direction %2. Total so far: 0").arg(r.cfg.head).arg(diskDirName(r.cfg.dir)));
        reason_->setText("Press Run or Next to service the first request.");
        code_->setCurrent(0);
    } else {
        const DiskMove& m = r.moves[step - 1];
        calc_->setText(QString("|%1 - %2| = %3 cylinders  ->  total %4%5")
                           .arg(m.to).arg(m.from).arg(m.distance).arg(m.cumulative)
                           .arg(m.requestId >= 0 ? QString("   (serviced R%1)").arg(m.requestId) : QString("   (%1, nothing serviced)").arg(moveKindName(m.kind))));
        reason_->setText(QString::fromStdString(m.reason) + (m.reversed ? "  The head reversed direction." : ""));
        code_->setCurrent(m.pseudoLine);
    }
    pending_->clear();
    std::vector<bool> done(r.cfg.requests.size(), false);
    for (int i = 0; i < step; ++i) if (r.moves[i].requestId >= 0) done[r.moves[i].requestId] = true;
    int servicedNow = 0;
    for (int id = 0; id < (int)done.size(); ++id) {
        if (done[id]) { ++servicedNow; continue; }
        auto* it = new QListWidgetItem(QString("R%1:%2").arg(id).arg(r.cfg.requests[id]));
        it->setBackground(ui::semReady().lighter(160));
        it->setForeground(ui::darkInk());
        pending_->addItem(it);
    }
    if (pending_->count() == 0) pending_->addItem("(none)");
    for (int i = 0; i < service_->rowCount(); ++i)
        for (int k = 0; k < service_->columnCount(); ++k)
            if (auto* it = service_->item(i, k)) {
                if (i == step - 1) ui::shade(it, ui::lightGreen());
                else { it->setBackground(QBrush()); it->setForeground(i < step ? palette().text() : QBrush(ui::semIdle())); }
            }
    if (step > 0) service_->scrollToItem(service_->item(step - 1, 0));
    int total = step > 0 ? r.moves[step - 1].cumulative : 0;
    QString st = QString("%1: step %2 of %3, serviced %4 of %5, head movement so far %6 cylinders")
                     .arg(diskAlgoName(r.algo)).arg(step).arg(r.moves.size()).arg(servicedNow).arg(r.cfg.requests.size()).arg(total);
    if (step == (int)r.moves.size())
        st += r.serviced > 0 ? QString(" | FINISHED: total %1, average %2 per request").arg(r.total).arg(r.average, 0, 'f', 2)
                             : QString(" | no requests (average not defined)");
    header_->setStatus(st, step == (int)r.moves.size() ? ui::hitColor().darker(130) : QColor());
}

bool DiskTab::compare() {
    if (!result_) return false;
    compare_ = runAllDisk(result_->cfg);
    cmpTable_->setRowCount((int)compare_.size());
    QStringList cats;
    std::vector<std::vector<double>> vals(1);
    int best = 0;
    for (int i = 0; i < (int)compare_.size(); ++i) if (compare_[i].total < compare_[best].total) best = i;
    for (int i = 0; i < (int)compare_.size(); ++i) {
        const DiskResult& r = compare_[i];
        QStringList ord;
        for (int id : r.order) ord << QString::number(r.cfg.requests[id]);
        QStringList cells = {diskAlgoName(r.algo), QString::number(r.total),
                             r.serviced ? QString::number(r.average, 'f', 2) : "n/a", ord.join(", ")};
        for (int k = 0; k < 4; ++k) {
            auto* it = ui::roItem(cells[k]);
            if (k == 3) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (r.total == compare_[best].total && k == 1) ui::shade(it, ui::lightGreen());
            if (r.algo == result_->algo && k == 0) { QFont f = it->font(); f.setBold(true); it->setFont(f); }
            cmpTable_->setItem(i, k, it);
        }
        cats << diskAlgoName(r.algo);
        vals[0].push_back(r.total);
    }
    cmpChart_->setData("Total head movement (cylinders, lower is better)", cats, {"total movement"}, vals, {ui::semReady()});
    return true;
}

bool DiskTab::loadPreset(int index) {
    if (index < 0 || index >= (int)presets().size()) return false;
    const Preset& p = presets()[index];
    DiskConfig c;
    c.minCyl = p.minC; c.maxCyl = p.maxC; c.head = p.head; c.dir = p.dir; c.requests = p.req;
    bool ok = setConfig(c);
    loading_ = true;
    preset_->setCurrentIndex(index + 1);
    loading_ = false;
    return ok;
}

bool DiskTab::setConfig(const DiskConfig& c) {
    loading_ = true;
    minC_->setValue(c.minCyl);
    maxC_->setValue(c.maxCyl);
    head_->setValue(c.head);
    dir_->setCurrentIndex(c.dir == DiskDir::Higher ? 0 : 1);
    fillRequests(c.requests);
    preset_->setCurrentIndex(0);
    loading_ = false;
    return rebuild();
}

void DiskTab::setAlgorithm(DiskAlgo a) { algo_->setCurrentIndex((int)a); }

bool DiskTab::loadFile(const QString& path) {
    try {
        DiskConfig c = loadDiskFile(path.toStdString());
        bool ok = setConfig(c);
        emit logMessage("Disk: loaded " + path);
        return ok;
    } catch (const std::exception& e) {
        ui::warn(this, QString("Could not load %1:\n%2").arg(path, e.what()));
        return false;
    }
}

QString DiskTab::statusText() const { return header_->status(); }

QString DiskTab::reportText() const {
    if (!result_) return "(no valid input)\n";
    const DiskConfig& c = result_->cfg;
    QStringList req;
    for (int v : c.requests) req << QString::number(v);
    QString out = QString("Cylinders %1..%2, head %3, direction %4, requests: %5\n")
                      .arg(c.minCyl).arg(c.maxCyl).arg(c.head).arg(diskDirName(c.dir)).arg(req.join(" "));
    out += "Convention: SCAN/C-SCAN go to the boundary only when requests remain behind the head; wrap distance counted.\n";
    for (const DiskResult& r : compare_) {
        QStringList ord;
        for (int id : r.order) ord << QString::number(c.requests[id]);
        out += QString("%1 total=%2 average=%3 order: %4\n").arg(diskAlgoName(r.algo), -7).arg(r.total)
                   .arg(r.serviced ? QString::number(r.average, 'f', 2) : "n/a").arg(ord.join(", "));
    }
    out += QString("Trace of %1:\n").arg(diskAlgoName(result_->algo));
    for (const DiskMove& m : result_->moves)
        out += QString("  %1 -> %2  %3  (%4) total %5\n").arg(m.from).arg(m.to).arg(m.distance).arg(moveKindName(m.kind)).arg(m.cumulative);
    return out;
}

QString DiskTab::csvSection() const {
    QString out = "## Disk scheduling\nalgorithm,total_movement,average,serviced,order\n";
    if (result_)
        for (const DiskResult& r : compare_) {
            QStringList ord;
            for (int id : r.order) ord << QString::number(r.cfg.requests[id]);
            out += QString("%1,%2,%3,%4,%5\n").arg(diskAlgoName(r.algo)).arg(r.total).arg(r.average).arg(r.serviced).arg(ord.join(' '));
        }
    out += "\n## Disk trace\nstep,from,to,distance,type,request,cumulative\n";
    if (result_)
        for (int i = 0; i < (int)result_->moves.size(); ++i) {
            const DiskMove& m = result_->moves[i];
            out += QString("%1,%2,%3,%4,%5,%6,%7\n").arg(i + 1).arg(m.from).arg(m.to).arg(m.distance)
                       .arg(m.kind == MoveKind::Service ? "service" : m.kind == MoveKind::Boundary ? "boundary" : "wrap")
                       .arg(m.requestId).arg(m.cumulative);
        }
    return out;
}
