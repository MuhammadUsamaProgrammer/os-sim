// NativeRunner.cpp - asynchronous helper process for the Live Linux demos.
#include "NativeRunner.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTimer>
#include <csignal>
#include "Style.h"
#ifdef Q_OS_UNIX
#include <signal.h>
#endif

NativeRunner::NativeRunner(QObject* parent) : QObject(parent) {
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(proc_, &QProcess::readyReadStandardOutput, this, &NativeRunner::readLines);
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        readLines();
        exitCode_ = code;
        emit finished(code, st == QProcess::CrashExit);
    });
}

NativeRunner::~NativeRunner() {
    // No signals while the owner (and the main window) are being destroyed.
    blockSignals(true);
    proc_->disconnect(this);
    if (running()) {
        stop();
        if (!proc_->waitForFinished(1500)) {     // only at shutdown; demos are short
#ifdef Q_OS_UNIX
            ::kill(-(pid_t)proc_->processId(), SIGKILL);
#endif
            proc_->kill();
            proc_->waitForFinished(500);
        }
    }
}

QString NativeRunner::helperPath() {
#ifdef OSSIM_HELPER
    if (QFileInfo(OSSIM_HELPER).isExecutable()) return OSSIM_HELPER;
#endif
    QString p = QCoreApplication::applicationDirPath() + "/../apps/ossim_helper";
    return QFileInfo(p).isExecutable() ? QFileInfo(p).absoluteFilePath() : QString();
}

bool NativeRunner::available() {
#ifdef Q_OS_LINUX
    return !helperPath().isEmpty();
#else
    return false;
#endif
}

bool NativeRunner::start(const QStringList& args) {
    if (running() || !available()) return false;
    ++run_;
    partial_.clear();
    events_.clear();
    exitCode_ = -1;
    proc_->start(helperPath(), args);
    return true;
}

bool NativeRunner::running() const { return proc_->state() != QProcess::NotRunning; }

// SIGTERM to the helper's process group (the helper made itself a group leader).
void NativeRunner::stop() {
    if (!running()) return;
#ifdef Q_OS_UNIX
    pid_t pid = (pid_t)proc_->processId();
    if (pid > 0) ::kill(-pid, SIGTERM);
    int run = run_;
    QTimer::singleShot(1500, this, [this, run, pid] {
        if (run == run_ && running() && pid > 0) ::kill(-pid, SIGKILL);
    });
#else
    proc_->terminate();
#endif
}

// Parses "T=.. ROLE=.. PID=.. PPID=.. EVENT=.. | detail" lines.
void NativeRunner::readLines() {
    partial_ += proc_->readAllStandardOutput();
    static const QRegularExpression re("^T=(\\d+) ROLE=(\\S+) PID=(\\d+) PPID=(\\d+) EVENT=(\\S+) \\| (.*)$");
    int nl;
    while ((nl = partial_.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(partial_.left(nl));
        partial_.remove(0, nl + 1);
        QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch()) continue;
        NativeEvent e;
        e.ms = m.captured(1).toLong();
        e.role = m.captured(2);
        e.pid = m.captured(3).toInt();
        e.ppid = m.captured(4).toInt();
        e.event = m.captured(5);
        e.detail = m.captured(6);
        events_.append(e);
        emit event(e);
    }
}

void NativeRunner::showEvents(QTableWidget* t, const QList<NativeEvent>& ev) {
    t->setColumnCount(6);
    t->setHorizontalHeaderLabels({"Time (ms, measured)", "Role", "PID", "PPID", "Event", "Detail (real system-call result)"});
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(22);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setRowCount(ev.size());
    for (int i = 0; i < ev.size(); ++i) {
        const NativeEvent& e = ev[i];
        QStringList c = {QString::number(e.ms), e.role, QString::number(e.pid), QString::number(e.ppid), e.event, e.detail};
        for (int k = 0; k < 6; ++k) {
            auto* it = ui::roItem(c[k]);
            if (k == 5) it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (e.event == "error" || e.event == "timeout") ui::shade(it, ui::lightRed());
            t->setItem(i, k, it);
        }
    }
    if (!ev.isEmpty()) t->scrollToBottom();
}
