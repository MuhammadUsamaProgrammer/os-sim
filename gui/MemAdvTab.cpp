// MemAdvTab.cpp - Advanced Memory Management pages (M4).
#include "MemAdvTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QRegularExpression>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <sstream>
#include "HeapLab.h"
#include "MemAdvViews.h"
#include "MemoryTab.h"
#include "ModuleKit.h"
#include "Style.h"

static QSpinBox* spin(int lo, int hi, int v, const QString& tip = QString()) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    if (!tip.isEmpty()) s->setToolTip(tip);
    return s;
}
static QTableWidget* kvTable() {
    auto* t = new QTableWidget(0, 2);
    t->setHorizontalHeaderLabels({"Measure", "Value"});
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return t;
}
static void fillKv(QTableWidget* t, const std::vector<std::pair<QString, QString>>& rows) {
    t->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); ++r) {
        auto* a = ui::roItem(rows[r].first);
        a->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        t->setItem(r, 0, a);
        t->setItem(r, 1, ui::roItem(rows[r].second));
    }
}
static void highlightList(QListWidget* l, int upto) {
    for (int i = 0; i < l->count(); ++i) {
        QListWidgetItem* it = l->item(i);
        bool cur = i == upto - 1;
        QColor bg = ui::semRunning();
        bg.setAlpha(90);
        it->setBackground(cur ? QBrush(bg) : QBrush());
        it->setForeground(i < upto ? QBrush() : QBrush(ui::semIdle()));
        QFont f = it->font();
        f.setBold(cur);
        it->setFont(f);
    }
}
static QString q(const std::string& s) { return QString::fromStdString(s); }

MemAdvArea::MemAdvArea(MemoryTab* pr, QWidget* parent) : QTabWidget(parent), mem_(pr) {
    addTab(mem_, "Page replacement");
    addTab(buildAlloc(), "Allocation && Fragmentation");
    addTab(buildPaging(), "Paging && Address Translation");
    addTab(buildSeg(), "Segmentation");
    addTab(buildDemand(), "Demand Paging");
    addTab(buildSwap(), "Swapping");
    addTab(buildThrash(), "Thrashing");
    heap_ = new HeapLabPage;                       // Phase 3: application-level heap (not OS allocation)
    addTab(heap_, "Heap Laboratory");
    connect(heap_, &HeapLabPage::logMessage, this, &MemAdvArea::logMessage);
}

// ================================================================ allocation

struct AllocPresetDef { const char* name; bool fixed; const char* mem; int os; const char* ops; const char* text; };
static const AllocPresetDef kAllocPresets[] = {
    {"Textbook: holes 100 500 200 300 600", false, "100 500 200 300 600", 100, "P1 212, P2 417, P3 112, P4 426",
     "First and Worst Fit cannot place P4 (426) although enough memory is free in total; Best Fit can."},
    {"Free and coalesce", false, "300 400", 100, "A 100, B 100, C 100, free B, free A, D 250",
     "Freeing A next to the hole left by B merges them into one larger hole (coalescing), so D fits."},
    {"Compaction", false, "100 500 200 300 600", 100, "P1 212, P2 417, P3 112, P4 426, compact, P5 426",
     "After compaction all free space is one hole and the failed request succeeds (needs relocation)."},
    {"Fixed partitions (internal fragmentation)", true, "100 200 300 400", 100, "A 90, B 150, C 290, D 120, E 50",
     "Each process takes a whole partition: the unused part inside it is internal fragmentation."},
};

QWidget* MemAdvArea::buildAlloc() {
    allocHdr_ = new ModuleHeader("Contiguous Allocation and Fragmentation",
        "Variable partitions with First / Best / Worst Fit (splitting and coalescing), fixed partitions, compaction and "
        "base/limit protection. Sizes are abstract memory units.");
    allocMode_ = new QComboBox;
    allocMode_->addItems({"Variable partitions (holes)", "Fixed partitions"});
    allocPreset_ = new QComboBox;
    allocPreset_->addItem("(custom)");
    for (const AllocPresetDef& p : kAllocPresets) allocPreset_->addItem(p.name);
    allocMem_ = new QLineEdit;
    allocMem_->setToolTip("Hole sizes (variable) or partition sizes (fixed), in address order");
    allocOs_ = spin(0, 10000, 100, "Memory reserved for the OS at address 0");
    allocOps_ = new QLineEdit;
    allocOps_->setToolTip("Comma separated: 'P1 212' allocates, 'free P1' frees, 'compact' compacts");
    allocFocus_ = new QComboBox;
    for (FitAlgo a : {FitAlgo::First, FitAlgo::Best, FitAlgo::Worst}) allocFocus_->addItem(fitAlgoName(a));
    blBlock_ = new QComboBox;
    blOffset_ = spin(-10, 100000, 50);
    blResult_ = new QLabel;
    blResult_->setWordWrap(true);
    auto* cfg = new QWidget;
    auto* f = new QFormLayout(cfg);
    f->addRow("Preset:", allocPreset_);
    f->addRow("Mode:", allocMode_);
    f->addRow("Holes / partitions:", allocMem_);
    f->addRow("OS region:", allocOs_);
    f->addRow("Operations:", allocOps_);
    f->addRow("Inspect algorithm:", allocFocus_);
    f->addRow(new QLabel("<b>Base / limit check</b> (current step)"));
    f->addRow("Process block:", blBlock_);
    f->addRow("Logical offset:", blOffset_);
    f->addRow(blResult_);
    allocView_ = new AllocBarView;
    allocMetrics_ = kvTable();
    allocExplain_ = new QLabel;
    allocExplain_->setWordWrap(true);
    auto* ins = new QSplitter(Qt::Vertical);
    ins->addWidget(allocMetrics_);
    ins->addWidget(allocExplain_);
    allocBar_ = new PlaybackBar;
    auto* page = new QWidget;
    assembleModule(page, allocHdr_, cfg, titledBox("Memory map after each operation (same input, three algorithms)", allocView_),
                   titledBox("Metrics and decisions", ins), allocBar_, nullptr);
    auto changed = [this] {
        if (loading_) return;
        loading_ = true;
        allocPreset_->setCurrentIndex(0);
        loading_ = false;
        rebuildAlloc();
    };
    connect(allocMode_, &QComboBox::currentIndexChanged, this, changed);
    connect(allocMem_, &QLineEdit::editingFinished, this, changed);
    connect(allocOps_, &QLineEdit::editingFinished, this, changed);
    connect(allocOs_, &QSpinBox::valueChanged, this, changed);
    connect(allocPreset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadAllocPreset(i - 1); });
    connect(allocFocus_, &QComboBox::currentIndexChanged, this, [this] { if (!alloc_.empty()) { allocView_->setResults({&alloc_[0], &alloc_[1], &alloc_[2]}, allocFocus_->currentIndex()); showAlloc(allocBar_->position(), false); } });
    connect(blBlock_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) showAlloc(allocBar_->position(), false); });
    connect(blOffset_, &QSpinBox::valueChanged, this, [this] { showAlloc(allocBar_->position(), false); });
    connect(allocBar_, &PlaybackBar::positionChanged, this, &MemAdvArea::showAlloc);
    connect(allocBar_, &PlaybackBar::resetRequested, this, [this] { rebuildAlloc(); });
    loadAllocPreset(0);
    return page;
}

bool MemAdvArea::loadAllocPreset(int i) {
    if (i < 0 || i >= (int)(sizeof kAllocPresets / sizeof kAllocPresets[0])) return false;
    const AllocPresetDef& p = kAllocPresets[i];
    loading_ = true;
    allocMode_->setCurrentIndex(p.fixed ? 1 : 0);
    allocMem_->setText(p.mem);
    allocOs_->setValue(p.os);
    allocOps_->setText(p.ops);
    allocPreset_->setCurrentIndex(i + 1);
    loading_ = false;
    bool ok = rebuildAlloc();
    if (ok) allocHdr_->setStatus(p.text);
    return ok;
}

bool MemAdvArea::rebuildAlloc() {
    alloc_.clear();
    allocView_->setResults({}, 0);
    try {
        std::vector<int> sizes;
        std::istringstream ss(allocMem_->text().replace(',', ' ').toStdString());
        std::string tok;
        while (ss >> tok) {
            size_t u;
            int v = std::stoi(tok, &u);
            if (u != tok.size() || v <= 0) throw std::invalid_argument("hole / partition sizes must be positive whole numbers");
            sizes.push_back(v);
        }
        if (sizes.empty() || sizes.size() > 40) throw std::invalid_argument("enter 1 to 40 hole / partition sizes");
        bool fixed = allocMode_->currentIndex() == 1;
        std::vector<MemBlock> init = fixed ? fixedPartitions(sizes, allocOs_->value()) : layoutFromHoles(sizes, allocOs_->value());
        std::vector<AllocOp> ops = parseAllocScript(allocOps_->text().toStdString());
        for (FitAlgo a : {FitAlgo::First, FitAlgo::Best, FitAlgo::Worst}) alloc_.push_back(runAllocation(a, init, ops, fixed));
    } catch (const std::exception& e) {
        alloc_.clear();
        allocBar_->setLength(0);
        allocHdr_->setStatus(QString("Invalid input: %1").arg(e.what()), ui::semError());
        return false;
    }
    allocView_->setResults({&alloc_[0], &alloc_[1], &alloc_[2]}, allocFocus_->currentIndex());
    allocBar_->setLength((int)alloc_[0].steps.size());
    showAlloc(0, false);
    emit logMessage(QString("Memory allocation: %1 operations; failures First %2, Best %3, Worst %4")
                        .arg(alloc_[0].steps.size()).arg(alloc_[0].metrics(9999).failures).arg(alloc_[1].metrics(9999).failures)
                        .arg(alloc_[2].metrics(9999).failures));
    return true;
}

void MemAdvArea::showAlloc(int step, bool fwd) {
    if (alloc_.empty()) return;
    allocView_->setStep(step, fwd ? allocBar_->animMs() : 0);
    const AllocResult& r = alloc_[allocFocus_->currentIndex()];
    AllocMetrics m = r.metrics(step);
    fillKv(allocMetrics_, {{"Algorithm", r.fixedPartitions ? "Fixed partitions (first free partition that fits)" : fitAlgoName(r.algo)},
                           {"Total memory", QString::number(m.total)},
                           {"Allocated (incl. internal waste)", QString::number(m.allocated)},
                           {"Requested by processes", QString::number(m.requested)},
                           {"Free", QString::number(m.freeBytes)},
                           {"Free holes / largest hole", QString("%1 / %2").arg(m.holes).arg(m.largestHole)},
                           {"Internal fragmentation", r.fixedPartitions ? QString::number(m.internalWaste) : "0 (variable partitions)"},
                           {"Failed requests", QString::number(m.failures)},
                           {"... failed although total free >= size", QString("%1 (external fragmentation)").arg(m.externalFailures)}});
    QString ex;
    if (step == 0) ex = "Initial memory. Press Next to run the first operation.";
    else for (const AllocResult& a : alloc_)
        ex += QString("<b>%1:</b> %2<br>").arg(a.fixedPartitions ? "Fixed" : fitAlgoName(a.algo)).arg(q(a.steps[step - 1].text).toHtmlEscaped());
    allocExplain_->setText(ex);
    // base / limit check on a block of the inspected algorithm
    const std::vector<MemBlock>& lay = step == 0 ? r.initial : r.steps[step - 1].layout;
    QString keep = blBlock_->currentText();
    loading_ = true;
    blBlock_->clear();
    for (const MemBlock& b : lay) if (!b.isHole() && b.owner != "OS" && b.owner.rfind("used", 0) != 0) blBlock_->addItem(q(b.owner));
    int ix = blBlock_->findText(keep);
    if (ix >= 0) blBlock_->setCurrentIndex(ix);
    loading_ = false;
    blResult_->clear();
    for (const MemBlock& b : lay)
        if (q(b.owner) == blBlock_->currentText() && !blBlock_->currentText().isEmpty()) {
            BaseLimitResult br = baseLimitCheck(b.start, b.size, blOffset_->value());
            blResult_->setText(QString("base = %1, limit = %2: %3").arg(b.start).arg(b.size).arg(q(br.message)));
            blResult_->setStyleSheet(QString("color:%1").arg(br.ok ? ui::hitColor().darker(130).name() : ui::semError().name()));
        }
    allocHdr_->setStatus(step == 0 ? QString("Step 0 of %1").arg(r.steps.size())
                                   : QString("Step %1 of %2: %3").arg(step).arg(r.steps.size()).arg(q(r.steps[step - 1].text)),
                         step > 0 && !r.steps[step - 1].ok ? ui::semError() : QColor());
}

// ================================================================ paging

struct AddrPreset { const char* name; uint64_t addr; bool write; };
static const AddrPreset kAddrPresets[] = {
    {"Reference: address 2500 -> 5572", 2500, false}, {"Zero address", 0, false}, {"Last byte of page 0 (1023)", 1023, false},
    {"Exact page boundary (1024)", 1024, false}, {"Valid page not resident (4101) -> page fault", 4101, false},
    {"Invalid page (8191)", 8191, false}, {"Outside the address space (8192)", 8192, false},
    {"Write to read-only page 1 (1100)", 1100, true},
};

QWidget* MemAdvArea::buildPaging() {
    pageHdr_ = new ModuleHeader("Paging and Address Translation",
        "page = address / page size, offset = address % page size, physical = frame x page size + offset. "
        "Illegal addresses, invalid pages and page faults are distinguished; nothing is mapped to frame 0 silently.");
    pagePreset_ = new QComboBox;
    pagePreset_->addItem("(custom)");
    for (const AddrPreset& p : kAddrPresets) pagePreset_->addItem(p.name);
    pageSize_ = new QComboBox;
    for (int s : {64, 128, 256, 512, 1024, 2048, 4096}) pageSize_->addItem(QString::number(s));
    pages_ = spin(1, 64, 8, "Logical pages");
    frames_ = spin(1, 64, 8, "Physical frames");
    addr_ = spin(0, 2000000000, 2500, "Logical address (decimal)");
    pageWrite_ = new QCheckBox("write access");
    pageShared_ = new QCheckBox("allow shared mappings (two pages -> one frame)");
    pageTable_ = new QTableWidget(0, 5);
    pageTable_->setHorizontalHeaderLabels({"Page", "Valid", "Present", "Frame", "Writable"});
    pageTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pageTable_->verticalHeader()->setVisible(false);
    pageTable_->setMinimumHeight(200);
    auto* cfg = new QWidget;
    auto* f = new QFormLayout(cfg);
    f->addRow("Preset:", pagePreset_);
    f->addRow("Page size (bytes):", pageSize_);
    f->addRow("Logical pages:", pages_);
    f->addRow("Physical frames:", frames_);
    f->addRow("Logical address:", addr_);
    f->addRow(pageWrite_);
    f->addRow(pageShared_);
    f->addRow(new QLabel("<b>Page table</b> (edit frames and bits)"));
    f->addRow(pageTable_);
    pageView_ = new TranslationView;
    pageSteps_ = new QListWidget;
    pageSteps_->setWordWrap(true);
    pageBar_ = new PlaybackBar;
    auto* page = new QWidget;
    assembleModule(page, pageHdr_, cfg, titledBox("MMU: logical -> physical", pageView_), titledBox("Translation steps", pageSteps_), pageBar_, nullptr);
    auto changed = [this] {
        if (loading_) return;
        loading_ = true;
        pagePreset_->setCurrentIndex(0);
        loading_ = false;
        rebuildPaging();
    };
    auto resize = [this, changed] {
        if (loading_) return;
        PagingConfig c;
        QString e;
        readPaging(c, e);
        c.table.resize(pages_->value());
        loading_ = true;
        fillPageTable(c);
        loading_ = false;
        changed();
    };
    connect(pageSize_, &QComboBox::currentIndexChanged, this, changed);
    connect(pages_, &QSpinBox::valueChanged, this, resize);
    connect(frames_, &QSpinBox::valueChanged, this, changed);
    connect(addr_, &QSpinBox::valueChanged, this, changed);
    connect(pageWrite_, &QCheckBox::toggled, this, changed);
    connect(pageShared_, &QCheckBox::toggled, this, changed);
    connect(pageTable_, &QTableWidget::itemChanged, this, changed);
    connect(pagePreset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPagingPreset(i - 1); });
    connect(pageBar_, &PlaybackBar::positionChanged, this, &MemAdvArea::showPaging);
    connect(pageBar_, &PlaybackBar::resetRequested, this, [this] { rebuildPaging(); });
    loadPagingPreset(0);
    return page;
}

void MemAdvArea::fillPageTable(const PagingConfig& c) {
    pageTable_->setRowCount((int)c.table.size());
    for (int i = 0; i < (int)c.table.size(); ++i) {
        const PageEntry& e = c.table[i];
        pageTable_->setItem(i, 0, ui::roItem(QString::number(i)));
        for (int k : {1, 2, 4}) {
            auto* it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            bool on = k == 1 ? e.valid : k == 2 ? e.present : e.writable;
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            pageTable_->setItem(i, k, it);
        }
        auto* fr = new QTableWidgetItem(e.frame >= 0 ? QString::number(e.frame) : QString("-"));
        fr->setTextAlignment(Qt::AlignCenter);
        pageTable_->setItem(i, 3, fr);
    }
}

bool MemAdvArea::readPaging(PagingConfig& c, QString& err) const {
    c.pageSize = pageSize_->currentText().toULongLong();
    c.logicalSize = c.pageSize * pages_->value();
    c.physicalSize = c.pageSize * frames_->value();
    c.allowShared = pageShared_->isChecked();
    c.table.assign(pageTable_->rowCount(), PageEntry{});
    for (int i = 0; i < pageTable_->rowCount(); ++i) {
        auto chk = [&](int k) { return pageTable_->item(i, k) && pageTable_->item(i, k)->checkState() == Qt::Checked; };
        c.table[i].valid = chk(1);
        c.table[i].present = chk(2);
        c.table[i].writable = chk(4);
        QString t = pageTable_->item(i, 3) ? pageTable_->item(i, 3)->text().trimmed() : "-";
        if (t == "-" || t.isEmpty()) c.table[i].frame = -1;
        else {
            bool ok = false;
            c.table[i].frame = t.toInt(&ok);
            if (!ok) { err = QString("page %1: frame '%2' is not a number").arg(i).arg(t); return false; }
        }
        if (c.table[i].valid && c.table[i].present && c.table[i].frame < 0) { err = QString("page %1 is present but has no frame").arg(i); return false; }
    }
    return true;
}

bool MemAdvArea::loadPagingPreset(int i) {
    if (i < 0 || i >= (int)(sizeof kAddrPresets / sizeof kAddrPresets[0])) return false;
    PagingConfig c = pagingPreset();
    loading_ = true;
    pageSize_->setCurrentText(QString::number(c.pageSize));
    pages_->setValue((int)c.table.size());
    frames_->setValue(int(c.physicalSize / c.pageSize));
    addr_->setValue((int)kAddrPresets[i].addr);
    pageWrite_->setChecked(kAddrPresets[i].write);
    pageShared_->setChecked(false);
    fillPageTable(c);
    pagePreset_->setCurrentIndex(i + 1);
    loading_ = false;
    return rebuildPaging();
}

bool MemAdvArea::rebuildPaging() {
    pageView_->setPaging(nullptr, nullptr);
    pageX_.reset();
    QString err;
    try {
        if (!readPaging(pageCfg_, err)) throw std::invalid_argument(err.toStdString());
        pageX_ = translatePaging(pageCfg_, (uint64_t)addr_->value(), pageWrite_->isChecked());
    } catch (const std::exception& e) {
        pageBar_->setLength(0);
        pageSteps_->clear();
        pageHdr_->setStatus(QString("Invalid configuration: %1").arg(e.what()), ui::semError());
        return false;
    }
    pageView_->setPaging(&pageCfg_, &*pageX_);
    pageSteps_->clear();
    for (const XlateStep& s : pageX_->steps) pageSteps_->addItem(q(s.title + ": " + s.detail));
    pageBar_->setLength((int)pageX_->steps.size());
    showPaging(0, false);
    emit logMessage("Paging: " + q(pageX_->message));
    return true;
}

void MemAdvArea::showPaging(int step, bool fwd) {
    if (!pageX_) return;
    pageView_->setStep(step, fwd ? pageBar_->animMs() : 0);
    highlightList(pageSteps_, step);
    bool end = step == (int)pageX_->steps.size();
    pageHdr_->setStatus(end ? QString("%1: %2").arg(xlateOutcomeName(pageX_->outcome)).arg(q(pageX_->message))
                            : QString("Translation step %1 of %2").arg(step).arg(pageX_->steps.size()),
                        end ? (pageX_->ok() ? ui::hitColor().darker(130) : ui::semError()) : QColor());
}

// ================================================================ segmentation

struct SegPresetDef { const char* name; int seg; int off; char acc; };
static const SegPresetDef kSegPresets[] = {
    {"Valid: segment 2, offset 250 -> 4250", 2, 250, 'r'}, {"Invalid: segment 2, offset 600 (limit 600)", 2, 600, 'r'},
    {"Last valid byte: segment 2, offset 599", 2, 599, 'r'}, {"No such segment (9)", 9, 0, 'r'},
    {"Write to the code segment (0)", 0, 10, 'w'},
};

QWidget* MemAdvArea::buildSeg() {
    segHdr_ = new ModuleHeader("Segmentation",
        "A logical address is (segment, offset). The segment table gives base and limit; offset >= limit traps "
        "(protection fault); otherwise physical = base + offset. Segments have different sizes.");
    segPreset_ = new QComboBox;
    segPreset_->addItem("(custom)");
    for (const SegPresetDef& p : kSegPresets) segPreset_->addItem(p.name);
    segPhys_ = spin(1024, 1 << 30, 8192, "Physical memory size");
    segNo_ = spin(0, 99, 2);
    segOff_ = spin(0, 1 << 30, 250);
    segAccess_ = new QComboBox;
    segAccess_->addItems({"read (r)", "write (w)", "execute (x)"});
    segTable_ = new QTableWidget(0, 7);
    segTable_->setHorizontalHeaderLabels({"Seg", "Name", "Base", "Limit", "R", "W", "X"});
    segTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    segTable_->verticalHeader()->setVisible(false);
    segTable_->setMinimumHeight(170);
    auto* cfg = new QWidget;
    auto* f = new QFormLayout(cfg);
    f->addRow("Preset:", segPreset_);
    f->addRow("Physical memory:", segPhys_);
    f->addRow("Segment s:", segNo_);
    f->addRow("Offset d:", segOff_);
    f->addRow("Access:", segAccess_);
    f->addRow(new QLabel("<b>Segment table</b>"));
    f->addRow(segTable_);
    segView_ = new TranslationView;
    segSteps_ = new QListWidget;
    segSteps_->setWordWrap(true);
    segBar_ = new PlaybackBar;
    auto* page = new QWidget;
    assembleModule(page, segHdr_, cfg, titledBox("Segment table lookup, limit check, base + offset", segView_),
                   titledBox("Translation steps", segSteps_), segBar_, nullptr);
    auto changed = [this] {
        if (loading_) return;
        loading_ = true;
        segPreset_->setCurrentIndex(0);
        loading_ = false;
        rebuildSeg();
    };
    for (QSpinBox* s : {segPhys_, segNo_, segOff_}) connect(s, &QSpinBox::valueChanged, this, changed);
    connect(segAccess_, &QComboBox::currentIndexChanged, this, changed);
    connect(segTable_, &QTableWidget::itemChanged, this, changed);
    connect(segPreset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadSegPreset(i - 1); });
    connect(segBar_, &PlaybackBar::positionChanged, this, &MemAdvArea::showSeg);
    connect(segBar_, &PlaybackBar::resetRequested, this, [this] { rebuildSeg(); });
    loadSegPreset(0);
    return page;
}

void MemAdvArea::fillSegTable(const std::vector<SegmentDesc>& s) {
    segTable_->setRowCount((int)s.size());
    for (int i = 0; i < (int)s.size(); ++i) {
        QStringList c = {QString::number(s[i].id), q(s[i].name), QString::number(s[i].base), QString::number(s[i].limit)};
        for (int k = 0; k < 4; ++k) { auto* it = new QTableWidgetItem(c[k]); it->setTextAlignment(Qt::AlignCenter); segTable_->setItem(i, k, it); }
        bool bits[3] = {s[i].read, s[i].write, s[i].exec};
        for (int k = 0; k < 3; ++k) {
            auto* it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(bits[k] ? Qt::Checked : Qt::Unchecked);
            segTable_->setItem(i, 4 + k, it);
        }
    }
}

bool MemAdvArea::readSegs(std::vector<SegmentDesc>& v, QString& err) const {
    v.clear();
    for (int i = 0; i < segTable_->rowCount(); ++i) {
        auto txt = [&](int k) { return segTable_->item(i, k) ? segTable_->item(i, k)->text().trimmed() : QString(); };
        auto chk = [&](int k) { return segTable_->item(i, k) && segTable_->item(i, k)->checkState() == Qt::Checked; };
        bool ok1, ok2, ok3;
        SegmentDesc s;
        s.id = txt(0).toInt(&ok1);
        s.name = txt(1).toStdString();
        s.base = txt(2).toULongLong(&ok2);
        s.limit = txt(3).toULongLong(&ok3);
        if (!ok1 || !ok2 || !ok3) { err = QString("segment row %1: id, base and limit must be whole numbers >= 0").arg(i + 1); return false; }
        s.read = chk(4); s.write = chk(5); s.exec = chk(6);
        v.push_back(s);
    }
    return true;
}

bool MemAdvArea::loadSegPreset(int i) {
    if (i < 0 || i >= (int)(sizeof kSegPresets / sizeof kSegPresets[0])) return false;
    loading_ = true;
    fillSegTable(segmentPreset());
    segPhys_->setValue(8192);
    segNo_->setValue(kSegPresets[i].seg);
    segOff_->setValue(kSegPresets[i].off);
    segAccess_->setCurrentIndex(kSegPresets[i].acc == 'w' ? 1 : kSegPresets[i].acc == 'x' ? 2 : 0);
    segPreset_->setCurrentIndex(i + 1);
    loading_ = false;
    return rebuildSeg();
}

bool MemAdvArea::rebuildSeg() {
    segView_->setSegmentation(nullptr, 0, nullptr, 0);
    segX_.reset();
    QString err;
    try {
        if (!readSegs(segs_, err)) throw std::invalid_argument(err.toStdString());
        char acc = "rwx"[segAccess_->currentIndex()];
        segX_ = translateSegment(segs_, (uint64_t)segPhys_->value(), segNo_->value(), (uint64_t)segOff_->value(), acc);
    } catch (const std::exception& e) {
        segBar_->setLength(0);
        segSteps_->clear();
        segHdr_->setStatus(QString("Invalid segment table: %1").arg(e.what()), ui::semError());
        return false;
    }
    segView_->setSegmentation(&segs_, (uint64_t)segPhys_->value(), &*segX_, segNo_->value());
    segSteps_->clear();
    for (const XlateStep& s : segX_->steps) segSteps_->addItem(q(s.title + ": " + s.detail));
    segBar_->setLength((int)segX_->steps.size());
    showSeg(0, false);
    emit logMessage("Segmentation: " + q(segX_->message));
    return true;
}

void MemAdvArea::showSeg(int step, bool fwd) {
    if (!segX_) return;
    segView_->setStep(step, fwd ? segBar_->animMs() : 0);
    highlightList(segSteps_, step);
    bool end = step == (int)segX_->steps.size();
    segHdr_->setStatus(end ? q(segX_->message) : QString("Translation step %1 of %2").arg(step).arg(segX_->steps.size()),
                       end ? (segX_->ok() ? ui::hitColor().darker(130) : ui::semError()) : QColor());
}

// ================================================================ demand paging

struct DpPresetDef { const char* name; const char* refs; int frames; const char* text; };
static const DpPresetDef kDpPresets[] = {
    {"Reference string (13 refs, 3 frames)", "7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2", 3,
     "Expected faults: FIFO 10, LRU 9, Optimal 7 (initial loads count as faults)."},
    {"Dirty pages and write-back", "1w, 2, 3w, 1, 4, 5, 2w, 1, 3", 3,
     "Writes set the dirty bit; evicting a dirty page costs a write-back to the backing store."},
    {"Edge: one frame", "1, 2, 1, 2, 1", 1, "With one frame every change of page is a fault."},
};

QWidget* MemAdvArea::buildDemand() {
    dpHdr_ = new ModuleHeader("Demand Paging",
        "Pages are loaded only when referenced. A fault: check the page table, take a free frame or a victim (the "
        "SAME FIFO / LRU / Optimal code as the Page replacement tab), write back a dirty victim, load, update, restart.");
    dpPreset_ = new QComboBox;
    dpPreset_->addItem("(custom)");
    for (const DpPresetDef& p : kDpPresets) dpPreset_->addItem(p.name);
    dpRefs_ = new QLineEdit;
    dpRefs_->setToolTip("Page numbers; add w for a write, e.g. 3w");
    dpFrames_ = spin(1, 16, 3);
    dpAlgo_ = new QComboBox;
    dpAlgo_->addItems({"FIFO", "LRU", "Optimal (offline baseline, needs the future)"});
    dpAccess_ = spin(1, 1000, 1, "Model cost of a memory access (logical units)");
    dpPageIn_ = spin(0, 100000, 100, "Model cost of reading a page from the backing store");
    dpWb_ = spin(0, 100000, 100, "Model cost of writing a dirty page back");
    auto* cfg = new QWidget;
    auto* f = new QFormLayout(cfg);
    f->addRow("Preset:", dpPreset_);
    f->addRow("References:", dpRefs_);
    f->addRow("Frames:", dpFrames_);
    f->addRow("Replacement:", dpAlgo_);
    f->addRow(new QLabel("<b>Cost model</b> (labelled parameters, not real latencies)"));
    f->addRow("Access:", dpAccess_);
    f->addRow("Page-in:", dpPageIn_);
    f->addRow("Write-back:", dpWb_);
    dpView_ = new DemandPagingView;
    dpActions_ = new QListWidget;
    dpActions_->setWordWrap(true);
    dpStats_ = kvTable();
    auto* ins = new QSplitter(Qt::Vertical);
    ins->addWidget(dpActions_);
    ins->addWidget(dpStats_);
    dpBar_ = new PlaybackBar;
    auto* page = new QWidget;
    assembleModule(page, dpHdr_, cfg, titledBox("Page table, RAM frames and backing store", dpView_),
                   titledBox("What the OS does for this reference", ins), dpBar_, nullptr);
    auto changed = [this] {
        if (loading_) return;
        loading_ = true;
        dpPreset_->setCurrentIndex(0);
        loading_ = false;
        rebuildDemand();
    };
    connect(dpRefs_, &QLineEdit::editingFinished, this, changed);
    connect(dpFrames_, &QSpinBox::valueChanged, this, changed);
    connect(dpAlgo_, &QComboBox::currentIndexChanged, this, [this] { dpIndex_ = dpAlgo_->currentIndex(); if (!dpAll_.empty()) { dpView_->setResult(&dpAll_[dpIndex_]); showDemand(dpBar_->position(), false); } });
    for (QSpinBox* s : {dpAccess_, dpPageIn_, dpWb_}) connect(s, &QSpinBox::valueChanged, this, [this] { showDemand(dpBar_->position(), false); });
    connect(dpPreset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadDemandPreset(i - 1); });
    connect(dpBar_, &PlaybackBar::positionChanged, this, &MemAdvArea::showDemand);
    connect(dpBar_, &PlaybackBar::resetRequested, this, [this] { rebuildDemand(); });
    loadDemandPreset(0);
    return page;
}

bool MemAdvArea::loadDemandPreset(int i) {
    if (i < 0 || i >= (int)(sizeof kDpPresets / sizeof kDpPresets[0])) return false;
    loading_ = true;
    dpRefs_->setText(kDpPresets[i].refs);
    dpFrames_->setValue(kDpPresets[i].frames);
    dpPreset_->setCurrentIndex(i + 1);
    loading_ = false;
    bool ok = rebuildDemand();
    if (ok) dpHdr_->setStatus(kDpPresets[i].text);
    return ok;
}

bool MemAdvArea::rebuildDemand() {
    dpView_->setResult(nullptr);
    dpAll_.clear();
    try {
        std::vector<DpRef> refs = parseDpRefs(dpRefs_->text().toStdString());
        if (refs.empty()) throw std::invalid_argument("the reference string is empty");
        for (PageAlgo a : {PageAlgo::FIFO, PageAlgo::LRU, PageAlgo::OPTIMAL}) dpAll_.push_back(runDemandPaging(a, refs, dpFrames_->value()));
    } catch (const std::exception& e) {
        dpAll_.clear();
        dpBar_->setLength(0);
        dpHdr_->setStatus(QString("Invalid input: %1").arg(e.what()), ui::semError());
        return false;
    }
    dpIndex_ = dpAlgo_->currentIndex();
    dpView_->setResult(&dpAll_[dpIndex_]);
    dpBar_->setLength((int)dpAll_[0].steps.size());
    showDemand(0, false);
    emit logMessage(QString("Demand paging (%1 frames): faults FIFO %2, LRU %3, Optimal %4")
                        .arg(dpFrames_->value()).arg(dpAll_[0].faults).arg(dpAll_[1].faults).arg(dpAll_[2].faults));
    return true;
}

void MemAdvArea::showDemand(int step, bool fwd) {
    if (dpAll_.empty()) return;
    const DpResult& r = dpAll_[dpIndex_];
    dpView_->setStep(step, fwd ? dpBar_->animMs() : 0);
    dpActions_->clear();
    if (step > 0) for (const std::string& a : r.steps[step - 1].actions) dpActions_->addItem(q(a));
    else dpActions_->addItem("Press Next: the first reference");
    int faults = 0, wbs = 0;
    for (int i = 0; i < step; ++i) { faults += !r.steps[i].hit; wbs += r.steps[i].victimDirty; }
    DpCost c{dpAccess_->value(), dpPageIn_->value(), dpWb_->value()};
    long cost = (long)step * c.access + (long)faults * c.pageIn + (long)wbs * c.writeBack;
    fillKv(dpStats_, {{"References so far", QString::number(step)}, {"Page faults", QString::number(faults)},
                      {"Hits", QString::number(step - faults)}, {"Dirty write-backs", QString::number(wbs)},
                      {"Model cost (logical units)", QString::number(cost)},
                      {"Final faults FIFO / LRU / Optimal", QString("%1 / %2 / %3").arg(dpAll_[0].faults).arg(dpAll_[1].faults).arg(dpAll_[2].faults)}});
    dpHdr_->setStatus(step == 0 ? QString("%1 references, %2 frames").arg(r.steps.size()).arg(r.frames)
                                : QString("Reference %1: page %2%3 -> %4").arg(step).arg(r.steps[step - 1].ref.page)
                                      .arg(r.steps[step - 1].ref.write ? " (write)" : "").arg(r.steps[step - 1].hit ? "HIT" : "PAGE FAULT"),
                      step > 0 ? (r.steps[step - 1].hit ? ui::hitColor().darker(130) : ui::semError()) : QColor());
}

// ================================================================ swapping

// Swapping presets (whole processes): RAM capacity, "name size" list, run order.
struct SwapPresetDef { const char* name; int cap; const char* procs; const char* order; const char* text; };
static const SwapPresetDef kSwapPresets[] = {
    {"Reference: 4 processes, RAM 100", 100, "A 40, B 50, C 30, D 20", "A B C A D B",
     "C does not fit next to A and B: the process loaded longest ago is swapped out, later swapped in again."},
    {"Everything fits (no swapping)", 200, "A 40, B 50, C 30, D 20", "A B C A D B",
     "RAM holds all four processes: every run after the first load is a hit, no swap-outs."},
    {"Ping-pong: two big processes, RAM 60", 60, "A 40, B 40", "A B A B A B",
     "Only one of the two fits: every switch swaps a whole process out and the other in (very expensive)."},
};

QWidget* MemAdvArea::buildSwap() {
    swHdr_ = new ModuleHeader("Swapping (whole processes)",
        "Unlike demand paging (single pages), swapping moves a WHOLE process between RAM and the backing store. "
        "Illustrative model: the process loaded longest ago is swapped out until the next one fits.");
    swCap_ = spin(10, 100000, 100, "RAM capacity (units)");
    swPreset_ = new QComboBox;
    swPreset_->addItem("(custom)");
    for (const SwapPresetDef& p : kSwapPresets) swPreset_->addItem(p.name);
    swProcs_ = new QLineEdit("A 40, B 50, C 30, D 20");
    swOrder_ = new QLineEdit("A B C A D B");
    auto* cfg = new QWidget;
    auto* f = new QFormLayout(cfg);
    f->addRow("Preset:", swPreset_);
    f->addRow("RAM capacity:", swCap_);
    f->addRow("Processes (name size):", swProcs_);
    f->addRow("Run order:", swOrder_);
    swView_ = new SwapView;
    swBar_ = new PlaybackBar;
    auto* page = new QWidget;
    assembleModule(page, swHdr_, cfg, titledBox("RAM and backing store", swView_), nullptr, swBar_, nullptr);
    auto swEdited = [this] { if (loading_) return; loading_ = true; swPreset_->setCurrentIndex(0); loading_ = false; rebuildSwap(); };
    connect(swCap_, &QSpinBox::valueChanged, this, swEdited);
    connect(swProcs_, &QLineEdit::editingFinished, this, swEdited);
    connect(swOrder_, &QLineEdit::editingFinished, this, swEdited);
    connect(swPreset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadSwapPreset(i - 1); });
    connect(swBar_, &PlaybackBar::positionChanged, this, [this](int s, bool fwd) {
        if (!swap_) return;
        swView_->setStep(s, fwd ? swBar_->animMs() : 0);
        swHdr_->setStatus(s == 0 ? QString("%1 runs").arg(swap_->steps.size())
                                 : QString("Step %1: %2 | swap-ins %3, swap-outs %4 in total").arg(s).arg(q(swap_->steps[s - 1].text))
                                       .arg(swap_->swapIns).arg(swap_->swapOuts));
    });
    connect(swBar_, &PlaybackBar::resetRequested, this, [this] { rebuildSwap(); });
    rebuildSwap();
    return page;
}

bool MemAdvArea::loadSwapPreset(int i) {
    if (i < 0 || i >= (int)(sizeof kSwapPresets / sizeof kSwapPresets[0])) return false;
    const SwapPresetDef& p = kSwapPresets[i];
    loading_ = true;
    swPreset_->setCurrentIndex(i + 1);
    swCap_->setValue(p.cap);
    swProcs_->setText(p.procs);
    swOrder_->setText(p.order);
    loading_ = false;
    bool ok = rebuildSwap();
    if (ok) swHdr_->setStatus(p.text);
    return ok;
}

QList<QPair<QString, QString>> MemAdvArea::presetList(const QString& page) {
    QList<QPair<QString, QString>> v;
    if (page == "allocation")
        for (const AllocPresetDef& p : kAllocPresets) v.append(qMakePair<QString, QString>(p.name, p.text));
    else if (page == "paging")
        for (const AddrPreset& p : kAddrPresets)
            v.append(qMakePair<QString, QString>(p.name, QString("Translate logical address %1 (%2) with the reference page table.").arg(p.addr).arg(p.write ? "write" : "read")));
    else if (page == "segmentation")
        for (const SegPresetDef& p : kSegPresets)
            v.append(qMakePair<QString, QString>(p.name, QString("Segment %1, offset %2, %3 access against the reference segment table.").arg(p.seg).arg(p.off)
                                  .arg(p.acc == 'w' ? "write" : p.acc == 'x' ? "execute" : "read")));
    else if (page == "demand")
        for (const DpPresetDef& p : kDpPresets) v.append(qMakePair<QString, QString>(p.name, p.text));
    else if (page == "swapping")
        for (const SwapPresetDef& p : kSwapPresets) v.append(qMakePair<QString, QString>(p.name, p.text));
    else if (page == "thrashing") {
        v.append(qMakePair<QString, QString>("Cyclic 0 1 2 3 x5, LRU, 3 vs 4 frames", "A loop over 4 pages with only 3 frames faults on every reference; one more frame removes almost all faults."));
        v.append(qMakePair<QString, QString>("Locality string, 3 vs 6 frames", "A seeded string with locality: the fault rate drops sharply once the working set fits."));
    }
    return v;
}

bool MemAdvArea::rebuildSwap() {
    swView_->setResult(nullptr);
    swap_.reset();
    try {
        std::vector<SwapProc> procs;
        for (const QString& item : swProcs_->text().split(',', Qt::SkipEmptyParts)) {
            QStringList t = item.simplified().split(' ');
            bool ok = false;
            if (t.size() != 2 || t[1].toInt(&ok) <= 0 || !ok) throw std::invalid_argument("processes: use 'name size', e.g. A 40");
            procs.push_back({t[0].toStdString(), t[1].toInt()});
        }
        std::vector<std::string> order;
        for (const QString& n : swOrder_->text().split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts)) order.push_back(n.toStdString());
        if (order.empty() || order.size() > 100) throw std::invalid_argument("run order: 1 to 100 process names");
        swap_ = runSwapping(swCap_->value(), procs, order);
    } catch (const std::exception& e) {
        swBar_->setLength(0);
        swHdr_->setStatus(QString("Invalid input: %1").arg(e.what()), ui::semError());
        return false;
    }
    swView_->setResult(&*swap_);
    swBar_->setLength((int)swap_->steps.size());
    swBar_->setPosition(0);
    return true;
}

// ================================================================ thrashing

QWidget* MemAdvArea::buildThrash() {
    thHdr_ = new ModuleHeader("Thrashing",
        "When a process has fewer frames than its working set, almost every reference faults and the system spends its "
        "time paging. Compare two frame counts on the same references; the bottom graph is a labelled logical model.");
    thPreset_ = new QComboBox;
    for (const auto& p : presetList("thrashing")) thPreset_->addItem(p.first);
    thMode_ = new QComboBox;
    thMode_->addItems({"Cyclic loop", "Locality (seeded random)"});
    thPages_ = spin(1, 64, 4, "Distinct pages");
    thRepeats_ = spin(1, 100, 5, "Cyclic: repetitions");
    thLen_ = spin(1, 2000, 60, "Locality: references");
    thLocality_ = spin(0, 100, 80, "Locality: % of next references within +-1 page");
    thSeed_ = spin(1, 99999, 3);
    thFramesA_ = spin(1, 32, 3);
    thFramesB_ = spin(1, 32, 4);
    thAlgo_ = new QComboBox;
    thAlgo_->addItems({"FIFO", "LRU", "Optimal"});
    thAlgo_->setCurrentIndex(1);
    thWindow_ = spin(1, 100, 8, "Rolling fault-rate window (references)");
    thWs_ = spin(1, 100, 6, "Working-set window (references)");
    mpFrames_ = spin(1, 512, 24, "Total frames");
    mpWs_ = spin(1, 64, 4, "Working set of each process (pages)");
    mpDegree_ = spin(1, 32, 12, "Largest degree of multiprogramming");
    mpRefs_ = spin(1, 5000, 200, "References per process");
    mpFault_ = spin(0, 1000, 10, "Fault service time (ticks, model parameter)");
    auto* cfg = new QWidget;
    auto* f = new QFormLayout(cfg);
    f->addRow("Preset:", thPreset_);
    f->addRow("References:", thMode_);
    f->addRow("Pages:", thPages_);
    f->addRow("Repetitions:", thRepeats_);
    f->addRow("Length:", thLen_);
    f->addRow("Locality %:", thLocality_);
    f->addRow("Seed:", thSeed_);
    f->addRow("Frames A:", thFramesA_);
    f->addRow("Frames B:", thFramesB_);
    f->addRow("Replacement:", thAlgo_);
    f->addRow("Fault-rate window:", thWindow_);
    f->addRow("Working-set window:", thWs_);
    f->addRow(new QLabel("<b>Multiprogramming model</b>"));
    f->addRow("Total frames:", mpFrames_);
    f->addRow("Working set / process:", mpWs_);
    f->addRow("Max degree:", mpDegree_);
    f->addRow("Refs / process:", mpRefs_);
    f->addRow("Fault time:", mpFault_);
    thView_ = new ThrashView;
    thBar_ = new PlaybackBar;
    auto* page = new QWidget;
    assembleModule(page, thHdr_, cfg, titledBox("Fault rate, working set and frames", thView_), nullptr, thBar_, nullptr);
    for (QSpinBox* s : {thPages_, thRepeats_, thLen_, thLocality_, thSeed_, thFramesA_, thFramesB_, thWindow_, thWs_, mpFrames_, mpWs_,
                        mpDegree_, mpRefs_, mpFault_})
        connect(s, &QSpinBox::valueChanged, this, [this] { if (!loading_) rebuildThrash(); });
    connect(thMode_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) rebuildThrash(); });
    connect(thAlgo_, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) rebuildThrash(); });
    connect(thPreset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_) loadThrashPreset(i); });
    connect(thBar_, &PlaybackBar::positionChanged, this, [this](int s, bool) {
        thView_->setStep(s);
        if (thA_ && thB_) {
            int fa = 0, fb = 0;
            for (int i = 0; i < s; ++i) { fa += thA_->points[i].fault; fb += thB_->points[i].fault; }
            thHdr_->setStatus(QString("After %1 references: %2 frames -> %3 faults, %4 frames -> %5 faults")
                                  .arg(s).arg(thA_->frames).arg(fa).arg(thB_->frames).arg(fb));
        }
    });
    connect(thBar_, &PlaybackBar::resetRequested, this, [this] { rebuildThrash(); });
    loadThrashPreset(0);
    return page;
}

bool MemAdvArea::loadThrashPreset(int i) {
    loading_ = true;
    thPreset_->setCurrentIndex(i);
    thMode_->setCurrentIndex(i == 0 ? 0 : 1);
    thPages_->setValue(i == 0 ? 4 : 10);
    thRepeats_->setValue(5);
    thLen_->setValue(60);
    thLocality_->setValue(80);
    thFramesA_->setValue(3);
    thFramesB_->setValue(i == 0 ? 4 : 6);
    thAlgo_->setCurrentIndex(1);
    loading_ = false;
    return rebuildThrash();
}

bool MemAdvArea::rebuildThrash() {
    thView_->setData(nullptr, nullptr, nullptr, 0);
    try {
        std::vector<int> refs = thMode_->currentIndex() == 0 ? cyclicRefs(thPages_->value(), thRepeats_->value())
                                                             : localityRefs(thLen_->value(), thPages_->value(), thLocality_->value(), (unsigned)thSeed_->value());
        PageAlgo a = (PageAlgo)thAlgo_->currentIndex();
        thA_ = runThrashing(refs, thFramesA_->value(), a, thWindow_->value(), thWs_->value());
        thB_ = runThrashing(refs, thFramesB_->value(), a, thWindow_->value(), thWs_->value());
        mp_ = multiprogrammingCurve(mpFrames_->value(), mpWs_->value(), mpDegree_->value(), mpRefs_->value(), mpFault_->value());
    } catch (const std::exception& e) {
        thA_.reset();
        thB_.reset();
        thBar_->setLength(0);
        thHdr_->setStatus(QString("Invalid input: %1").arg(e.what()), ui::semError());
        return false;
    }
    thView_->setData(&*thA_, &*thB_, &mp_, mpFault_->value());
    thBar_->setLength((int)thA_->points.size());
    thBar_->setPosition(thBar_->length());
    emit logMessage(QString("Thrashing: %1 refs, %2 frames -> %3 faults, %4 frames -> %5 faults")
                        .arg(thA_->refs.size()).arg(thA_->frames).arg(thA_->faults).arg(thB_->frames).arg(thB_->faults));
    return true;
}

// ================================================================ report

QString MemAdvArea::reportText() const {
    QString out = heap_->reportText();
    if (!alloc_.empty()) {
        out += "Contiguous allocation (" + allocOps_->text() + "):\n";
        for (const AllocResult& r : alloc_) {
            AllocMetrics m = r.metrics(9999);
            out += QString("  %1: failures %2 (external %3), free %4 in %5 holes, largest %6%7\n")
                       .arg(r.fixedPartitions ? "Fixed partitions" : fitAlgoName(r.algo)).arg(m.failures).arg(m.externalFailures)
                       .arg(m.freeBytes).arg(m.holes).arg(m.largestHole)
                       .arg(r.fixedPartitions ? QString(", internal waste %1").arg(m.internalWaste) : QString());
        }
    }
    if (pageX_) out += "Paging: " + q(pageX_->message) + "\n";
    if (segX_) out += "Segmentation: " + q(segX_->message) + "\n";
    if (!dpAll_.empty())
        out += QString("Demand paging (%1, %2 frames): faults FIFO %3, LRU %4, Optimal %5; write-backs FIFO %6\n")
                   .arg(dpRefs_->text()).arg(dpFrames_->value()).arg(dpAll_[0].faults).arg(dpAll_[1].faults).arg(dpAll_[2].faults)
                   .arg(dpAll_[0].writeBacks);
    if (swap_) out += QString("Swapping: %1 swap-ins, %2 swap-outs\n").arg(swap_->swapIns).arg(swap_->swapOuts);
    if (thA_ && thB_) out += QString("Thrashing: %1 frames -> %2 faults, %3 frames -> %4 faults (%5 references)\n")
                                 .arg(thA_->frames).arg(thA_->faults).arg(thB_->frames).arg(thB_->faults).arg(thA_->refs.size());
    return out.isEmpty() ? "(not run)\n" : out;
}

QString MemAdvArea::csvSection() const {
    QString out = "## Memory allocation\nalgorithm,failures,external_failures,free,holes,largest_hole,internal_waste\n";
    for (const AllocResult& r : alloc_) {
        AllocMetrics m = r.metrics(9999);
        out += QString("%1,%2,%3,%4,%5,%6,%7\n").arg(r.fixedPartitions ? "Fixed" : fitAlgoName(r.algo)).arg(m.failures)
                   .arg(m.externalFailures).arg(m.freeBytes).arg(m.holes).arg(m.largestHole).arg(m.internalWaste);
    }
    out += "\n## Address translation\nkind,input,outcome,physical\n";
    if (pageX_) out += QString("paging,%1,%2,%3\n").arg(pageX_->address).arg(xlateOutcomeName(pageX_->outcome)).arg(pageX_->ok() ? QString::number(pageX_->physical) : "");
    if (segX_) out += QString("segmentation,(%1 %2),%3,%4\n").arg(segNo_->value()).arg(segX_->offset).arg(xlateOutcomeName(segX_->outcome)).arg(segX_->ok() ? QString::number(segX_->physical) : "");
    out += "\n## Demand paging\nalgorithm,frames,faults,hits,write_backs\n";
    for (const DpResult& r : dpAll_)
        out += QString("%1,%2,%3,%4,%5\n").arg(pageAlgoName(r.algo)).arg(r.frames).arg(r.faults).arg(r.hits).arg(r.writeBacks);
    return out;
}
