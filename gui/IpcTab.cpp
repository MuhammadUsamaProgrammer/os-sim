// IpcTab.cpp - Inter-Process Communication module (M3).
#include "IpcTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <cmath>
#include "ModuleKit.h"
#include "NativeRunner.h"
#include "Style.h"

static QColor ink(const QWidget* w) { return w->palette().text().color(); }
static QColor faint(const QWidget* w, int a) { QColor c = ink(w); c.setAlpha(a); return c; }

// ================================================================ IpcView

IpcView::IpcView(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    anim_.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t_ = v.toDouble(); update(); });
    setAccessibleName("IPC view: processes and the communication object");
}

void IpcView::setSnapshot(const IpcConfig* cfg, const IpcSnapshot* s, int animMs) {
    anim_.stop();
    cfg_ = cfg;
    s_ = s;
    t_ = 1;
    if (animMs > 0 && s && (s->transfer.from >= 0 || s->transfer.to >= 0)) { t_ = 0; anim_.setDuration(animMs); anim_.start(); }
    update();
}

QRectF IpcView::procRect(int i, int n) const {
    double W = width(), H = height(), w = std::min(200.0, W * 0.27), h = n > 2 ? H * 0.36 : H * 0.5;
    if (i == 0) return QRectF(8, 10, w, h);
    if (i == 1) return QRectF(W - w - 8, 10, w, h);
    if (i == 2) return QRectF(8, H - h - 10, w, h);
    return QRectF(W - w - 8, H - h - 10, w, h);
}

QRectF IpcView::objectRect() const {
    double W = width(), H = height();
    return QRectF(W * 0.31, H * 0.12, W * 0.38, H * 0.62);
}

void IpcView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    if (!s_ || !cfg_) { p.setPen(ink(this)); p.drawText(rect(), Qt::AlignCenter, "Choose a preset or a valid configuration"); return; }
    const IpcSnapshot& s = *s_;
    QFont small = font(), bold = font(), mono = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    bold.setBold(true);
    mono.setFamily("monospace");
    mono.setPointSizeF(small.pointSizeF());
    QRectF obj = objectRect();
    int n = (int)s.procs.size();
    // links from processes to the object
    for (int i = 0; i < n; ++i) {
        QRectF r = procRect(i, n);
        bool linked = cfg_->mode != IpcMode::SharedMemory || s.procs[i].attached;
        p.setPen(QPen(linked ? faint(this, 140) : faint(this, 40), linked ? 2 : 1, linked ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(r.center(), obj.center());
    }
    // object
    QColor oc = cfg_->mode == IpcMode::SharedMemory ? ui::semDone() : cfg_->mode == IpcMode::MessagePassing ? ui::semReady() : ui::semWaiting();
    QColor of = oc;
    of.setAlpha(30);
    bool gone = cfg_->mode == IpcMode::SharedMemory && !s.shmExists;
    p.setPen(Qt::NoPen);
    p.setBrush(palette().base());                       // opaque, so the link lines stay behind it
    p.drawRoundedRect(obj, 10, 10);
    p.setPen(QPen(oc, 2, gone || s.shmRemoved ? Qt::DashLine : Qt::SolidLine));
    p.setBrush(of);
    p.drawRoundedRect(obj, 10, 10);
    p.setFont(bold);
    p.setPen(ink(this));
    QString title;
    if (cfg_->mode == IpcMode::SharedMemory)
        title = gone ? QString("no segment (id %1)").arg(cfg_->shmId)
                     : QString("shared segment id %1%2").arg(cfg_->shmId).arg(s.shmRemoved ? "  [marked for removal]" : "");
    else if (cfg_->mode == IpcMode::MessagePassing)
        title = cfg_->direct ? QString("direct links (capacity %1 each)").arg(cfg_->queueCapacity)
                             : QString("mailbox %1 (capacity %2)").arg(cfg_->mailboxId).arg(cfg_->queueCapacity);
    else
        title = QString("%1 (modelled capacity %2 bytes)").arg(cfg_->namedPipe ? "named pipe (FIFO)" : "anonymous pipe").arg(cfg_->pipeCapacity);
    p.drawText(obj.adjusted(8, 4, -6, 0), Qt::AlignLeft | Qt::AlignTop, title);
    p.setFont(small);
    QRectF inner = obj.adjusted(10, 26, -10, -26);
    if (cfg_->mode == IpcMode::SharedMemory && !gone) {
        int cap = (int)s.shmBytes.size(), cols = std::min(16, std::max(1, cap)), rows = (cap + cols - 1) / cols;
        double cw = inner.width() / cols, ch = std::min(22.0, inner.height() / std::max(1, rows));
        p.setFont(mono);
        for (int i = 0; i < cap; ++i) {
            QRectF c(inner.left() + (i % cols) * cw, inner.top() + (i / cols) * ch, cw - 1, ch - 1);
            char b = s.shmBytes[i];
            p.setPen(QPen(faint(this, 110), 1));
            p.setBrush(b ? ui::lightGreen() : palette().base().color());
            p.drawRect(c);
            p.setPen(b ? ui::darkInk() : faint(this, 120));
            p.drawText(c, Qt::AlignCenter, b ? QString(QChar(b)) : QString::number(i));
        }
        p.setFont(small);
        p.setPen(ink(this));
        p.drawText(QRectF(obj.left() + 8, obj.bottom() - 22, obj.width() - 16, 18), Qt::AlignLeft,
                   QString("attachments: %1   permissions 0600 (illustrative)").arg(s.shmAttach));
    } else if (cfg_->mode == IpcMode::MessagePassing) {
        int cap = cfg_->direct ? std::max<int>(cfg_->queueCapacity, (int)s.queue.size()) : cfg_->queueCapacity;
        double sh = std::min(40.0, inner.height() / std::max(1, cap));
        for (int i = 0; i < cap; ++i) {
            QRectF c(inner.left(), inner.top() + i * sh, inner.width(), sh - 4);
            p.setPen(QPen(faint(this, 110), 1));
            p.setBrush(i < (int)s.queue.size() ? ui::semReady().lighter(160) : palette().base().color());
            p.drawRoundedRect(c, 5, 5);
            if (i < (int)s.queue.size()) {
                const IpcMessage& m = s.queue[i];
                p.setPen(ui::darkInk());
                p.drawText(c.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           QString("%1 \"%2\"  from %3%4").arg(i == 0 ? "head:" : "     ").arg(QString::fromStdString(m.text))
                               .arg(QString::fromStdString(s.procs[m.from].name)).arg(m.to >= 0 ? " to " + QString::fromStdString(s.procs[m.to].name) : QString()));
            } else {
                p.setPen(faint(this, 120));
                p.drawText(c, Qt::AlignCenter, "empty slot");
            }
        }
    } else if (cfg_->mode == IpcMode::Pipe) {
        int cap = cfg_->pipeCapacity, cols = std::min(cap, 16), rows = (cap + cols - 1) / cols;
        double cw = inner.width() / cols, ch = std::min(26.0, (inner.height() - 30) / std::max(1, rows));
        p.setFont(mono);
        for (int i = 0; i < cap; ++i) {
            QRectF c(inner.left() + (i % cols) * cw, inner.top() + 20 + (i / cols) * ch, cw - 1, ch - 1);
            bool has = i < (int)s.pipeBuf.size();
            p.setPen(QPen(faint(this, 110), 1));
            p.setBrush(has ? ui::semWaiting().lighter(150) : palette().base().color());
            p.drawRect(c);
            if (has) { p.setPen(ui::darkInk()); p.drawText(c, Qt::AlignCenter, QString(QChar(s.pipeBuf[i]))); }
        }
        p.setFont(small);
        p.setPen(ink(this));
        p.drawText(QRectF(inner.left(), inner.top(), inner.width(), 16), Qt::AlignLeft, "read side (oldest byte)  <-  bytes  <-  write side");
        p.drawText(QRectF(obj.left() + 8, obj.bottom() - 22, obj.width() - 16, 18), Qt::AlignLeft,
                   QString("%1 of %2 bytes buffered (a byte stream: no message boundaries)").arg(s.pipeBuf.size()).arg(cap));
    }
    // process cards
    for (int i = 0; i < n; ++i) {
        const IpcProcState& pr = s.procs[i];
        QRectF r = procRect(i, n);
        QColor c = pr.blocked ? ui::semWaiting() : ui::pidColor(i);
        QColor f = c;
        f.setAlpha(45);
        p.setPen(Qt::NoPen);
        p.setBrush(palette().base());
        p.drawRoundedRect(r, 8, 8);
        p.setPen(QPen(c, pr.blocked ? 3 : 1.6));
        p.setBrush(f);
        p.drawRoundedRect(r, 8, 8);
        p.setFont(bold);
        p.setPen(ink(this));
        p.drawText(r.adjusted(8, 4, -6, 0), Qt::AlignLeft | Qt::AlignTop, QString::fromStdString(pr.name) + (pr.blocked ? "  [BLOCKED]" : ""));
        p.setFont(small);
        QStringList lines;
        if (pr.blocked) lines << QString::fromStdString(pr.blockedOn);
        if (cfg_->mode == IpcMode::SharedMemory) {
            char buf[32];
            snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)pr.attachAddr);
            lines << (pr.attached ? QString("attached %1 at %2").arg(pr.attachReadOnly ? "read-only" : "read-write").arg(buf) : QString("not attached"));
        } else if (cfg_->mode == IpcMode::MessagePassing) {
            lines << QString("sent %1, received %2").arg(pr.sent).arg(pr.received);
        } else {
            lines << QString("read end %1, write end %2").arg(pr.readOpen ? "open" : "CLOSED").arg(pr.writeOpen ? "open" : "CLOSED");
            lines << QString("bytes written %1, read %2").arg(pr.bytesOut).arg(pr.bytesIn);
        }
        if (!pr.lastData.empty()) lines << QString("last data: \"%1\"").arg(QString::fromStdString(pr.lastData));
        p.drawText(r.adjusted(8, 24, -6, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, lines.join("\n"));
    }
    // moving data token
    const IpcTransfer& tr = s.transfer;
    if ((tr.from >= 0 || tr.to >= 0) && !tr.data.empty()) {
        QPointF a = tr.from >= 0 ? procRect(tr.from, n).center() : obj.center();
        QPointF b = tr.to >= 0 ? procRect(tr.to, n).center() : obj.center();
        QPointF at = a + (b - a) * t_;
        QString txt = QString::fromStdString(tr.data);
        double w = std::max(50.0, p.fontMetrics().horizontalAdvance(txt) + 16.0);
        QRectF tok(at.x() - w / 2, at.y() - 12, w, 24);
        p.setPen(QPen(ui::darkInk(), 1));
        p.setBrush(ui::semRunning());
        p.drawRoundedRect(tok, 10, 10);
        p.setPen(ui::darkInk());
        p.drawText(tok, Qt::AlignCenter, txt);
    }
    if (s.failed) {
        p.setPen(ui::semError());
        p.setFont(bold);
        p.drawText(QRectF(obj.left(), obj.bottom() + 4, obj.width(), 20), Qt::AlignCenter, "operation FAILED (see the event)");
    }
}

// ================================================================ IpcTab

static const char* kDemos[][2] = {
    {"shm", "System V shared memory: shmget (private key), server shmat + write \"HELLO\", client shmat (read-only) + read, shmdt, shmctl(IPC_RMID)."},
    {"msg", "System V message queue: non-blocking msgrcv on the empty queue fails (ENOMSG), the receiver then blocks in msgrcv until the sender's msgsnd; msgctl(IPC_RMID)."},
    {"pipe", "Anonymous pipe: pipe(), fork(); the child writes, the parent reads in 8-byte pieces until read() returns 0 (EOF)."},
    {"fifo", "Named pipe: mkfifo in a private temporary directory; a separately exec'ed writer program; the FIFO and directory are removed."},
};

IpcTab::IpcTab(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Inter-Process Communication",
        "How independent processes exchange data: a shared memory segment (no copying through the kernel), message passing "
        "(the kernel copies each message, FIFO, bounded) and pipes (a kernel byte stream).");
    native_ = new NativeRunner(this);
    mode_ = new QComboBox;
    for (IpcMode m : {IpcMode::SharedMemory, IpcMode::MessagePassing, IpcMode::Pipe}) mode_->addItem(ipcModeName(m));
    preset_ = new QComboBox;
    preset_->addItem("(custom)");
    for (const IpcPreset& p : ipcPresets()) preset_->addItem(QString::fromStdString(p.name));
    shmSize_ = new QSpinBox; shmSize_->setRange(1, 256); shmSize_->setValue(32);
    qCap_ = new QSpinBox; qCap_->setRange(1, 16); qCap_->setValue(2);
    pipeCap_ = new QSpinBox; pipeCap_->setRange(1, 256); pipeCap_->setValue(16);
    direct_ = new QCheckBox("direct (name the peer)");
    named_ = new QCheckBox("named pipe (FIFO)");
    opProc_ = new QComboBox;
    opKind_ = new QComboBox;
    opPeer_ = new QComboBox;
    opData_ = new QLineEdit("HELLO");
    opOffset_ = new QSpinBox; opOffset_->setRange(-5, 300);
    opLen_ = new QSpinBox; opLen_->setRange(0, 300); opLen_->setValue(5);
    opNb_ = new QCheckBox("non-blocking");
    opRo_ = new QCheckBox("attach read-only");
    opWriteEnd_ = new QCheckBox("close the WRITE end");
    auto* applyBtn = new QPushButton("Apply operation");
    opsList_ = new QListWidget;
    opsList_->setMaximumHeight(140);
    auto* cfgW = new QWidget;
    auto* f = new QFormLayout(cfgW);
    f->addRow("Preset:", preset_);
    f->addRow("Mechanism:", mode_);
    f->addRow("Segment size:", shmSize_);
    f->addRow("Queue capacity:", qCap_);
    f->addRow(direct_);
    f->addRow("Pipe capacity (model):", pipeCap_);
    f->addRow(named_);
    f->addRow(new QLabel("<b>Next operation</b>"));
    f->addRow("Process:", opProc_);
    f->addRow("Operation:", opKind_);
    f->addRow("Peer (direct):", opPeer_);
    f->addRow("Data:", opData_);
    f->addRow("Offset:", opOffset_);
    f->addRow("Length:", opLen_);
    f->addRow(opNb_);
    f->addRow(opRo_);
    f->addRow(opWriteEnd_);
    f->addRow(applyBtn);
    f->addRow(new QLabel("Operations issued (a new one after Prev starts a new branch):"));
    f->addRow(opsList_);

    view_ = new IpcView;
    state_ = new QTableWidget(0, 2);
    state_->setHorizontalHeaderLabels({"State", "Value"});
    state_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    state_->verticalHeader()->setVisible(false);
    state_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    state_->setWordWrap(true);

    bar_ = new PlaybackBar;
    bottom_ = new QTabWidget;
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    bottom_->addTab(log_, "Event log");
    auto* cmp = new QTextBrowser;
    cmp->setHtml(
        "<table border=1 cellpadding=4 cellspacing=0>"
        "<tr><th></th><th>Shared memory</th><th>Message passing</th><th>Pipe</th></tr>"
        "<tr><td>Data path</td><td>both processes map the same physical pages; reads/writes are plain memory accesses</td>"
        "<td>the kernel copies every message into a queue and out again</td><td>the kernel copies bytes into a buffer and out again</td></tr>"
        "<tr><td>Boundaries</td><td>none (raw bytes at offsets)</td><td>kept: one receive = one message</td><td>none: a byte stream</td></tr>"
        "<tr><td>Coordination</td><td>NOT provided: needs semaphores / mutexes (Synchronization tab)</td><td>built in: receive blocks on empty, send on full</td>"
        "<td>built in: read blocks on empty, write on full, EOF when all writers close</td></tr>"
        "<tr><td>Typical use</td><td>large data, frequent updates</td><td>small structured messages, many senders</td><td>one-way streams between related processes (named pipes: unrelated)</td></tr>"
        "</table><p>No absolute speed numbers are claimed: costs depend on message size, copying and the OS.</p>");
    bottom_->addTab(cmp, "Compare the mechanisms");
    demo_ = new QComboBox;
    for (auto& d : kDemos) demo_->addItem(d[0]);
    demoStart_ = new QPushButton("Start live demo");
    demoStop_ = new QPushButton("Stop");
    demoStatus_ = new QLabel;
    demoStatus_->setWordWrap(true);
    demoTable_ = new QTableWidget;
    NativeRunner::showEvents(demoTable_, {});
    auto* dW = new QWidget;
    auto* dl = new QVBoxLayout(dW);
    auto* dt = new QHBoxLayout;
    dt->addWidget(demo_);
    dt->addWidget(demoStart_);
    dt->addWidget(demoStop_);
    auto* lab = new QLabel("<b>Live Linux Demo</b>: real system calls on private, owned IPC objects that are removed afterwards; results are captured, never invented.");
    lab->setWordWrap(true);
    dt->addWidget(lab, 1);
    dl->addLayout(dt);
    dl->addWidget(demoStatus_);
    dl->addWidget(demoTable_, 1);
    bottom_->addTab(dW, "Live Linux demo");

    assembleModule(this, header_, cfgW, titledBox("Processes and the communication object", view_), titledBox("Inspector", state_), bar_, bottom_);

    auto cfgChanged = [this] {
        if (loading_) return;
        loading_ = true;
        preset_->setCurrentIndex(0);
        loading_ = false;
        rebuild();
    };
    connect(mode_, &QComboBox::currentIndexChanged, this, cfgChanged);
    for (QSpinBox* s : {shmSize_, qCap_, pipeCap_}) connect(s, &QSpinBox::valueChanged, this, cfgChanged);
    connect(direct_, &QCheckBox::toggled, this, cfgChanged);
    connect(named_, &QCheckBox::toggled, this, cfgChanged);
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_ && i > 0) loadPreset(i - 1); });
    connect(applyBtn, &QPushButton::clicked, this, [this] {
        IpcOp op;
        op.kind = (IpcOpKind)opKind_->currentData().toInt();
        op.proc = opProc_->currentIndex();
        op.peer = opPeer_->currentIndex();
        op.data = opData_->text().toStdString();
        op.offset = opOffset_->value();
        op.length = opLen_->value();
        op.nonBlocking = opNb_->isChecked();
        op.readOnly = opRo_->isChecked();
        op.writeEnd = opWriteEnd_->isChecked();
        QString e = applyOp(op);
        if (!e.isEmpty()) header_->setStatus("Refused / failed: " + e, ui::semError());
    });
    connect(bar_, &PlaybackBar::positionChanged, this, &IpcTab::showStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { int i = preset_->currentIndex(); if (i > 0) loadPreset(i - 1); else rebuild(); });
    connect(bottom_, &QTabWidget::currentChanged, this, [this](int i) { header_->setMode(i == 2); });
    connect(demo_, &QComboBox::currentIndexChanged, this, [this](int i) { demoStatus_->setText(kDemos[i][1]); });
    connect(demoStart_, &QPushButton::clicked, this, [this] { startDemo(demo_->currentText()); });
    connect(demoStop_, &QPushButton::clicked, this, &IpcTab::stopDemo);
    connect(native_, &NativeRunner::event, this, [this](const NativeEvent&) { NativeRunner::showEvents(demoTable_, native_->events()); });
    connect(native_, &NativeRunner::finished, this, [this](int code, bool crashed) {
        demoStart_->setEnabled(true);
        demoStop_->setEnabled(false);
        QString t = crashed ? "stopped" : QString("helper exited with code %1").arg(code);
        demoStatus_->setText(demoStatus_->text() + "\nFinished: " + t + ".");
        emit logMessage("IPC live demo finished: " + t);
    });
    demoStop_->setEnabled(false);
    if (!NativeRunner::available()) {
        demoStart_->setEnabled(false);
        demoStatus_->setText("Live Linux demos are not available here (needs Linux and ossim_helper). The simulation is unaffected.");
    } else demoStatus_->setText(kDemos[0][1]);
    loadPreset(0);
}

IpcConfig IpcTab::readConfig() const {
    IpcConfig c;
    c.mode = (IpcMode)mode_->currentIndex();
    c.procs = c.mode == IpcMode::SharedMemory ? std::vector<std::string>{"server", "client"}
            : c.mode == IpcMode::Pipe ? std::vector<std::string>{"parent", "child"}
            : (direct_->isChecked() ? std::vector<std::string>{"P1", "P2", "P3"} : std::vector<std::string>{"P1", "P2"});
    c.shmSize = shmSize_->value();
    c.queueCapacity = qCap_->value();
    c.direct = direct_->isChecked();
    c.pipeCapacity = pipeCap_->value();
    c.namedPipe = named_->isChecked();
    return c;
}

void IpcTab::setConfig(const IpcConfig& c) {
    loading_ = true;
    mode_->setCurrentIndex((int)c.mode);
    shmSize_->setValue(c.shmSize);
    qCap_->setValue(c.queueCapacity);
    direct_->setChecked(c.direct);
    pipeCap_->setValue(c.pipeCapacity);
    named_->setChecked(c.namedPipe);
    loading_ = false;
}

bool IpcTab::rebuild() {
    view_->setSnapshot(nullptr, nullptr, 0);
    IpcConfig c = readConfig();
    try {
        sim_ = std::make_unique<IpcSim>(c);
    } catch (const std::exception& e) {
        sim_.reset();
        bar_->setLength(0);
        header_->setStatus(QString("Invalid configuration: %1").arg(e.what()), ui::semError());
        return false;
    }
    // operation choices for this mechanism
    loading_ = true;
    opKind_->clear();
    std::vector<IpcOpKind> kinds = c.mode == IpcMode::SharedMemory
        ? std::vector<IpcOpKind>{IpcOpKind::ShmCreate, IpcOpKind::ShmAttach, IpcOpKind::ShmWrite, IpcOpKind::ShmRead, IpcOpKind::ShmDetach, IpcOpKind::ShmRemove}
        : c.mode == IpcMode::MessagePassing ? std::vector<IpcOpKind>{IpcOpKind::Send, IpcOpKind::Receive}
                                            : std::vector<IpcOpKind>{IpcOpKind::PipeWrite, IpcOpKind::PipeRead, IpcOpKind::PipeClose};
    for (IpcOpKind k : kinds) opKind_->addItem(ipcOpName(k), (int)k);
    opProc_->clear();
    opPeer_->clear();
    for (const std::string& n : c.procs) { opProc_->addItem(QString::fromStdString(n)); opPeer_->addItem(QString::fromStdString(n)); }
    opPeer_->setCurrentIndex(1);
    loading_ = false;
    afterChange();
    bar_->setPosition(0);
    return true;
}

bool IpcTab::loadPreset(int index) {
    std::vector<IpcPreset> v = ipcPresets();
    if (index < 0 || index >= (int)v.size()) return false;
    setConfig(v[index].cfg);
    loading_ = true;
    preset_->setCurrentIndex(index + 1);
    loading_ = false;
    if (!rebuild()) return false;
    for (const IpcOp& op : v[index].ops) sim_->apply(op);
    afterChange();
    bar_->setPosition(0);
    header_->setStatus(QString::fromStdString(v[index].description));
    emit logMessage(QString("IPC: %1 (%2 events)").arg(QString::fromStdString(v[index].name)).arg(sim_->trace().size() - 1));
    return true;
}

QString IpcTab::applyOp(const IpcOp& op) {
    if (!sim_) return "no valid configuration";
    if (bar_->position() < bar_->length()) sim_->truncate(bar_->position());   // new branch
    std::string e = sim_->apply(op);
    afterChange();
    bar_->setPosition(bar_->length());
    if (e.empty()) emit logMessage("IPC: " + QString::fromStdString(sim_->trace().back().event));
    return QString::fromStdString(e);
}

void IpcTab::afterChange() {
    log_->clear();
    for (const IpcSnapshot& s : sim_->trace())
        log_->appendPlainText(QString("[%1]%2 %3").arg(s.step).arg(s.failed ? " FAILED" : s.wake ? " WAKE" : "").arg(QString::fromStdString(s.event)));
    refreshOps();
    bar_->setLength((int)sim_->trace().size() - 1);
}

void IpcTab::refreshOps() {
    opsList_->clear();
    for (const IpcOp& op : sim_->applied()) {
        QString t = QString("%1: %2").arg(QString::fromStdString(sim_->config().procs[op.proc])).arg(ipcOpName(op.kind));
        if (!op.data.empty()) t += QString(" \"%1\"").arg(QString::fromStdString(op.data));
        if (op.kind == IpcOpKind::ShmRead || op.kind == IpcOpKind::PipeRead) t += QString(" len %1").arg(op.length);
        if (op.nonBlocking) t += " (non-blocking)";
        opsList_->addItem(t);
    }
}

void IpcTab::showStep(int pos, bool forward) {
    if (!sim_) return;
    pos = std::clamp(pos, 0, (int)sim_->trace().size() - 1);
    const IpcSnapshot& s = sim_->trace()[pos];
    view_->setSnapshot(&sim_->config(), &s, forward ? bar_->animMs() : 0);
    std::vector<std::pair<QString, QString>> rows;
    const IpcConfig& c = sim_->config();
    if (c.mode == IpcMode::SharedMemory) {
        QString bytes;
        for (char ch : s.shmBytes) bytes += ch ? QChar(ch) : QChar('.');
        rows = {{"Segment", s.shmExists ? QString("id %1, %2 bytes%3").arg(c.shmId).arg(c.shmSize).arg(s.shmRemoved ? ", marked for removal" : "") : "none"},
                {"Attachments", QString::number(s.shmAttach)}, {"Contents", bytes}};
    } else if (c.mode == IpcMode::MessagePassing) {
        QStringList q;
        for (const IpcMessage& m : s.queue) q << QString::fromStdString(m.text);
        rows = {{"Model", c.direct ? "direct (sender and receiver named)" : QString("indirect, mailbox %1").arg(c.mailboxId)},
                {"Occupancy", QString("%1 / %2").arg(s.queue.size()).arg(c.queueCapacity)}, {"Queue (head first)", q.join(", ")}};
    } else {
        rows = {{"Pipe", c.namedPipe ? "named (FIFO)" : "anonymous"},
                {"Buffered", QString("%1 / %2 bytes (model)").arg(s.pipeBuf.size()).arg(c.pipeCapacity)},
                {"Bytes", QString::fromStdString(s.pipeBuf)}};
    }
    for (const IpcProcState& p : s.procs)
        rows.push_back({QString::fromStdString(p.name), (p.blocked ? "BLOCKED: " + QString::fromStdString(p.blockedOn) : QString("ready")) +
                                                           (p.lastData.empty() ? QString() : QString(", last \"%1\"").arg(QString::fromStdString(p.lastData)))});
    state_->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); ++r) {
        auto* a = ui::roItem(rows[r].first);
        auto* b = ui::roItem(rows[r].second);
        a->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        b->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        state_->setItem(r, 0, a);
        state_->setItem(r, 1, b);
    }
    state_->resizeRowsToContents();
    header_->setStatus(QString("Event %1 of %2: %3").arg(pos).arg(sim_->trace().size() - 1).arg(QString::fromStdString(s.event)),
                       s.failed ? ui::semError() : s.wake ? ui::hitColor().darker(130) : QColor());
}

bool IpcTab::startDemo(const QString& demo) {
    int i = demo_->findText(demo);
    if (i < 0) return false;
    demo_->setCurrentIndex(i);
    if (!native_->start({demo})) return false;
    NativeRunner::showEvents(demoTable_, {});
    demoStart_->setEnabled(false);
    demoStop_->setEnabled(true);
    bottom_->setCurrentIndex(2);
    emit logMessage("IPC live demo started: " + demo);
    return true;
}

void IpcTab::stopDemo() { native_->stop(); }
bool IpcTab::demoRunning() const { return native_->running(); }
int IpcTab::demoExitCode() const { return native_->lastExitCode(); }
QString IpcTab::statusText() const { return header_->status(); }

QString IpcTab::reportText() const {
    if (!sim_) return "(invalid configuration)\n";
    QString out = QString("%1, %2 events:\n").arg(ipcModeName(sim_->config().mode)).arg(sim_->trace().size() - 1);
    for (const IpcSnapshot& s : sim_->trace())
        out += QString("  [%1]%2 %3\n").arg(s.step).arg(s.failed ? " FAILED" : s.wake ? " WAKE" : "").arg(QString::fromStdString(s.event));
    if (!native_->events().isEmpty()) {
        out += "Last Live Linux demo:\n";
        for (const NativeEvent& e : native_->events()) out += QString("  %1 ms %2 pid %3: %4: %5\n").arg(e.ms).arg(e.role).arg(e.pid).arg(e.event, e.detail);
    }
    return out;
}

QString IpcTab::csvSection() const {
    QString out = "## IPC\nstep,status,event\n";
    if (sim_)
        for (const IpcSnapshot& s : sim_->trace()) {
            QString e = QString::fromStdString(s.event);
            e.replace('"', '\'');
            out += QString("%1,%2,\"%3\"\n").arg(s.step).arg(s.failed ? "failed" : s.wake ? "wake" : "ok").arg(e);
        }
    return out;
}
