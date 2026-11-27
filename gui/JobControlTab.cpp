// JobControlTab.cpp - Processes > Job Control (see JobControlTab.h).
#include "JobControlTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "ModuleKit.h"
#include "NativeRunner.h"
#include "Style.h"

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QColor stateColor(JcState s) {
    switch (s) {
        case JcState::Running: return ui::semRunning();
        case JcState::Sleeping: return ui::semReady();
        case JcState::Stopped: return ui::semWaiting();
        case JcState::Exited: return ui::semDone();
        case JcState::Killed: return ui::semError();
    }
    return ui::semIdle();
}
}

// ======================================================================= JobView

JobView::JobView(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { t_ = v.toDouble(); update(); });
}

void JobView::setSnapshot(const JcSnapshot* cur, const JcSnapshot* prev, int animMs) {
    cur_ = cur;
    prev_ = prev;
    anim_.stop();
    if (animMs > 0 && prev) { t_ = 0; anim_.setDuration(animMs); anim_.start(); }
    else t_ = 1;
    update();
}

// Live jobs plus the most recent finished ones (at most 6 boxes).
std::vector<const JcJob*> JobView::shownJobs(const JcSnapshot& s) const {
    std::vector<const JcJob*> v;
    for (auto it = s.jobs.rbegin(); it != s.jobs.rend() && v.size() < 6; ++it) v.insert(v.begin(), &*it);
    return v;
}

QRectF JobView::boxOf(int pgid, const JcSnapshot& s) const {
    double top = 96, left = 8, w = width() - 16;
    if (pgid == JobShell::kShellPid) return QRectF(left, top, std::min(150.0, w * 0.22), 76);
    std::vector<const JcJob*> jobs = shownJobs(s);
    double x0 = left + std::min(150.0, w * 0.22) + 12;
    int cols = std::max(1, std::min(3, (int)((w - x0 + left) / 170)));
    double bw = (w - (x0 - left) - 8 * (cols - 1)) / cols;
    double bh = std::max(70.0, std::min(120.0, (height() - top - 30) / 2));
    for (size_t i = 0; i < jobs.size(); ++i)
        if (jobs[i]->pgid == pgid) return QRectF(x0 + (i % cols) * (bw + 8), top + (i / cols) * (bh + 10), bw, bh);
    return QRectF();
}

void JobView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::WindowText);
    if (!cur_) return;
    const JcSnapshot& s = *cur_;
    // terminal
    QRectF term(8, 6, width() - 16, 40);
    p.setPen(QPen(ink, 1.2));
    p.setBrush(QColor("#263238"));
    p.drawRoundedRect(term, 6, 6);
    p.setPen(QColor("#e0f2f1"));
    QString fgName = s.fgPgid == JobShell::kShellPid ? "the shell (PID 100)" : QString("process group %1").arg(s.fgPgid);
    for (const JcJob& j : s.jobs) if (j.pgid == s.fgPgid) fgName += QString(" = job [%1]").arg(j.id);
    p.drawText(term.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
               QString("Terminal (session %1)   t = %2   foreground process group: %3%4").arg(JobShell::kShellPid).arg(s.tick).arg(fgName)
                   .arg(s.pendingInput.empty() ? QString() : QString("   input: \"%1\"").arg(q(s.pendingInput))));
    // foreground arrow (moves from the old foreground group to the new one)
    QRectF to = boxOf(s.fgPgid, s);
    QRectF from = prev_ ? boxOf(prev_->fgPgid, s) : to;
    if (from.isNull()) from = to;
    if (!to.isNull()) {
        QPointF a(from.center().x() + (to.center().x() - from.center().x()) * t_, from.top() + (to.top() - from.top()) * t_);
        QPointF src(a.x(), term.bottom());
        p.setPen(QPen(QColor("#00897b"), 3));
        p.drawLine(src, a - QPointF(0, 4));
        p.setBrush(QColor("#00897b"));
        QPolygonF head;
        head << a << a + QPointF(-7, -12) << a + QPointF(7, -12);
        p.drawPolygon(head);
        p.setPen(ink);
        double lx = std::max(4.0, std::min(width() - 204.0, a.x() + 10));
        p.drawText(QRectF(lx, term.bottom() + 4, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, "foreground group (owns the terminal)");
    }
    // shell
    QRectF sh = boxOf(JobShell::kShellPid, s);
    p.setPen(QPen(ink, 1.2));
    p.setBrush(s.fgPgid == JobShell::kShellPid ? QColor("#e0f2f1") : palette().color(QPalette::Base));
    p.drawRoundedRect(sh, 8, 8);
    p.setPen(ui::darkInk());
    if (s.fgPgid != JobShell::kShellPid) p.setPen(ink);
    p.drawText(sh.adjusted(4, 4, -4, -4), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
               QString("shell (PID 100)\nPGID 100, SID 100\n%1").arg(s.fgPgid == JobShell::kShellPid ? "prompt" : "waits for fg job"));
    // jobs = process groups
    for (const JcJob* j : shownJobs(s)) {
        QRectF b = boxOf(j->pgid, s);
        bool done = j->state == JcJobState::Done || j->state == JcJobState::Terminated;
        QColor frame = j->state == JcJobState::Stopped ? ui::semWaiting() : done ? ui::semIdle() : ui::semRunning();
        p.setPen(QPen(frame, j->pgid == s.fgPgid ? 3 : 1.5, done ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(b, 8, 8);
        p.setPen(ink);
        p.drawText(QRectF(b.left() + 6, b.top() + 2, b.width() - 12, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("job [%1]  PGID %2  %3%4").arg(j->id).arg(j->pgid).arg(jcJobStateName(j->state)).arg(j->pgid == s.fgPgid ? "  (fg)" : done ? "" : "  (bg)"));
        p.drawText(QRectF(b.left() + 6, b.top() + 18, b.width() - 12, 14), Qt::AlignLeft | Qt::AlignVCenter, q(j->cmdline));
        int n = (int)j->pids.size();
        double r = std::min(16.0, std::min(b.height() - 50, b.width() / (n * 3.2)) / 2);
        for (int k = 0; k < n; ++k) {
            const JcProc* pr = nullptr;
            for (const JcProc& x : s.procs) if (x.pid == j->pids[k]) pr = &x;
            if (!pr) continue;
            QPointF c(b.left() + b.width() * (k + 0.5) / n, b.top() + 34 + r + 4);
            bool changed = false;
            if (prev_) for (const JcProc& o : prev_->procs) if (o.pid == pr->pid && o.state != pr->state) changed = true;
            if (changed && t_ < 1) {
                QColor fl("#ffd54f");
                fl.setAlphaF(0.8 * (1 - t_));
                p.setPen(Qt::NoPen);
                p.setBrush(fl);
                p.drawEllipse(c, r + 7, r + 7);
            }
            p.setPen(QPen(ink, 1));
            p.setBrush(stateColor(pr->state));
            p.drawEllipse(c, r, r);
            p.setPen(Qt::white);
            p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter, QString::number(pr->pid));
            p.setPen(ink);
            QString info = QString("%1\n%2").arg(q(pr->cmd), jcStateName(pr->state));
            if (!pr->waitingOn.empty() && pr->state == JcState::Sleeping) info += "\n" + q(pr->waitingOn);
            if (pr->signal) info += QString("\nsignal %1").arg(pr->signal);
            p.drawText(QRectF(c.x() - b.width() / (2 * n), c.y() + r + 2, b.width() / n, b.bottom() - c.y() - r - 2), Qt::AlignHCenter | Qt::AlignTop, info);
        }
        // pipe between the two pipeline stages (contents from the IPC pipe model)
        if (n == 2 && s.pipeCapacity > 0 && !done) {
            double x0 = b.left() + b.width() * 0.5 / n + r + 4, x1 = b.left() + b.width() * 1.5 / n - r - 4;
            double y = b.top() + 34 + r + 4;
            double cw = std::max(8.0, (x1 - x0) / s.pipeCapacity);
            for (int k = 0; k < s.pipeCapacity; ++k) {
                QRectF cell(x0 + k * cw, y - 8, cw - 2, 16);
                bool full = k < (int)s.pipeBuffer.size();
                p.setPen(QPen(ink, 1));
                p.setBrush(full ? QColor("#ffe082") : palette().color(QPalette::Base));
                p.drawRect(cell);
                if (full) { p.setPen(ui::darkInk()); p.drawText(cell, Qt::AlignCenter, QString(QChar(s.pipeBuffer[k]))); }
            }
        }
    }
    p.setPen(ink);
    p.drawText(QRectF(8, height() - 18, width() - 16, 16), Qt::AlignLeft | Qt::AlignVCenter,
               "green = running   blue = sleeping   orange = stopped   purple = exited   red = killed   box = process group (job)");
}

// ======================================================================= page

JobControlPage::JobControlPage(QWidget* parent) : QWidget(parent) {
    header_ = new ModuleHeader("Job Control",
        "Shell job control as a deterministic model: jobs, process groups, one session and ONE foreground group that owns the "
        "terminal. fg / bg / Ctrl-Z here are simulated; the Live Linux tab shows real process groups and signals.");
    scenario_ = new QComboBox;
    scenario_->addItem("(empty session)");
    for (const JcScenario& s : jobScenarios()) scenario_->addItem(q(s.name));
    scenarioText_ = new QLabel;
    scenarioText_->setWordWrap(true);
    input_ = new QLineEdit;
    input_->setPlaceholderText("command, e.g. compute 8 &   (help lists the grammar)");
    input_->setToolTip("Limited educational grammar - nothing is executed on the host");
    auto* run = new QPushButton("Run command");
    program_ = new QComboBox;
    program_->addItems({"compute N", "sleep N", "readline", "gen N | upper"});
    amount_ = new QSpinBox;
    amount_->setRange(1, 50);
    amount_->setValue(6);
    background_ = new QCheckBox("in the background (&&)");
    auto* launch = new QPushButton("Launch program");
    target_ = new QComboBox;
    target_->setToolTip("Job to control");
    auto btn = [](const QString& t, const QString& tip) { auto* b = new QPushButton(t); b->setToolTip(tip); return b; };
    auto* bJobs = btn("jobs", "List the jobs");
    auto* bFg = btn("fg", "Bring the job to the foreground (continues it if stopped)");
    auto* bBg = btn("bg", "Continue a stopped job in the background");
    auto* bStop = btn("kill -STOP", "Stop the job's process group");
    auto* bCont = btn("kill -CONT", "Continue the job's process group (stays where it is)");
    auto* bTerm = btn("kill -TERM", "Terminate the job's process group");
    auto* bZ = btn("Ctrl-Z", "SIGTSTP to the foreground process group");
    auto* bC = btn("Ctrl-C", "SIGINT to the foreground process group");
    auto* bTick = btn("tick", "Advance one tick");
    auto* bUntil = btn("until prompt", "Advance until the shell has the terminal again");
    auto* bWait = btn("wait", "Wait for the background jobs");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow("Scenario:", scenario_);
    cf->addRow(scenarioText_);
    cf->addRow(new QLabel("<b>Console</b>"));
    cf->addRow(input_);
    cf->addRow(run);
    cf->addRow(new QLabel("<b>Launch</b>"));
    cf->addRow("Program:", program_);
    cf->addRow("N:", amount_);
    cf->addRow(background_);
    cf->addRow(launch);
    cf->addRow(new QLabel("<b>Job control</b>"));
    cf->addRow("Job:", target_);
    auto* g = new QWidget;
    auto* gl = new QGridLayout(g);
    gl->setContentsMargins(0, 0, 0, 0);
    QList<QPushButton*> bs = {bJobs, bFg, bBg, bWait, bStop, bCont, bTerm, bZ, bC, bTick, bUntil};
    for (int i = 0; i < bs.size(); ++i) gl->addWidget(bs[i], i / 2, i % 2);
    cf->addRow(g);

    view_ = new JobView;

    jobs_ = new QTableWidget(0, 5);
    jobs_->setHorizontalHeaderLabels({"Job", "PGID", "State", "Where", "Command"});
    jobs_->verticalHeader()->setVisible(false);
    jobs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    jobs_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    jobs_->horizontalHeader()->setStretchLastSection(true);
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Process", "PID", "PGID", "SID", "State"});
    details_ = new QLabel(
        "<b>Job id</b> = the shell's number (%1); <b>PID</b> = the kernel's process id; <b>PGID</b> = id of the process group "
        "(the PID of the job's first process); <b>SID</b> = session (the shell's PID). The terminal sends Ctrl-Z / Ctrl-C to "
        "its FOREGROUND group only. Stopped = paused by a signal; sleeping = waiting for an event; a background job is "
        "running, not blocked - unless it reads the terminal (SIGTTIN).");
    details_->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>Jobs</b>"));
    il->addWidget(jobs_, 1);
    il->addWidget(new QLabel("<b>Process tree</b>"));
    il->addWidget(tree_, 1);
    il->addWidget(details_);

    bar_ = new PlaybackBar;
    auto* bottom = new QTabWidget;
    transcript_ = new QPlainTextEdit;
    transcript_->setReadOnly(true);
    transcript_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bottom->addTab(transcript_, "Terminal transcript");
    timeline_ = new QTableWidget(0, 4);
    timeline_->setHorizontalHeaderLabels({"Tick", "Kind", "Job", "Event"});
    timeline_->verticalHeader()->setVisible(false);
    timeline_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    timeline_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    timeline_->horizontalHeader()->setStretchLastSection(true);
    bottom->addTab(timeline_, "Event timeline");
    runner_ = new NativeRunner(this);
    native_ = new QTableWidget(0, 6);
    native_->setHorizontalHeaderLabels({"Time (ms)", "Role", "PID", "PPID", "Event", "Detail"});
    native_->verticalHeader()->setVisible(false);
    native_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    native_->horizontalHeader()->setStretchLastSection(true);
    auto* nW = new QWidget;
    auto* nl = new QVBoxLayout(nW);
    auto* nLbl = new QLabel(NativeRunner::available()
        ? "<b>Live Linux Demo</b>: ossim_helper puts its OWN children into new process groups (setpgid), stops and continues a group "
          "with kill(-pgid, SIGSTOP / SIGCONT) observed through waitpid(WUNTRACED / WCONTINUED), runs a two-process pipeline in one "
          "group and terminates the first group. There is no controlling terminal (no tcsetpgrp), so foreground ownership is only modelled above."
        : "<b>Live Linux Demo unavailable</b> on this platform; the simulated job control works everywhere.");
    nLbl->setWordWrap(true);
    auto* nRun = new QPushButton("Run Live Linux process-group demo");
    nRun->setEnabled(NativeRunner::available());
    nl->addWidget(nLbl);
    nl->addWidget(nRun);
    nl->addWidget(native_, 1);
    bottom->addTab(nW, "Live Linux demo");

    assembleModule(this, header_, cfgW, view_, titledBox("Inspector", insW), bar_, bottom);

    connect(scenario_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!loading_) loadScenario(i - 1); });
    auto exec = [this] { if (!input_->text().trimmed().isEmpty()) { command(input_->text()); input_->clear(); } };
    connect(run, &QPushButton::clicked, this, exec);
    connect(input_, &QLineEdit::returnPressed, this, exec);
    connect(launch, &QPushButton::clicked, this, [this] {
        QString p = program_->currentText();
        QString line = p == "readline" ? p : QString(p).replace("N", QString::number(amount_->value()));
        command(line + (background_->isChecked() ? " &" : ""));
    });
    auto jobCmd = [this](const QString& c) {
        QString t = target_->currentData().toString();
        command(t.isEmpty() ? c : c + " " + t);
    };
    connect(bJobs, &QPushButton::clicked, this, [this] { command("jobs"); });
    connect(bFg, &QPushButton::clicked, this, [=] { jobCmd("fg"); });
    connect(bBg, &QPushButton::clicked, this, [=] { jobCmd("bg"); });
    connect(bWait, &QPushButton::clicked, this, [this] { command("wait"); });
    connect(bStop, &QPushButton::clicked, this, [=] { jobCmd("kill -STOP"); });
    connect(bCont, &QPushButton::clicked, this, [=] { jobCmd("kill -CONT"); });
    connect(bTerm, &QPushButton::clicked, this, [=] { jobCmd("kill -TERM"); });
    connect(bZ, &QPushButton::clicked, this, [this] { command("ctrl-z"); });
    connect(bC, &QPushButton::clicked, this, [this] { command("ctrl-c"); });
    connect(bTick, &QPushButton::clicked, this, [this] { command("tick"); });
    connect(bUntil, &QPushButton::clicked, this, [this] { command("until-prompt"); });
    connect(bar_, &PlaybackBar::positionChanged, this, &JobControlPage::onStep);
    connect(bar_, &PlaybackBar::resetRequested, this, [this] { rebuild(false); });
    connect(nRun, &QPushButton::clicked, this, [this] { startNativeDemo(); });
    connect(runner_, &NativeRunner::event, this, [this](const NativeEvent&) { NativeRunner::showEvents(native_, runner_->events()); });
    connect(runner_, &NativeRunner::finished, this, [this](int code, bool) {
        emit logMessage(QString("Job Control: Live Linux process-group demo finished (exit %1)").arg(code));
    });
    loadScenario(0);
}

bool JobControlPage::loadScenario(int i) {
    loading_ = true;
    scenario_->setCurrentIndex(i + 1);
    loading_ = false;
    if (i < 0) { commands_.clear(); scenarioText_->setText("Empty session: type commands or use the buttons."); rebuild(false); return true; }
    if (i >= (int)jobScenarios().size()) return false;
    JcScenario s = jobScenarios()[i];
    commands_ = s.commands;
    scenarioText_->setText(q(s.description));
    rebuild(false);
    emit logMessage("Job Control: scenario '" + q(s.name) + "'");
    return true;
}

bool JobControlPage::command(const QString& line) {
    // a command after Previous starts a new branch: keep the commands up to the shown snapshot
    const JcSnapshot* cur = current();
    if (cur && bar_->position() < bar_->length()) commands_.resize(std::max(0, cur->command + 1));
    commands_.push_back(line.trimmed().toStdString());
    rebuild(true);
    bool ok = !results_.empty() && results_.back().ok;
    header_->setStatus(q(results_.back().reply.empty() ? std::string("ok") : results_.back().reply), ok ? ui::hitColor().darker(130) : ui::semError());
    emit logMessage("Job Control: $ " + line + " -> " + q(results_.back().reply));
    return ok;
}

void JobControlPage::rebuild(bool toEnd) {
    results_.clear();
    shell_ = runJobScript(commands_, &results_);
    int n = (int)shell_.snapshots().size() - 1;
    bar_->setLength(n);
    bar_->setPosition(toEnd ? n : 0);
}

const JcSnapshot* JobControlPage::current() const {
    int p = bar_->position();
    const auto& s = shell_.snapshots();
    return p >= 0 && p < (int)s.size() ? &s[p] : nullptr;
}

void JobControlPage::onStep(int pos, bool forward) {
    const auto& snaps = shell_.snapshots();
    if (pos < 0 || pos >= (int)snaps.size()) return;
    const JcSnapshot& s = snaps[pos];
    view_->setSnapshot(&s, pos > 0 ? &snaps[pos - 1] : nullptr, forward ? bar_->animMs() : 0);
    transcript_->setPlainText(QStringList([&] { QStringList l; for (const std::string& x : s.transcript) l << q(x); return l; }()).join("\n"));
    transcript_->moveCursor(QTextCursor::End);
    timeline_->setRowCount((int)s.events.size());
    for (int i = 0; i < (int)s.events.size(); ++i) {
        const JcEvent& e = s.events[i];
        QStringList c = {QString::number(e.tick), q(e.kind), e.job ? QString("[%1]").arg(e.job) : QString("-"), q(e.text)};
        for (int k = 0; k < 4; ++k) {
            auto* it = ui::roItem(c[k]);
            if (k == 3) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            timeline_->setItem(i, k, it);
        }
    }
    timeline_->scrollToBottom();
    jobs_->setRowCount((int)s.jobs.size());
    QString keep = target_->currentData().toString();
    target_->clear();
    target_->addItem("(current job)", QString());
    for (int i = 0; i < (int)s.jobs.size(); ++i) {
        const JcJob& j = s.jobs[i];
        bool done = j.state == JcJobState::Done || j.state == JcJobState::Terminated;
        QStringList c = {QString("[%1]").arg(j.id), QString::number(j.pgid), jcJobStateName(j.state), done ? "-" : j.pgid == s.fgPgid ? "foreground" : "background", q(j.cmdline)};
        for (int k = 0; k < 5; ++k) jobs_->setItem(i, k, ui::roItem(c[k]));
        ui::shade(jobs_->item(i, 2), j.state == JcJobState::Stopped ? QColor("#fde2c4") : done ? QColor("#e0e0e0") : ui::lightGreen());
        if (!done) target_->addItem(QString("%%1  %2").arg(j.id).arg(q(j.cmdline)), QString("%%1").arg(j.id));
    }
    int ti = target_->findData(keep);
    target_->setCurrentIndex(ti >= 0 ? ti : 0);
    tree_->clear();
    auto* root = new QTreeWidgetItem(tree_, {"shell", "100", "100", "100", s.fgPgid == JobShell::kShellPid ? "running (has the terminal)" : "waiting"});
    for (const JcProc& p : s.procs)
        new QTreeWidgetItem(root, {q(p.cmd), QString::number(p.pid), QString::number(p.pgid), QString::number(p.sid),
                                   QString(jcStateName(p.state)) + (p.signal ? QString(" (signal %1)").arg(p.signal) : QString())});
    tree_->expandAll();
    for (int k = 0; k < 5; ++k) tree_->resizeColumnToContents(k);
    QString label = q(s.label);
    header_->setStatus(QString("Snapshot %1 of %2 (t = %3): %4").arg(pos).arg(snaps.size() - 1).arg(s.tick).arg(label));
}

bool JobControlPage::startNativeDemo() {
    native_->setRowCount(0);
    return runner_->start({"jobs"});
}
bool JobControlPage::nativeRunning() const { return runner_->running(); }
int JobControlPage::nativeExitCode() const { return runner_->lastExitCode(); }
int JobControlPage::nativeEventCount() const { return runner_->events().size(); }

QString JobControlPage::reportText() const {
    QString t = "Job control (simulated shell):\n";
    const auto& snaps = shell_.snapshots();
    if (snaps.empty()) return t + "  (no session)\n";
    for (const std::string& l : snaps.back().transcript) t += "  " + q(l) + "\n";
    return t;
}
