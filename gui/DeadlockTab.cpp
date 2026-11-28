// DeadlockTab.cpp - matrices in, safe sequence / deadlocked set / RAG out.
#include "DeadlockTab.h"
#include <QComboBox>
#include <QFileInfo>
#include <QRegularExpression>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include "PanelLayout.h"
#include "ModuleKit.h"
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include "RagView.h"
#include "Style.h"
#include "io_formats.h"

// Creates a compact matrix table.
static QTableWidget* makeMatrix(bool editable) {
    auto* t = new QTableWidget;
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->verticalHeader()->setDefaultSectionSize(24);
    if (!editable) t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setMinimumHeight(100);
    t->horizontalHeader()->setMinimumSectionSize(28);
    return t;
}

// Puts a table under a small bold title.
static QWidget* titled(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    auto* lab = new QLabel("<b>" + title + "</b>");
    l->addWidget(lab);
    l->addWidget(w);
    return box;
}

DeadlockTab::DeadlockTab(QWidget* parent) : QWidget(parent) {
    // ---- sizes and sample buttons ----
    procSpin_ = new QSpinBox; procSpin_->setRange(1, 10); procSpin_->setValue(5);
    resSpin_ = new QSpinBox;  resSpin_->setRange(1, 6);   resSpin_->setValue(3);
    auto* bankBtn = new QPushButton("Load Banker's sample");
    auto* deadBtn = new QPushButton("Load deadlock sample");
    auto* clrBtn = new QPushButton("Clear");
    auto* pidBtn = new QPushButton("Use Scheduling tab pids");
    pidBtn->setToolTip("Label the rows with the processes of the Scheduling tab (shared process list)");
    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(new QLabel("Processes:")); top->addWidget(procSpin_);
    top->addWidget(new QLabel("Resources:")); top->addWidget(resSpin_);
    top->addStretch();
    top->addWidget(bankBtn); top->addWidget(deadBtn); top->addWidget(pidBtn); top->addWidget(clrBtn);

    // ---- matrices ----
    allocT_ = makeMatrix(true);
    maxT_ = makeMatrix(true);
    needT_ = makeMatrix(false);
    reqT_ = makeMatrix(true);
    totalT_ = makeMatrix(true);                         // total instances of each resource (input)
    totalT_->setMinimumHeight(56);
    totalT_->setMaximumHeight(60);
    availT_ = makeMatrix(false);                        // read-only: Total - sum of Allocation column
    availT_->setMinimumHeight(56);
    availT_->setMaximumHeight(60);
    auto* matBox = new QGroupBox("State (rows = processes, columns = resource types)");
    auto* mg = new QGridLayout(matBox);
    mg->setSpacing(8);
    mg->addWidget(titled("Allocation", allocT_), 0, 0);
    mg->addWidget(titled("Max", maxT_), 0, 1);
    mg->addWidget(titled("Need = Max - Allocation", needT_), 0, 2);
    mg->addWidget(titled("Request (for detection)", reqT_), 1, 0);
    auto* availBox = new QWidget;
    auto* al = new QVBoxLayout(availBox);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(8);
    al->addWidget(titled("Total instances", totalT_));
    al->addWidget(titled("Available = Total - sum(Allocation)", availT_));
    al->addStretch();
    mg->addWidget(availBox, 1, 1);

    // ---- actions ----
    auto* actBox = new QGroupBox("Actions");
    auto* safeBtn = new QPushButton("Check safety");
    auto* reqBtn = new QPushButton("Request resources");
    auto* detBtn = new QPushButton("Detect deadlock");
    auto* recBtn = new QPushButton("Recover");
    actionBtns_ = {safeBtn, reqBtn, detBtn, recBtn, bankBtn, deadBtn, clrBtn, pidBtn};
    reqPid_ = new QComboBox;
    reqEdit_ = new QLineEdit("1 0 2");
    reqEdit_->setPlaceholderText("e.g. 1 0 2");
    auto* ag = new QGridLayout(actBox);
    ag->setSpacing(8);
    ag->addWidget(safeBtn, 0, 0); ag->addWidget(detBtn, 0, 1); ag->addWidget(recBtn, 0, 2);
    auto* rq = new QHBoxLayout;
    rq->addWidget(new QLabel("Request by:"));
    rq->addWidget(reqPid_); rq->addWidget(reqEdit_, 1); rq->addWidget(reqBtn);
    ag->addLayout(rq, 1, 0, 1, 3);
    mg->addWidget(actBox, 2, 0, 1, 3);                  // actions on their own full-width row
    for (int c = 0; c < 3; ++c) mg->setColumnStretch(c, 1);
    mg->setRowStretch(0, 1);
    mg->setRowStretch(1, 1);

    // ---- results: status, trace, RAG ----
    status_ = new QLabel("Load a sample or enter matrices, then choose an action.");
    status_->setWordWrap(true);
    QFont big = status_->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    big.setBold(true);
    status_->setFont(big);
    trace_ = new QListWidget;
    trace_->setWordWrap(true);
    rag_ = new RagView;
    auto* resBox = new QGroupBox("Result");
    auto* rl = new QVBoxLayout(resBox);
    rl->addWidget(status_);
    rl->addWidget(trace_);
    auto* ragBox = new QGroupBox("Resource-allocation graph");
    (new QVBoxLayout(ragBox))->addWidget(rag_);

    auto* right = new PanelSplitter(Qt::Vertical, "Deadlock/v");
    right->addPanel(ragBox, PanelSplitter::Center, 3);
    right->addPanel(ConfigPanel::wrap(resBox, "detailPanel"), PanelSplitter::Detail, 2);
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(8);
    ll->addLayout(top);
    ll->addWidget(matBox, 1);
    // The matrices are the input of this module: a scrollable configuration panel that may take
    // up to about half of the width by default.
    auto* split = new PanelSplitter(Qt::Horizontal, "Deadlock/h");
    split->addPanel(new ConfigPanel(withSampleSlot(left)), PanelSplitter::Config, 1, 0.55);
    split->addPanel(right, PanelSplitter::Center);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(split);

    connect(procSpin_, &QSpinBox::valueChanged, this, &DeadlockTab::onSizeChanged);
    connect(resSpin_, &QSpinBox::valueChanged, this, &DeadlockTab::onSizeChanged);
    connect(bankBtn, &QPushButton::clicked, this, [this] { loadFile(ui::samplesDir() + "/banker_sample.txt"); });
    connect(deadBtn, &QPushButton::clicked, this, [this] { loadFile(ui::samplesDir() + "/deadlock_sample.txt"); });
    connect(clrBtn, &QPushButton::clicked, this, &DeadlockTab::clearAll);
    connect(pidBtn, &QPushButton::clicked, this, &DeadlockTab::wantProcessPids);
    connect(safeBtn, &QPushButton::clicked, this, [this] { checkSafety(); });
    connect(reqBtn, &QPushButton::clicked, this, [this] { requestResources(reqPid_->currentIndex(), reqEdit_->text()); });
    connect(detBtn, &QPushButton::clicked, this, [this] { detect(); });
    connect(recBtn, &QPushButton::clicked, this, [this] { recover(); });
    for (QTableWidget* t : {allocT_, maxT_, reqT_, totalT_})
        connect(t, &QTableWidget::itemChanged, this, [this] { updateNeed(); updateAvail(); redraw(); });

    onSizeChanged();
}

QString DeadlockTab::pidName(int row) const {
    return QString("P%1").arg(row < (int)rowPids_.size() ? rowPids_[row] : row);
}

QString DeadlockTab::pidList(const std::vector<int>& rows) const {
    QStringList l;
    for (int r : rows) l << pidName(r);
    return l.join(", ");
}

// Resizes every table to the chosen number of processes / resources (new cells = 0).
void DeadlockTab::onSizeChanged() {
    int n = procSpin_->value(), m = resSpin_->value();
    if ((int)rowPids_.size() != n) {
        rowPids_.resize(n);
        for (int i = 0; i < n; ++i) rowPids_[i] = i;
    }
    QStringList cols, rows;
    for (int j = 0; j < m; ++j) cols << QString(QChar('A' + j));    // resource types A, B, C ...
    for (int i = 0; i < n; ++i) rows << pidName(i);
    for (QTableWidget* t : {allocT_, maxT_, needT_, reqT_, availT_, totalT_}) {
        bool oneRow = (t == availT_ || t == totalT_);
        t->blockSignals(true);
        t->setRowCount(oneRow ? 1 : n);
        t->setColumnCount(m);
        t->setHorizontalHeaderLabels(cols);
        if (!oneRow) t->setVerticalHeaderLabels(rows);
        else t->setVerticalHeaderLabels({t == availT_ ? "avail" : "total"});
        for (int i = 0; i < t->rowCount(); ++i)
            for (int j = 0; j < m; ++j)
                if (!t->item(i, j)) {
                    auto* it = new QTableWidgetItem("0");
                    it->setTextAlignment(Qt::AlignCenter);
                    if (t == needT_ || t == availT_) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                    t->setItem(i, j, it);
                }
        t->blockSignals(false);
    }
    reqPid_->clear();
    for (int i = 0; i < n; ++i) reqPid_->addItem(pidName(i));
    killed_.clear();
    deadlocked_.clear();
    highlightRows({}, QColor());
    updateNeed();
    updateAvail();
    redraw();
}

void DeadlockTab::setSize(int procs, int res) {
    procSpin_->blockSignals(true); resSpin_->blockSignals(true);
    procSpin_->setValue(procs); resSpin_->setValue(res);
    procSpin_->blockSignals(false); resSpin_->blockSignals(false);
    onSizeChanged();
}

// Shared process list: rows are labelled with the Scheduling tab's pids.
void DeadlockTab::useProcessPids(const std::vector<PCB>& procs) {
    if (procs.empty()) return;
    int n = std::min<int>((int)procs.size(), 10);
    setSize(n, resSpin_->value());
    for (int i = 0; i < n; ++i) rowPids_[i] = procs[i].pid;
    onSizeChanged();
    emit logMessage(QString("Deadlock tab rows now use the Scheduling tab pids: %1").arg(pidList([&] {
        std::vector<int> r; for (int i = 0; i < n; ++i) r.push_back(i); return r; }())));
}

void DeadlockTab::setCell(const QString& table, int row, int col, int value) {
    QTableWidget* t = table == "alloc" ? allocT_ : table == "max" ? maxT_ : table == "request" ? reqT_ : totalT_;
    if (t->item(row, col)) t->item(row, col)->setText(QString::number(value));
}

void DeadlockTab::writeMatrix(QTableWidget* t, const Matrix& m) {
    t->blockSignals(true);
    for (int i = 0; i < (int)m.size() && i < t->rowCount(); ++i)
        for (int j = 0; j < (int)m[i].size() && j < t->columnCount(); ++j)
            t->item(i, j)->setText(QString::number(m[i][j]));
    t->blockSignals(false);
}

void DeadlockTab::writeTotal(const Vec& v) { writeMatrix(totalT_, Matrix{v}); }

// Total = Available + sum of the Allocation column (used when a state gives Available).
static Vec totalFrom(const Matrix& alloc, const Vec& avail) {
    Vec total = avail;
    for (const Vec& r : alloc)
        for (size_t j = 0; j < total.size() && j < r.size(); ++j) total[j] += r[j];
    return total;
}

// Loads the integrated run's resource state at its first deadlock.
void DeadlockTab::loadState(const std::vector<int>& rowPids, const Matrix& alloc, const Matrix& max,
                            const Matrix& request, const Vec& avail, const QString& title) {
    int n = (int)alloc.size(), m = (int)avail.size();
    if (n < 1 || n > 10 || m < 1 || m > 6) return;          // tab limits
    setSize(n, m);
    rowPids_ = rowPids;
    onSizeChanged();                                        // relabel rows with the real pids
    writeMatrix(allocT_, alloc);
    writeMatrix(maxT_, max);
    writeMatrix(reqT_, request);
    writeTotal(totalFrom(alloc, avail));                   // Available is then recomputed from Total
    lastSafety_.reset();
    lastDetect_.reset();
    trace_->clear();
    history_.clear();
    updateNeed();
    updateAvail();
    status_->setText(title);
    status_->setStyleSheet("");
    totalsMsg_ = false;
    redraw();
    emit logMessage("Deadlock: " + title);
}

// Loads a matrix sample file (Banker's or detection).
bool DeadlockTab::loadFile(const QString& path) {
    MatrixData d;
    try {
        d = loadMatrixFile(path.toStdString());
        if (d.nProcs > 10 || d.nRes > 6) throw std::runtime_error("at most 10 processes and 6 resources");
    } catch (const std::exception& e) {
        ui::warn(this, QString("Could not load %1:\n%2").arg(path, e.what()));
        return false;
    }
    rowPids_.clear();                                   // sample files use P0..Pn-1
    setSize(d.nProcs, d.nRes);
    writeMatrix(allocT_, d.alloc);
    writeMatrix(maxT_, d.max);
    writeMatrix(reqT_, d.request.empty() ? Matrix(d.nProcs, Vec(d.nRes, 0)) : d.request);
    writeTotal(totalFrom(d.alloc, d.available));          // Total = sample Available + allocated
    lastSafety_.reset();
    lastDetect_.reset();
    trace_->clear();
    history_.clear();
    updateNeed();
    updateAvail();
    status_->setText("Loaded " + QFileInfo(path).fileName());
    status_->setStyleSheet("");
    totalsMsg_ = false;
    redraw();
    emit logMessage("Deadlock: loaded " + path);
    return true;
}

void DeadlockTab::clearAll() {
    for (QTableWidget* t : {allocT_, maxT_, reqT_, totalT_}) {
        int r = t->rowCount(), c = t->columnCount();
        writeMatrix(t, Matrix(r, Vec(c, 0)));
    }
    killed_.clear(); deadlocked_.clear();
    lastSafety_.reset(); lastDetect_.reset();
    trace_->clear();
    history_.clear();
    highlightRows({}, QColor());
    updateNeed();
    updateAvail();
    status_->setText("Cleared.");
    status_->setStyleSheet("");
    totalsMsg_ = false;
    redraw();
}

// Reads one table; returns false (with a warning) on a non-number or negative value.
// quiet = true: no dialog (used while redrawing during editing).
static bool readTable(QWidget* parent, QTableWidget* t, const QString& name, Matrix& out, bool quiet = false) {
    out.assign(t->rowCount(), Vec(t->columnCount(), 0));
    for (int i = 0; i < t->rowCount(); ++i)
        for (int j = 0; j < t->columnCount(); ++j) {
            bool ok = false;
            int v = t->item(i, j) ? t->item(i, j)->text().trimmed().toInt(&ok) : 0;
            if (!ok || v < 0) {
                if (!quiet) ui::warn(parent, QString("%1, row %2, column %3: enter a whole number >= 0.").arg(name).arg(i + 1).arg(j + 1));
                return false;
            }
            out[i][j] = v;
        }
    return true;
}

// Reads every input table and computes Available = Total - sum(Allocation).
// Refuses (status message + warning) when a resource has more allocated than exists.
bool DeadlockTab::readAll(Matrix& alloc, Matrix& max, Matrix& req, Vec& avail) {
    Matrix tot;
    if (!readTable(this, allocT_, "Allocation", alloc) || !readTable(this, maxT_, "Max", max) ||
        !readTable(this, reqT_, "Request", req) || !readTable(this, totalT_, "Total", tot))
        return false;
    QString error, warning;
    if (totalsProblem(error, warning)) {
        status_->setText(error);
        status_->setStyleSheet(QString("color:%1").arg(ui::faultColor().name()));
        totalsMsg_ = true;
        ui::warn(this, error);
        return false;
    }
    avail = tot[0];
    for (const Vec& r : alloc)
        for (size_t j = 0; j < avail.size(); ++j) avail[j] -= r[j];
    return true;
}

// Checks the totals: error = some resource has Allocation > Total (blocks the algorithms),
// warning = some Max value > Total (that process could never get its full claim).
// Returns true only for an error. Bad cells are skipped here (readTable reports them).
bool DeadlockTab::totalsProblem(QString& error, QString& warning) const {
    auto cell = [](QTableWidget* t, int i, int j) {
        return t->item(i, j) ? t->item(i, j)->text().trimmed().toInt() : 0;
    };
    QStringList over, maxOver;
    for (int j = 0; j < totalT_->columnCount(); ++j) {
        int total = cell(totalT_, 0, j), used = 0;
        QString res(QChar('A' + j));
        for (int i = 0; i < allocT_->rowCount(); ++i) used += cell(allocT_, i, j);
        if (used > total) over << QString("%1 (allocated %2 > total %3)").arg(res).arg(used).arg(total);
        for (int i = 0; i < maxT_->rowCount(); ++i)
            if (cell(maxT_, i, j) > total)
                maxOver << QString("%1 Max %2 = %3 > total %4").arg(pidName(i), res).arg(cell(maxT_, i, j)).arg(total);
    }
    error = over.isEmpty() ? QString()
                           : "Allocation exceeds Total for resource " + over.join(", ") +
                                 ". Increase Total or reduce Allocation before running an algorithm.";
    warning = maxOver.isEmpty() ? QString() : "Warning: " + maxOver.join(", ") + " (can never be satisfied).";
    return !error.isEmpty();
}

// Adds the "Max > Total" warning (if any) to the trace after an algorithm that uses Max.
void DeadlockTab::addMaxWarning() {
    QString error, warning;
    totalsProblem(error, warning);
    if (!warning.isEmpty()) trace_->addItem(warning);
}

// Available row = Total - sum(Allocation column), recomputed live; negative values in red.
// Also shows (or clears) the Total/Allocation validation message in the status label.
void DeadlockTab::updateAvail() {
    availT_->blockSignals(true);
    for (int j = 0; j < availT_->columnCount(); ++j) {
        int v = totalT_->item(0, j) ? totalT_->item(0, j)->text().trimmed().toInt() : 0;
        for (int i = 0; i < allocT_->rowCount(); ++i)
            v -= allocT_->item(i, j) ? allocT_->item(i, j)->text().trimmed().toInt() : 0;
        if (QTableWidgetItem* it = availT_->item(0, j)) {
            it->setText(QString::number(v));
            it->setForeground(v >= 0 ? palette().text().color() : ui::faultColor());
        }
    }
    availT_->blockSignals(false);

    QString error, warning;
    totalsProblem(error, warning);
    if (!error.isEmpty() || !warning.isEmpty()) {
        status_->setText(error.isEmpty() ? warning : error + (warning.isEmpty() ? "" : "\n" + warning));
        status_->setStyleSheet(QString("color:%1").arg(ui::faultColor().name()));
        totalsMsg_ = true;
    } else if (totalsMsg_) {                                // problem fixed: clear the old message
        status_->setText("Totals OK: Available = Total - Allocation.");
        status_->setStyleSheet("");
        totalsMsg_ = false;
    }
}

// Need is recomputed whenever Allocation or Max change ("!" marks Allocation > Max).
void DeadlockTab::updateNeed() {
    needT_->blockSignals(true);
    for (int i = 0; i < needT_->rowCount(); ++i)
        for (int j = 0; j < needT_->columnCount(); ++j) {
            int a = allocT_->item(i, j) ? allocT_->item(i, j)->text().toInt() : 0;
            int m = maxT_->item(i, j) ? maxT_->item(i, j)->text().toInt() : 0;
            QTableWidgetItem* it = needT_->item(i, j);
            it->setText(m - a >= 0 ? QString::number(m - a) : "!");
            it->setForeground(m - a >= 0 ? palette().text().color() : ui::faultColor());
        }
    needT_->blockSignals(false);
}

// Colours whole rows in every matrix (deadlocked = red, killed = grey).
void DeadlockTab::highlightRows(const std::vector<int>& rows, const QColor& c) {
    for (QTableWidget* t : {allocT_, maxT_, needT_, reqT_}) {
        t->blockSignals(true);
        for (int i = 0; i < t->rowCount(); ++i) {
            bool on = std::find(rows.begin(), rows.end(), i) != rows.end();
            bool gone = std::find(killed_.begin(), killed_.end(), i) != killed_.end();
            for (int j = 0; j < t->columnCount(); ++j)
                if (auto* it = t->item(i, j)) {
                    if (gone || on) { ui::shade(it, gone ? ui::idleColor() : c); continue; }
                    it->setBackground(QColor(Qt::transparent));
                    // back to the theme's text colour (keeps other colours, e.g. red negative Need)
                    if (it->foreground().color() == ui::darkInk()) it->setForeground(QBrush());
                }
        }
        t->blockSignals(false);
    }
}

// Rebuilds the resource-allocation graph from the current tables.
void DeadlockTab::redraw() {
    Matrix alloc, req, tot;
    if (!readTable(nullptr, allocT_, "", alloc, true) || !readTable(nullptr, reqT_, "", req, true) ||
        !readTable(nullptr, totalT_, "", tot, true))
        return;                                           // half-edited table: keep the old picture
    try {
        rag_->showGraph(buildRag(alloc, req), alloc, req, tot[0], deadlocked_, killed_, rowPids_);
    } catch (const std::exception&) {}
}

// Banker's safety algorithm on the current state.
bool DeadlockTab::checkSafety() {
    Matrix alloc, max, req; Vec avail;
    if (!readAll(alloc, max, req, avail)) return false;
    try {
        SafetyResult r = bankersSafety(alloc, max, avail);
        lastSafety_ = r;
        trace_->clear();
        trace_->addItem(QString("Work = Available = %1").arg(QString::fromStdString(vecToString(avail))));
        for (const SafetyStep& s : r.trace)
            trace_->addItem(QString("%1: Need %2 <= Work %3 -> runs, releases, Work = %4")
                                .arg(pidName(s.pid), QString::fromStdString(vecToString(r.need[s.pid])),
                                     QString::fromStdString(vecToString(s.workBefore)),
                                     QString::fromStdString(vecToString(s.workAfter))));
        QString text;
        if (r.safe) {
            text = QString("SAFE: <%1>").arg(pidList(r.sequence));
            status_->setStyleSheet(QString("color:%1").arg(ui::hitColor().darker(130).name()));
            highlightRows({}, QColor());
        } else {
            std::vector<int> stuck;
            for (int i = 0; i < (int)alloc.size(); ++i)
                if (std::find(r.sequence.begin(), r.sequence.end(), i) == r.sequence.end()) stuck.push_back(i);
            text = QString("UNSAFE: no safe sequence (%1 cannot finish)").arg(pidList(stuck));
            status_->setStyleSheet(QString("color:%1").arg(ui::faultColor().name()));
            trace_->addItem("No remaining process has Need <= Work -> state is UNSAFE");
        }
        status_->setText(text);
        totalsMsg_ = false;
        addMaxWarning();
        history_ << "Banker's safety: " + text;
        emit logMessage("Deadlock: " + text);
        return true;
    } catch (const std::exception& e) {
        ui::warn(this, QString("Cannot run the safety algorithm: %1").arg(e.what()));
        return false;
    }
}

// Banker's resource-request algorithm; a granted request is applied to the tables.
void DeadlockTab::prefillRequest(int row, const QString& text) {
    if (row >= 0 && row < reqPid_->count()) reqPid_->setCurrentIndex(row);
    reqEdit_->setText(text);
}

bool DeadlockTab::requestResources(int row, const QString& text) {
    Matrix alloc, max, req; Vec avail;
    if (!readAll(alloc, max, req, avail)) return false;
    QStringList parts = text.simplified().split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts);
    Vec request;
    for (const QString& p : parts) {
        bool ok = false;
        int v = p.toInt(&ok);
        if (!ok || v < 0) { ui::warn(this, "Request: enter whole numbers >= 0, e.g. 1 0 2"); return false; }
        request.push_back(v);
    }
    if ((int)request.size() != resSpin_->value()) {
        ui::warn(this, QString("Request needs exactly %1 numbers (one per resource type).").arg(resSpin_->value()));
        return false;
    }
    if (row < 0 || row >= (int)alloc.size()) { ui::warn(this, "Choose a process."); return false; }
    try {
        RequestResult r = bankersRequest(row, request, alloc, max, avail);
        QString msg = QString("Request %1 (%2): %3 - %4").arg(pidName(row), QString::fromStdString(vecToString(request)),
                                                              r.granted ? "GRANTED" : "DENIED",
                                                              QString::fromStdString(r.reason));
        if (r.granted) {
            for (size_t j = 0; j < request.size(); ++j) alloc[row][j] += request[j];
            writeMatrix(allocT_, alloc);
            updateNeed();
            updateAvail();                              // Total is unchanged, so Available drops
            redraw();
            msg += QString(" | new safe sequence <%1>").arg(pidList(r.safetyAfter.sequence));
        }
        status_->setText(msg);
        status_->setStyleSheet(QString("color:%1").arg(r.granted ? ui::hitColor().darker(130).name() : ui::faultColor().name()));
        totalsMsg_ = false;
        trace_->addItem(msg);
        addMaxWarning();
        history_ << msg;
        emit logMessage("Deadlock: " + msg);
        return r.granted;
    } catch (const std::exception& e) {
        ui::warn(this, QString("Cannot process the request: %1").arg(e.what()));
        return false;
    }
}

// Detection algorithm: deadlocked rows are painted red in every table and the RAG.
bool DeadlockTab::detect() {
    Matrix alloc, max, req; Vec avail;
    if (!readAll(alloc, max, req, avail)) return false;
    try {
        DetectionResult d = detectDeadlock(alloc, req, avail);
        lastDetect_ = d;
        deadlocked_ = d.deadlocked_pids;
        QString text;
        if (d.deadlocked) {
            text = QString("DEADLOCK: %1 are deadlocked").arg(pidList(d.deadlocked_pids));
            status_->setStyleSheet(QString("color:%1").arg(ui::faultColor().name()));
        } else {
            text = QString("No deadlock. Finish order: <%1>").arg(pidList(d.finishOrder));
            status_->setStyleSheet(QString("color:%1").arg(ui::hitColor().darker(130).name()));
        }
        status_->setText(text);
        totalsMsg_ = false;
        trace_->clear();
        trace_->addItem(text);
        RagGraph g = buildRag(alloc, req);
        if (!g.cycleNodes.empty()) {
            QStringList c;
            for (int v : g.cycleNodes) c << (v < g.nProcs ? pidName(v) : QString(QChar('A' + v - g.nProcs)));
            trace_->addItem("RAG cycle: " + c.join(" -> ") + " -> " + c.first() +
                            "  (with multi-instance resources a cycle is necessary, not sufficient)");
        }
        history_ << "Detection: " + text;
        highlightRows(deadlocked_, ui::lightRed());
        redraw();
        emit logMessage("Deadlock: " + text);
        return true;
    } catch (const std::exception& e) {
        ui::warn(this, QString("Cannot run detection: %1").arg(e.what()));
        return false;
    }
}

// Recovery by termination, one victim at a time (animated with a short delay).
bool DeadlockTab::recover(int stepDelayMs) {
    Matrix alloc, max, req; Vec avail;
    if (!readAll(alloc, max, req, avail) || recovering_) return false;
    try {
        recSteps_ = recoverByTermination(alloc, req, avail);
    } catch (const std::exception& e) {
        ui::warn(this, QString("Cannot recover: %1").arg(e.what()));
        return false;
    }
    if (recSteps_.empty()) {
        status_->setText("No deadlock: nothing to recover.");
        totalsMsg_ = false;
        status_->setStyleSheet(QString("color:%1").arg(ui::hitColor().darker(130).name()));
        emit recoveryFinished();
        return true;
    }
    recovering_ = true;
    for (QPushButton* b : actionBtns_) b->setEnabled(false);
    trace_->clear();
    applyRecoveryStep(0, stepDelayMs);
    return true;
}

// Applies one termination: zero the victim's rows, return its resources, redraw.
void DeadlockTab::applyRecoveryStep(size_t k, int delay) {
    const RecoveryStep& s = recSteps_[k];
    Matrix alloc, max, req; Vec avail;
    readAll(alloc, max, req, avail);
    for (size_t j = 0; j < avail.size(); ++j) {         // released instances return to Available
        alloc[s.killedPid][j] = 0;                       // (Available = Total - Allocation)
        req[s.killedPid][j] = 0;
    }
    writeMatrix(allocT_, alloc);
    writeMatrix(reqT_, req);
    killed_.push_back(s.killedPid);
    deadlocked_ = s.after.deadlocked_pids;
    lastDetect_ = s.after;
    QString msg = QString("Recovery: terminated %1, released %2 -> %3")
                      .arg(pidName(s.killedPid), QString::fromStdString(vecToString(s.released)),
                           s.after.deadlocked ? "still deadlocked: " + pidList(s.after.deadlocked_pids)
                                              : QString("deadlock cleared"));
    trace_->addItem(msg);
    history_ << msg;
    emit logMessage("Deadlock: " + msg);
    updateNeed();
    updateAvail();
    highlightRows(deadlocked_, ui::lightRed());
    redraw();
    status_->setText(msg);
    totalsMsg_ = false;
    status_->setStyleSheet(QString("color:%1").arg(s.after.deadlocked ? ui::faultColor().name()
                                                                       : ui::hitColor().darker(130).name()));
    if (k + 1 < recSteps_.size()) {
        if (delay > 0) QTimer::singleShot(delay, this, [this, k, delay] { applyRecoveryStep(k + 1, delay); });
        else applyRecoveryStep(k + 1, delay);
        return;
    }
    recovering_ = false;
    for (QPushButton* b : actionBtns_) b->setEnabled(true);
    emit recoveryFinished();
}

QString DeadlockTab::statusText() const { return status_->text(); }

// Plain-text block for the exported report.
QString DeadlockTab::reportText() const {
    auto dump = [&](const QString& name, QTableWidget* t) {
        QString s = name + ":\n";
        for (int i = 0; i < t->rowCount(); ++i) {
            s += "  " + (t == availT_ || t == totalT_ ? QString("    ") : pidName(i).leftJustified(4));
            for (int j = 0; j < t->columnCount(); ++j) s += " " + (t->item(i, j) ? t->item(i, j)->text() : "0").rightJustified(3);
            s += "\n";
        }
        return s;
    };
    QString out = dump("Allocation", allocT_) + dump("Max", maxT_) + dump("Need", needT_) +
                  dump("Request", reqT_) + dump("Total", totalT_) + dump("Available", availT_);
    out += "Last result: " + status_->text() + "\n";
    for (const QString& h : history_) out += "  " + h + "\n";
    return out;
}

// CSV block: one line per process with its Allocation / Max / Need / Request.
QString DeadlockTab::csvSection() const {
    QString out = "## Deadlock state\nprocess";
    int m = availT_->columnCount();
    for (const char* name : {"alloc", "max", "need", "request"})
        for (int j = 0; j < m; ++j) out += QString(",%1_%2").arg(name).arg(QChar('A' + j));
    out += "\n";
    for (int i = 0; i < allocT_->rowCount(); ++i) {
        out += pidName(i);
        for (QTableWidget* t : {allocT_, maxT_, needT_, reqT_})
            for (int j = 0; j < m; ++j) out += "," + (t->item(i, j) ? t->item(i, j)->text() : "0");
        out += "\n";
    }
    out += "total";
    for (int j = 0; j < m; ++j) out += "," + (totalT_->item(0, j) ? totalT_->item(0, j)->text() : "0");
    out += "\navailable";
    for (int j = 0; j < m; ++j) out += "," + (availT_->item(0, j) ? availT_->item(0, j)->text() : "0");
    out += "\n\n## Deadlock result\nresult\n\"" + status_->text() + "\"\n";
    for (const QString& h : history_) out += "\"" + QString(h).replace('"', '\'') + "\"\n";
    return out;
}
