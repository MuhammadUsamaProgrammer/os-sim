// MemoryTab.cpp - all page replacement work is done by core/ (runPageReplacement);
// this class reads the input and paints the frame tables.
#include "MemoryTab.h"
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QSplitter>
#include <QFormLayout>
#include "PanelLayout.h"
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include "BarChartWidget.h"
#include "Style.h"
#include "io_formats.h"

static const char* kStandard = "7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1";
static const char* kBelady = "1,2,3,4,1,2,5,1,2,3,4,5";

MemoryTab::MemoryTab(QWidget* parent) : QWidget(parent) {
    // ---- inputs ----
    auto* inBox = new QGroupBox("Page replacement input");
    refEdit_ = new QLineEdit(kStandard);
    refEdit_->setPlaceholderText("e.g. 7,0,1,2,0,3 (commas and/or spaces)");
    framesSpin_ = new QSpinBox;
    framesSpin_->setRange(1, 10);
    framesSpin_->setValue(3);
    modeCombo_ = new QComboBox;
    modeCombo_->addItems({"FIFO", "LRU", "Optimal", "Compare all"});
    modeCombo_->setCurrentIndex(3);
    auto* runBtn = new QPushButton("Run");
    auto* randBtn = new QPushButton("Random string");
    auto* stdBtn = new QPushButton("Standard example");
    auto* belBtn = new QPushButton("Belady example");
    auto* g = new QFormLayout(inBox);                 // vertical form: fits a narrow side panel
    g->setSpacing(8);
    g->addRow(new QLabel("Reference string:"));
    g->addRow(refEdit_);
    g->addRow("Frames:", framesSpin_);
    g->addRow("Algorithm:", modeCombo_);
    g->addRow(runBtn);
    auto* exRow = new QGridLayout;
    exRow->addWidget(randBtn, 0, 0); exRow->addWidget(stdBtn, 0, 1); exRow->addWidget(belBtn, 1, 0, 1, 2);
    g->addRow(exRow);

    // ---- outputs: frame tables (one tab per algorithm), summaries, chart ----
    tables_ = new QTabWidget;
    summaryRow_ = new QWidget;
    new QHBoxLayout(summaryRow_);
    beladyLabel_ = new QLabel;
    beladyLabel_->setWordWrap(true);
    chart_ = new BarChartWidget;
    auto* outLeft = new QWidget;
    auto* ol = new QVBoxLayout(outLeft);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(tables_, 1);
    ol->addWidget(summaryRow_);
    ol->addWidget(beladyLabel_);
    auto* outSplit = new QSplitter(Qt::Horizontal);
    outSplit->addWidget(outLeft);
    outSplit->addWidget(chart_);
    outSplit->setStretchFactor(0, 3);
    outSplit->setStretchFactor(1, 1);
    outSplit->setSizes({680, 300});

    // ---- page-table panel ----
    auto* ptBox = new QGroupBox("Page table (equal frame allocation)");
    pidCombo_ = new QComboBox;
    physSpin_ = new QSpinBox;
    physSpin_->setRange(1, 64);
    physSpin_->setValue(12);
    pageTable_ = new QTableWidget(0, 3);
    pageTable_->setHorizontalHeaderLabels({"Page", "Frame", "Valid bit"});
    pageTable_->setToolTip("invalid = page is not in memory (on disk); touching it causes a page fault");
    pageTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pageTable_->verticalHeader()->setVisible(false);
    pageTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    allocLabel_ = new QLabel;
    allocLabel_->setWordWrap(true);
    auto* pg = new QGridLayout(ptBox);
    pg->setSpacing(8);
    pg->addWidget(new QLabel("Process:"), 0, 0); pg->addWidget(pidCombo_, 0, 1);
    pg->addWidget(new QLabel("Physical frames:"), 1, 0); pg->addWidget(physSpin_, 1, 1);
    pg->addWidget(allocLabel_, 2, 0, 1, 2);
    pg->addWidget(pageTable_, 3, 0, 1, 2);

    auto* inW = new QWidget;
    auto* il = new QVBoxLayout(inW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(inBox);
    il->addStretch();
    auto* body = new PanelSplitter(Qt::Horizontal, "Memory/PageReplacement/h");
    body->addPanel(new ConfigPanel(inW), PanelSplitter::Config, 1, 0.3);
    body->addPanel(outSplit, PanelSplitter::Center);
    body->addPanel(ConfigPanel::wrap(ptBox, "inspectorPanel"), PanelSplitter::Inspector, 1, 0.26);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(8);
    lay->addWidget(body, 1);

    connect(runBtn, &QPushButton::clicked, this, [this] { run(); });
    connect(refEdit_, &QLineEdit::returnPressed, this, [this] { run(); });
    connect(randBtn, &QPushButton::clicked, this, [this] {
        QStringList pages;
        for (int i = 0; i < 20; ++i) pages << QString::number(QRandomGenerator::global()->bounded(10));
        refEdit_->setText(pages.join(","));
        run();
    });
    connect(stdBtn, &QPushButton::clicked, this, [this] {
        refEdit_->setText(kStandard); framesSpin_->setValue(3); modeCombo_->setCurrentIndex(3); run();
    });
    connect(belBtn, &QPushButton::clicked, this, &MemoryTab::beladyExample);
    connect(pidCombo_, &QComboBox::currentIndexChanged, this, &MemoryTab::showPageTable);
    connect(physSpin_, &QSpinBox::valueChanged, this, &MemoryTab::showPageTable);
}

void MemoryTab::setRefString(const QString& s) { refEdit_->setText(s); }
void MemoryTab::setFrames(int n) { framesSpin_->setValue(n); }
void MemoryTab::setMode(int index) { modeCombo_->setCurrentIndex(index); }

bool MemoryTab::loadRefFile(const QString& path) {
    try {
        std::string text = readTextFile(path.toStdString());
        parseRefString(text);                                  // validate before showing
        refEdit_->setText(QString::fromStdString(text).simplified());
        emit logMessage("Loaded reference string from " + path);
        return run();
    } catch (const std::exception& e) {
        ui::warn(this, QString("Could not load %1:\n%2").arg(path, e.what()));
        return false;
    }
}

// Frame table: columns = references, rows = frames + HIT/FAULT + Evicted.
QTableWidget* MemoryTab::makeFrameTable(const MemResult& r) {
    int n = (int)r.steps.size();
    auto* t = new QTableWidget(r.numFrames + 2, n);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setMinimumSectionSize(30);
    QStringList rows;
    for (int f = 0; f < r.numFrames; ++f) rows << QString("Frame %1").arg(f + 1);
    rows << "Result" << "Evicted";
    t->setVerticalHeaderLabels(rows);
    QStringList cols;
    for (const MemStep& s : r.steps) cols << QString::number(s.page);
    t->setHorizontalHeaderLabels(cols);
    for (int i = 0; i < n; ++i) {
        const MemStep& s = r.steps[i];
        for (int f = 0; f < r.numFrames; ++f) {
            auto* it = ui::roItem(s.frames[f] < 0 ? "" : QString::number(s.frames[f]));
            bool changed = s.fault && s.frames[f] == s.page;         // frame that received the page
            if (changed) ui::shade(it, ui::lightRed());          // the frame that was just loaded
            else if (!s.fault && s.frames[f] == s.page) ui::shade(it, ui::lightGreen());
            t->setItem(f, i, it);
        }
        auto* res = ui::roItem(s.fault ? "FAULT" : "HIT");
        res->setForeground(Qt::white);
        res->setBackground(s.fault ? ui::faultColor() : ui::hitColor());
        if (s.replaced >= 0) res->setToolTip(QString("page %1 was replaced").arg(s.replaced));
        t->setItem(r.numFrames, i, res);
        t->setItem(r.numFrames + 1, i, ui::roItem(s.replaced >= 0 ? QString::number(s.replaced) : ""));
    }
    return t;
}

// A small box with faults / hits / hit ratio for one algorithm.
QWidget* MemoryTab::makeSummary(const MemResult& r) {
    auto* box = new QGroupBox(pageAlgoName(r.algo));
    auto* l = new QVBoxLayout(box);
    l->addWidget(new QLabel(QString("<span style='color:%1'><b>Faults: %2</b></span><br>"
                                    "<span style='color:%3'><b>Hits: %4</b></span><br>Hit ratio: %5")
                                .arg(ui::faultColor().name()).arg(r.faults)
                                .arg(ui::hitColor().darker(120).name()).arg(r.hits)
                                .arg(r.hitRatio, 0, 'f', 3)));
    return box;
}

// Runs the selected algorithm (or all three) via core/ and displays the result.
bool MemoryTab::run() {
    std::vector<int> refs;
    try {
        refs = parseRefString(refEdit_->text().toStdString());
    } catch (const std::exception& e) {
        ui::warn(this, QString("Bad reference string: %1\nUse page numbers >= 0 separated by commas or spaces.").arg(e.what()));
        return false;
    }
    if (refs.empty()) { ui::warn(this, "The reference string is empty."); return false; }
    int frames = framesSpin_->value();
    std::vector<PageAlgo> algos;
    int mode = modeCombo_->currentIndex();
    if (mode == 3) algos = {PageAlgo::FIFO, PageAlgo::LRU, PageAlgo::OPTIMAL};
    else algos = {(PageAlgo)mode};

    results_.clear();
    while (tables_->count()) { QWidget* w = tables_->widget(0); tables_->removeTab(0); delete w; }
    QLayout* sl = summaryRow_->layout();
    while (QLayoutItem* it = sl->takeAt(0)) { delete it->widget(); delete it; }

    QStringList cats;
    std::vector<std::vector<double>> faults(1);
    std::vector<QColor> colors;
    for (PageAlgo a : algos) {
        MemResult r = runPageReplacement(a, refs, frames);
        results_.push_back(r);
        tables_->addTab(makeFrameTable(r), QString("%1 (%2 faults)").arg(pageAlgoName(a)).arg(r.faults));
        sl->addWidget(makeSummary(r));
        cats << pageAlgoName(a);
        faults[0].push_back(r.faults);
        emit logMessage(QString("Memory: %1, %2 frames, %3 refs -> %4 faults, %5 hits, hit ratio %6")
                            .arg(pageAlgoName(a)).arg(frames).arg(refs.size()).arg(r.faults).arg(r.hits)
                            .arg(r.hitRatio, 0, 'f', 3));
    }
    chart_->setData(QString("Page faults (%1 frames)").arg(frames), cats, {"Faults"}, faults, {ui::faultColor()});
    beladyLabel_->clear();
    belady_.clear();
    return true;
}

// Belady's anomaly: FIFO gets MORE faults with 4 frames than with 3 on this string.
void MemoryTab::beladyExample() {
    refEdit_->setText(kBelady);
    framesSpin_->setValue(3);
    modeCombo_->setCurrentIndex(0);
    run();
    std::vector<int> refs = parseRefString(kBelady);
    QStringList cats;
    std::vector<std::vector<double>> faults(1);
    for (int f = 1; f <= 6; ++f) {
        cats << QString("%1 fr").arg(f);
        faults[0].push_back(runPageReplacement(PageAlgo::FIFO, refs, f).faults);
    }
    MemResult four = runPageReplacement(PageAlgo::FIFO, refs, 4);
    tables_->addTab(makeFrameTable(four), QString("FIFO, 4 frames (%1 faults)").arg(four.faults));
    summaryRow_->layout()->addWidget(makeSummary(four));
    chart_->setData("FIFO faults vs number of frames", cats, {"Faults"}, faults, {ui::faultColor()});
    belady_ = QString("Belady's anomaly: FIFO with 3 frames -> %1 faults, with 4 frames -> %2 faults "
                      "(more memory, more faults).").arg((int)faults[0][2]).arg((int)faults[0][3]);
    beladyLabel_->setText("<b>" + belady_ + "</b>");
    emit logMessage(belady_);
}

// Refills the pid combo from the Scheduling tab's process list.
void MemoryTab::refreshPids() {
    if (!procs_) return;
    int keep = pidCombo_->currentData().isValid() ? pidCombo_->currentData().toInt() : -1;
    pidCombo_->blockSignals(true);
    pidCombo_->clear();
    for (const PCB& p : procs_()) pidCombo_->addItem(QString("P%1 (%2 pages)").arg(p.pid).arg(p.pages), p.pid);
    int idx = pidCombo_->findData(keep);
    pidCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    pidCombo_->blockSignals(false);
    showPageTable();
}

// Builds the page table of the chosen pid with FrameAllocator (equal share).
void MemoryTab::showPageTable() {
    pageTable_->setRowCount(0);
    if (!procs_ || pidCombo_->count() == 0) { allocLabel_->setText("No processes in the Scheduling tab."); return; }
    std::vector<PCB> procs = procs_();
    std::vector<int> pids;
    int pages = 0, pid = pidCombo_->currentData().toInt();
    for (const PCB& p : procs) { pids.push_back(p.pid); if (p.pid == pid) pages = p.pages; }
    FrameAllocator fa(physSpin_->value());
    int share = fa.allocateEqual(pids);
    PageTable pt = buildPageTable(pid, pages, fa);
    allocLabel_->setText(QString("%1 frames / %2 processes = %3 frame(s) each. P%4 owns frames: %5")
                             .arg(fa.totalFrames).arg(pids.size()).arg(share).arg(pid)
                             .arg([&] { QStringList l; for (int f : fa.framesOf(pid)) l << QString::number(f);
                                        return l.isEmpty() ? QString("none") : l.join(", "); }()));
    pageTable_->setRowCount((int)pt.entries.size());
    for (int i = 0; i < (int)pt.entries.size(); ++i) {
        const PageTableEntry& e = pt.entries[i];
        pageTable_->setItem(i, 0, ui::roItem(QString::number(i)));
        pageTable_->setItem(i, 1, ui::roItem(e.valid ? QString::number(e.frame) : "-"));
        auto* v = ui::roItem(e.valid ? "valid" : "invalid");
        ui::shade(v, e.valid ? ui::lightGreen() : ui::lightRed());
        pageTable_->setItem(i, 2, v);
    }
}
