// SyncBridge.cpp - background thread for the real synchronization demos.
#include "SyncBridge.h"

SyncBridge::SyncBridge(QObject* parent) : QObject(parent) {
    connect(this, &SyncBridge::rawLine, this, &SyncBridge::onRawLine, Qt::QueuedConnection);
    connect(this, &SyncBridge::rawFinished, this, &SyncBridge::onRawFinished, Qt::QueuedConnection);
}

SyncBridge::~SyncBridge() { stop(); }

void SyncBridge::joinThread() { if (thread_.joinable()) thread_.join(); }

// Validates first (so the GUI can show a warning), then runs the demo on a new thread.
bool SyncBridge::start(SyncDemoKind kind, const SyncDemoParams& params) {
    if (running_) return false;
    validateSyncDemoParams(kind, params);
    joinThread();
    int gen = ++gen_;
    runner_ = std::make_unique<SyncDemoRunner>();
    // Observer runs on the background (dispatcher) thread: only emit a signal.
    runner_->setObserver([this, gen](const std::string& l) { emit rawLine(gen, QString::fromStdString(l)); });
    running_ = true;
    SyncDemoRunner* r = runner_.get();
    thread_ = std::thread([this, r, kind, params, gen] {
        SyncDemoResult res = r->run(kind, params);
        {
            std::lock_guard<std::mutex> g(resultMtx_);
            result_ = res;
        }
        emit rawFinished(gen);
    });
    return true;
}

void SyncBridge::onRawLine(int gen, const QString& text) {
    if (gen == gen_) emit line(text);
}

void SyncBridge::onRawFinished(int gen) {
    if (gen != gen_) return;
    joinThread();                          // the thread has already returned
    running_ = false;
    emit finished();
}

// Cancel wakes every blocked worker; run() then joins them and returns.
void SyncBridge::stop() {
    if (runner_) runner_->cancel();
    joinThread();
}

SyncDemoResult SyncBridge::result() const {
    std::lock_guard<std::mutex> g(resultMtx_);
    return result_;
}
