// HeapLab.cpp - Memory > Heap Laboratory (see HeapLab.h).
#include "HeapLab.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <set>
#include "ModuleKit.h"
#include "Style.h"

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
bool sameBlock(const HeapBlock& a, const HeapBlock& b) {
    return a.offset == b.offset && a.size == b.size && a.free == b.free && a.requested == b.requested;
}
}

// ======================================================================= HeapMapView

HeapMapView::HeapMapView(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t_ = v.toDouble(); update(); });
}

void HeapMapView::setStep(const HeapStep* cur, const HeapStep* prev, HeapFamily family, int capacity, int animMs) {
    cur_ = cur;
    prevBlocks_ = prev ? prev->blocks : std::vector<HeapBlock>();
    family_ = family;
    capacity_ = capacity;
    anim_.stop();
    if (animMs > 0 && prev) { t_ = 0; anim_.setDuration(animMs); anim_.start(); }
    else t_ = 1;
    update();
}

// Rectangle of bytes [from, to) inside one row (the caller splits spans at row borders).
QRectF HeapMapView::byteRect(int from, int to, int row) const {
    int rows = (capacity_ + bytesPerRow_ - 1) / bytesPerRow_;
    double mapH = family_ == HeapFamily::Buddy ? height() * 0.62 : height() - 30.0;
    double rh = std::min(40.0, std::max(10.0, (mapH - 8) / rows));
    double left = 54, w = width() - left - 6;
    double x0 = left + w * (from - row * bytesPerRow_) / bytesPerRow_;
    double x1 = left + w * (to - row * bytesPerRow_) / bytesPerRow_;
    return QRectF(x0, 4 + row * rh, std::max(1.0, x1 - x0), rh - 4);
}

QList<QRectF> HeapMapView::spanRects(int from, int to) const {
    QList<QRectF> v;
    while (from < to) {
        int row = from / bytesPerRow_;
        int end = std::min(to, (row + 1) * bytesPerRow_);
        v << byteRect(from, end, row);
        from = end;
    }
    return v;
}

void HeapMapView::mousePressEvent(QMouseEvent* e) {
    if (!cur_) return;
    for (const HeapBlock& b : cur_->blocks)
        for (const QRectF& r : spanRects(b.offset, b.offset + b.size))
            if (r.contains(e->pos())) { emit blockClicked(b.offset); return; }
}

void HeapMapView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::WindowText);
    if (!cur_) { p.drawText(rect(), Qt::AlignCenter, "No heap"); return; }
    QFont f = font();
    f.setPointSizeF(std::max(6.5, f.pointSizeF() * 0.82));
    p.setFont(f);
    std::map<int, QString> nameOf;
    for (auto& kv : cur_->names) if (kv.second > 0) nameOf[kv.second] = q(kv.first);
    int rows = (capacity_ + bytesPerRow_ - 1) / bytesPerRow_;
    for (int r = 0; r < rows; ++r) {
        QRectF rr = byteRect(r * bytesPerRow_, std::min(capacity_, (r + 1) * bytesPerRow_), r);
        p.setPen(ink);
        p.drawText(QRectF(0, rr.top(), 50, rr.height()), Qt::AlignRight | Qt::AlignVCenter, QString("@%1").arg(r * bytesPerRow_));
    }
    auto fill = [&](int a, int b, const QBrush& br, const QString& label = QString(), const QColor& textCol = ui::darkInk()) {
        for (const QRectF& r : spanRects(a, b)) {
            p.setPen(Qt::NoPen);
            p.setBrush(br);
            p.drawRect(r);
            if (!label.isEmpty() && r.width() > p.fontMetrics().horizontalAdvance(label) + 4) {
                p.setPen(textCol);
                p.drawText(r, Qt::AlignCenter, label);
            }
        }
    };
    for (const HeapBlock& b : cur_->blocks) {
        int hdrEnd = b.offset + b.header;
        if (b.free) {
            fill(b.offset, hdrEnd, QColor("#9aa0a6"), "hdr");
            fill(hdrEnd, b.offset + b.size, QColor("#eef1f4"), QString("free %1 B").arg(b.size));
        } else {
            QColor c = ui::pidColor(b.handle - 1).lighter(120);
            fill(b.offset, hdrEnd, QColor("#4a4f55"), "hdr", Qt::white);
            QString nm = nameOf.count(b.handle) ? nameOf[b.handle] : QString("h%1").arg(b.handle);
            fill(hdrEnd, hdrEnd + b.requested, c, QString("%1: %2 B").arg(nm).arg(b.requested));
            if (b.padding > 0) {
                fill(hdrEnd + b.requested, b.offset + b.size, c.lighter(140));
                fill(hdrEnd + b.requested, b.offset + b.size, QBrush(QColor("#5f6368"), Qt::BDiagPattern));
            }
        }
        // outline the block
        for (const QRectF& r : spanRects(b.offset, b.offset + b.size)) {
            p.setPen(QPen(QColor("#5f6368"), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        }
        bool changed = true;
        for (const HeapBlock& o : prevBlocks_) if (sameBlock(o, b)) { changed = false; break; }
        if (changed && !prevBlocks_.empty() && t_ < 1) {
            QColor fl("#ffd54f");
            fl.setAlphaF(0.75 * (1 - t_));
            for (const QRectF& r : spanRects(b.offset, b.offset + b.size)) p.fillRect(r, fl);
        }
        if (b.offset == selected_)
            for (const QRectF& r : spanRects(b.offset, b.offset + b.size)) { p.setPen(QPen(ink, 2.5)); p.setBrush(Qt::NoBrush); p.drawRect(r.adjusted(1, 1, -1, -1)); }
    }
    // free-list links (address-ordered list): arcs from each free block to the next
    if (family_ == HeapFamily::FreeList) {
        p.setPen(QPen(QColor("#1a73e8"), 1.4));
        p.setBrush(Qt::NoBrush);
        for (size_t i = 0; i + 1 < cur_->freeList.size(); ++i) {
            QRectF a = spanRects(cur_->freeList[i], cur_->freeList[i] + 1).first(), b = spanRects(cur_->freeList[i + 1], cur_->freeList[i + 1] + 1).first();
            QPointF s(a.left() + 3, a.top() + 2), e(b.left() + 3, b.top() + 2);
            QPainterPath path(s);
            double lift = std::min(18.0, a.height() * 0.6);
            path.cubicTo(s + QPointF(0, -lift), e + QPointF(0, -lift), e);
            p.drawPath(path);
            p.drawLine(e, e + QPointF(-4, -6));
            p.drawLine(e, e + QPointF(4, -6));
        }
    }
    // realloc move
    if (cur_->movedFrom >= 0 && cur_->movedTo >= 0) {
        QRectF a = spanRects(cur_->movedFrom, cur_->movedFrom + 1).first(), b = spanRects(cur_->movedTo, cur_->movedTo + 1).first();
        QPointF s = a.center(), e = b.center();
        QPointF m = s + (e - s) * std::min(1.0, t_ * 1.2);
        p.setPen(QPen(ui::semError(), 2, Qt::DashLine));
        p.drawLine(s, m);
        p.setBrush(ui::semError());
        p.drawEllipse(m, 4, 4);
    }
    // buddy tree: one row per block size, split ancestors drawn as dashed outlines
    if (family_ == HeapFamily::Buddy) {
        int maxO = 0, minO = 30;
        while ((1 << maxO) < capacity_) ++maxO;
        for (const HeapBlock& b : cur_->blocks) minO = std::min(minO, b.order);
        double top = height() * 0.62 + 20, h = height() - top - 4;
        int levels = std::max(1, maxO - minO + 1);
        double lh = std::min(24.0, h / levels);
        double left = 54, w = width() - left - 6;
        std::set<std::pair<int, int>> split;
        for (const HeapBlock& b : cur_->blocks)
            for (int o = b.order + 1; o <= maxO; ++o) split.insert({o, b.offset & ~((1 << o) - 1)});
        p.setPen(ink);
        p.drawText(QRectF(left, top - 16, w, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   "Buddy tree: one row per block size; dashed = split into two buddies, filled = existing block");
        for (int lv = 0; lv < levels; ++lv) {
            int o = maxO - lv;
            double y = top + lv * lh;
            p.setPen(ink);
            p.drawText(QRectF(0, y + lh * 0.2, 50, lh), Qt::AlignRight | Qt::AlignVCenter, QString("%1 B").arg(1 << o));
            for (const auto& sp : split) if (sp.first == o) {
                QRectF r(left + w * sp.second / capacity_, y + lh * 0.25, w * (1 << o) / capacity_, lh * 0.7);
                p.setPen(QPen(QColor("#80868b"), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(r);
            }
            for (const HeapBlock& b : cur_->blocks) if (b.order == o) {
                QRectF r(left + w * b.offset / capacity_, y + lh * 0.25, w * b.size / capacity_, lh * 0.7);
                p.setPen(QPen(QColor("#5f6368"), 1));
                p.setBrush(b.free ? QColor("#eef1f4") : ui::pidColor(b.handle - 1).lighter(120));
                p.drawRect(r);
            }
        }
    }
    // legend
    p.setPen(ink);
    if (family_ == HeapFamily::FreeList)
        p.drawText(QRectF(4, height() - 18, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   "hdr = header (metadata)   colour = payload   hatched = padding   light = free   blue arcs = free list (address order)");
}

// ======================================================================= HeapLabPage

HeapLabPage::HeapLabPage(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Heap Laboratory",
        "Application-level heap allocation (malloc / calloc / realloc / free) inside a bounded byte arena owned by this program: "
        "headers, alignment, splitting, coalescing, free lists and a buddy allocator. Not the OS-level allocation of the other pages.");
    family_ = new QComboBox;
    family_->addItems({heapFamilyName(HeapFamily::FreeList), heapFamilyName(HeapFamily::Buddy)});
    family_->setToolTip("Changing the allocator family resets the heap and replays the operations from an empty arena");
    fit_ = new QComboBox;
    for (FitAlgo a : {FitAlgo::First, FitAlgo::Best, FitAlgo::Worst}) fit_->addItem(fitAlgoName(a));
    capacity_ = new QSpinBox;
    capacity_->setRange(128, 8192);
    capacity_->setSingleStep(128);
    capacity_->setValue(1024);
    capacity_->setSuffix(" B");
    align_ = new QComboBox;
    align_->addItems({"8", "16", "32"});
    align_->setCurrentIndex(1);
    preset_ = new QComboBox;
    preset_->addItem("(custom script)");
    for (const HeapPreset& p : heapPresets()) preset_->addItem(q(p.name));
    presetText_ = new QLabel;
    presetText_->setWordWrap(true);
    script_ = new QPlainTextEdit;
    script_->setToolTip("One operation per line or separated by ';':  a = malloc 100 | b = calloc 4 25 | a = realloc a 200 | free a | "
                        "write a 0 text | read a 0 4 | overflow a 8");
    script_->setMinimumHeight(70);
    auto* bLoad = new QPushButton("Run script from an empty heap");
    op_ = new QComboBox;
    op_->addItems({"malloc", "calloc", "realloc", "free", "write", "read", "overflow (demo)"});
    name_ = new QLineEdit("p");
    name_->setToolTip("Variable that holds the handle (e.g. p)");
    size_ = new QSpinBox;
    size_->setRange(0, 100000);
    size_->setValue(64);
    size_->setToolTip("Bytes (malloc / realloc / read count / overflow bytes) or element size (calloc)");
    count_ = new QSpinBox;
    count_->setRange(0, 100000);
    count_->setValue(4);
    count_->setToolTip("calloc: number of elements; write / read: payload offset");
    text_ = new QLineEdit("hello");
    text_->setToolTip("write: the text stored in the payload");
    auto* bApply = new QPushButton("Apply operation");
    bApply->setToolTip("Appended after the current step (after Previous this starts a new branch)");
    auto* bUndo = new QPushButton("Undo last operation");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow("Allocator:", family_);
    cf->addRow("Fit policy:", fit_);
    cf->addRow("Arena:", capacity_);
    cf->addRow("Alignment:", align_);
    cf->addRow("Preset:", preset_);
    cf->addRow(presetText_);
    cf->addRow(new QLabel("<b>Operations</b>"));
    cf->addRow(script_);
    cf->addRow(bLoad);
    cf->addRow(new QLabel("<b>Manual operation</b>"));
    cf->addRow("Operation:", op_);
    cf->addRow("Variable:", name_);
    cf->addRow("Size / bytes:", size_);
    cf->addRow("Count / offset:", count_);
    cf->addRow("Text:", text_);
    auto* ob = new QWidget;
    auto* og = new QGridLayout(ob);
    og->setContentsMargins(0, 0, 0, 0);
    og->addWidget(bApply, 0, 0);
    og->addWidget(bUndo, 0, 1);
    cf->addRow(ob);

    map_ = new HeapMapView;
    zoom_ = new QSpinBox;
    zoom_->setRange(32, 8192);
    zoom_->setSingleStep(64);
    zoom_->setValue(256);
    zoom_->setSuffix(" bytes per row");
    zoom_->setToolTip("Zoom: fewer bytes per row make tiny blocks wider");
    auto* centerW = new QWidget;
    auto* cl = new QVBoxLayout(centerW);
    cl->setContentsMargins(0, 0, 0, 0);
    auto* zl = new QHBoxLayout;
    zl->addWidget(new QLabel("Heap map (address order):"));
    zl->addStretch();
    zl->addWidget(zoom_);
    cl->addLayout(zl);
    cl->addWidget(map_, 1);

    blockInfo_ = new QLabel("Click a block in the map.");
    blockInfo_->setWordWrap(true);
    bytes_ = new QPlainTextEdit;
    bytes_->setReadOnly(true);
    bytes_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bytes_->setMinimumHeight(60);
    metrics_ = new QLabel;
    metrics_->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>Selected block</b>"));
    il->addWidget(blockInfo_);
    il->addWidget(bytes_, 1);
    il->addWidget(new QLabel("<b>Metrics</b>"));
    il->addWidget(metrics_);

    bar_ = new PlaybackBar;
    auto* bottom = new QTabWidget;
    history_ = new QTableWidget(0, 4);
    history_->setHorizontalHeaderLabels({"Step", "Operation", "Result", "Details"});
    history_->verticalHeader()->setVisible(false);
    history_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    history_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    history_->horizontalHeader()->setStretchLastSection(true);
    bottom->addTab(history_, "Allocation history");
    auto* chk = new QWidget;
    auto* kl = new QHBoxLayout(chk);
    integrity_ = new QPlainTextEdit;
    integrity_->setReadOnly(true);
    leaks_ = new QTableWidget(0, 3);
    leaks_->setHorizontalHeaderLabels({"Variable", "Handle", "Bytes"});
    leaks_->verticalHeader()->setVisible(false);
    leaks_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    leaks_->horizontalHeader()->setStretchLastSection(true);
    auto* lw = new QWidget;
    auto* ll = new QVBoxLayout(lw);
    ll->setContentsMargins(0, 0, 0, 0);
    auto* lt = new QLabel("<b>Outstanding allocations</b> (at the end of the session: potential leaks - not proof that the memory is unreachable)");
    lt->setWordWrap(true);
    ll->addWidget(lt);
    ll->addWidget(leaks_);
    kl->addWidget(integrity_, 1);
    kl->addWidget(lw, 1);
    bottom->addTab(chk, "Integrity check and leaks");

    assembleModule(this, header_, cfgW, centerW, titledBox("Inspector", insW), bar_, bottom);

    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    for (QComboBox* c : {family_, fit_, align_})
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            if (loading_) return;
            if (rebuild()) header_->setStatus("Configuration changed: the heap was reset and the operations replayed from an empty arena", ui::semReady());
        });
    connect(capacity_, &QSpinBox::editingFinished, this, [this] { if (!loading_) rebuild(); });
    connect(bLoad, &QPushButton::clicked, this, [this] {
        lines_ = splitHeapScript(script_->toPlainText().toStdString());
        rebuild();
    });
    connect(bApply, &QPushButton::clicked, this, [this] {
        QString n = name_->text().trimmed().isEmpty() ? "p" : name_->text().trimmed();
        QString o = op_->currentText();
        QString line;
        if (o == "malloc") line = QString("%1 = malloc %2").arg(n).arg(size_->value());
        else if (o == "calloc") line = QString("%1 = calloc %2 %3").arg(n).arg(count_->value()).arg(size_->value());
        else if (o == "realloc") line = QString("%1 = realloc %1 %2").arg(n).arg(size_->value());
        else if (o == "free") line = "free " + n;
        else if (o == "write") line = QString("write %1 %2 %3").arg(n).arg(count_->value()).arg(text_->text());
        else if (o == "read") line = QString("read %1 %2 %3").arg(n).arg(count_->value()).arg(size_->value());
        else line = QString("overflow %1 %2").arg(n).arg(std::min(64, std::max(1, size_->value())));
        applyOp(line);
    });
    connect(bUndo, &QPushButton::clicked, this, [this] { undo(); });
    connect(zoom_, &QSpinBox::valueChanged, map_, &HeapMapView::setBytesPerRow);
    connect(map_, &HeapMapView::blockClicked, this, &HeapLabPage::selectBlock);
    connect(bar_, &PlaybackBar::positionChanged, this, &HeapLabPage::onStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(); });
    loadPreset(0);
}

bool HeapLabPage::readConfig(HeapConfig& c, QString& err) const {
    c.family = (HeapFamily)family_->currentIndex();
    c.fit = (FitAlgo)fit_->currentIndex();
    c.capacity = capacity_->value();
    c.alignment = align_->currentText().toInt();
    try { validateHeapConfig(c); } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

void HeapLabPage::setFit(FitAlgo a) { fit_->setCurrentIndex((int)a); }
void HeapLabPage::setFamily(HeapFamily f) { family_->setCurrentIndex((int)f); }

bool HeapLabPage::loadPreset(int i) {
    if (i < 0 || i >= (int)heapPresets().size()) return false;
    HeapPreset p = heapPresets()[i];
    loading_ = true;
    family_->setCurrentIndex((int)p.cfg.family);
    fit_->setCurrentIndex((int)p.cfg.fit);
    capacity_->setValue(p.cfg.capacity);
    align_->setCurrentText(QString::number(p.cfg.alignment));
    preset_->setCurrentIndex(i + 1);
    presetText_->setText(q(p.description));
    zoom_->setValue(p.cfg.capacity <= 512 ? 128 : 256);
    loading_ = false;
    lines_ = splitHeapScript(p.script);
    bool ok = rebuild();
    emit logMessage(QString("Heap Laboratory: preset '%1'").arg(q(p.name)));
    return ok;
}

bool HeapLabPage::rebuild() {
    HeapConfig c;
    QString err;
    if (!readConfig(c, err)) {
        header_->setStatus("Invalid configuration: " + err, ui::semError());
        ui::warn(this, "Heap configuration: " + err);
        return false;
    }
    QStringList sl;
    for (const std::string& l : lines_) sl << q(l);
    loading_ = true;
    script_->setPlainText(sl.join("\n"));
    loading_ = false;
    session_ = runHeapScript(c, lines_);
    history_->setRowCount((int)session_.steps.size());
    for (int i = 0; i < (int)session_.steps.size(); ++i) {
        const HeapStep& s = session_.steps[i];
        QStringList ev;
        for (const std::string& e : s.result.events) ev << q(e);
        QStringList cells = {QString::number(i), q(s.op), s.result.ok ? "ok" : "FAILED", q(s.result.message) + (ev.isEmpty() ? QString() : "  [" + ev.join("; ") + "]")};
        for (int k = 0; k < 4; ++k) {
            auto* it = ui::roItem(cells[k]);
            if (k == 3 || k == 1) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (k == 2) ui::shade(it, s.result.ok ? ui::lightGreen() : ui::lightRed());
            history_->setItem(i, k, it);
        }
    }
    bar_->setLength((int)session_.steps.size() - 1);
    bar_->setPosition((int)session_.steps.size() - 1);
    return true;
}

bool HeapLabPage::applyOp(const QString& line) {
    int pos = bar_->position();
    if (pos < (int)lines_.size()) lines_.resize(pos);        // new branch after Previous
    lines_.push_back(line.toStdString());
    if (!rebuild()) return false;
    const HeapStep& s = session_.steps.back();
    header_->setStatus(q(s.result.message), s.result.ok ? ui::hitColor().darker(130) : ui::semError());
    emit logMessage("Heap Laboratory: " + line + " -> " + q(s.result.message));
    return s.result.ok;
}

bool HeapLabPage::undo() {
    if (lines_.empty()) return false;
    lines_.pop_back();
    return rebuild();
}

const HeapStep* HeapLabPage::current() const {
    int p = bar_->position();
    return p >= 0 && p < (int)session_.steps.size() ? &session_.steps[p] : nullptr;
}

void HeapLabPage::onStep(int pos, bool forward) {
    if (pos < 0 || pos >= (int)session_.steps.size()) return;
    const HeapStep& s = session_.steps[pos];
    map_->setStep(&s, pos > 0 ? &session_.steps[pos - 1] : nullptr, session_.cfg.family, session_.cfg.capacity, forward ? bar_->animMs() : 0);
    for (int i = 0; i < history_->rowCount(); ++i)
        for (int k = 0; k < history_->columnCount(); ++k)
            if (auto* it = history_->item(i, k)) {
                QFont f = it->font();
                f.setBold(i == pos);
                it->setFont(f);
            }
    if (pos < history_->rowCount()) history_->scrollToItem(history_->item(pos, 0));
    header_->setStatus(QString("Step %1 of %2: %3").arg(pos).arg(session_.steps.size() - 1).arg(q(s.result.message)),
                       s.result.ok ? QColor() : ui::semError());
    bool found = false;
    for (const HeapBlock& b : s.blocks) if (b.offset == selected_) found = true;
    if (!found) selected_ = s.blocks.empty() ? -1 : s.blocks.front().offset;
    showInspector();
}

void HeapLabPage::selectBlock(int offset) {
    selected_ = offset;
    showInspector();
}

void HeapLabPage::showInspector() {
    const HeapStep* s = current();
    if (!s) return;
    map_->setSelected(selected_);
    const HeapBlock* b = nullptr;
    for (const HeapBlock& x : s->blocks) if (x.offset == selected_) b = &x;
    std::map<int, QString> nameOf;
    for (auto& kv : s->names) if (kv.second > 0) nameOf[kv.second] = q(kv.first);
    if (b) {
        QString t = QString("Block @%1, %2 B (header %3 B @%1..%4)").arg(b->offset).arg(b->size).arg(b->header).arg(b->offset + b->header - 1);
        if (b->free) t += QString("<br><b>free</b>; next free block: %1").arg(b->nextFree < 0 ? QString("none (end of list)") : QString("@%1").arg(b->nextFree));
        else t += QString("<br><b>allocated</b> to %1 (handle %2): payload @%3, %4 B requested, %5 B padding")
                      .arg(nameOf.count(b->handle) ? nameOf[b->handle] : "?").arg(b->handle).arg(b->payloadOffset()).arg(b->requested).arg(b->padding);
        if (session_.cfg.family == HeapFamily::Buddy) t += QString("<br>buddy order %1 (%2 B); its buddy is @%3 (offset XOR size)").arg(b->order).arg(b->size).arg(b->offset ^ b->size);
        blockInfo_->setText(t);
        QString hex;
        int from = b->offset, to = std::min(b->offset + b->size, b->offset + 128);
        for (int row = from; row < to; row += 16) {
            hex += QString("@%1  ").arg(row, 5);
            QString asc;
            for (int k = row; k < row + 16 && k < to; ++k) {
                unsigned char c = s->arena[k];
                hex += QString("%1 ").arg(c, 2, 16, QChar('0'));
                asc += c >= 32 && c < 127 ? QChar(c) : QChar('.');
            }
            hex += " " + asc + "\n";
        }
        if (b->size > 128) hex += QString("... (%1 more bytes)").arg(b->size - 128);
        bytes_->setPlainText(hex);
    } else { blockInfo_->setText("Click a block in the map."); bytes_->clear(); }
    const HeapMetrics& m = s->metrics;
    metrics_->setText(QString("Live payload requested: %1 B in %2 allocation(s)<br>Overhead: %3 B headers (all blocks) + %4 B padding<br>"
                              "Free: %5 B in %6 block(s), largest free block %7 B (largest request now: %8 B)<br>"
                              "Fragmentation indicator 1 - largest/free: %9<br>Allocations %10, frees %11, failures %12")
                          .arg(m.liveRequested).arg(m.liveAllocations).arg(m.headerBytes).arg(m.paddingBytes).arg(m.freeBytes).arg(s->freeList.size())
                          .arg(m.largestFree).arg(m.largestRequest).arg(m.fragDefined ? QString::number(m.fragIndicator, 'f', 2) : QString("not defined (nothing free)"))
                          .arg(m.allocations).arg(m.frees).arg(m.failures));
    integrity_->setPlainText(s->integrity.empty() ? QString("Integrity check after step %1: OK\n- blocks cover the arena without gaps or overlap\n- every header "
                                                            "is valid and every payload aligned\n- the free list matches the free blocks%2\n- headers + payload + "
                                                            "padding + free = %3 B (accounting identity)")
                                                        .arg(bar_->position()).arg(session_.cfg.family == HeapFamily::FreeList ? ", no two adjacent free blocks" : ", no unmerged free buddies")
                                                        .arg(session_.cfg.capacity)
                                                  : "Integrity check FAILED:\n- " + q(s->integrity.front()) + "\nThe heap refuses further operations until it is reset.");
    leaks_->setRowCount(0);
    for (const HeapBlock& x : s->blocks) {
        if (x.free) continue;
        int r = leaks_->rowCount();
        leaks_->insertRow(r);
        leaks_->setItem(r, 0, ui::roItem(nameOf.count(x.handle) ? nameOf[x.handle] : "?"));
        leaks_->setItem(r, 1, ui::roItem(QString::number(x.handle)));
        leaks_->setItem(r, 2, ui::roItem(QString::number(x.requested)));
    }
}

QString HeapLabPage::reportText() const {
    if (session_.steps.empty()) return "(not run)\n";
    const HeapStep& s = session_.steps.back();
    QString t = QString("Heap Laboratory: %1, %2, arena %3 B, alignment %4\n").arg(heapFamilyName(session_.cfg.family), fitAlgoName(session_.cfg.fit))
                    .arg(session_.cfg.capacity).arg(session_.cfg.alignment);
    for (size_t i = 1; i < session_.steps.size(); ++i)
        t += QString("  %1. %2 -> %3\n").arg(i).arg(q(session_.steps[i].op), q(session_.steps[i].result.message));
    t += QString("  final: live %1 B, headers %2 B, padding %3 B, free %4 B (largest %5 B), failures %6, outstanding allocations %7\n")
             .arg(s.metrics.liveRequested).arg(s.metrics.headerBytes).arg(s.metrics.paddingBytes).arg(s.metrics.freeBytes).arg(s.metrics.largestFree)
             .arg(s.metrics.failures).arg(s.metrics.liveAllocations);
    return t;
}
