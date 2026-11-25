// EngineBridge.cpp - runs SimulationEngine and forwards its events as Qt signals.
#include "EngineBridge.h"

EngineBridge::EngineBridge(QObject* parent) : QObject(parent) {
    connect(this, &EngineBridge::rawEvent, this, &EngineBridge::onRawEvent, Qt::QueuedConnection);
}

EngineBridge::~EngineBridge() { stop(); }

// Creates the engine, installs the observer and starts the threads.
void EngineBridge::start(const std::vector<PCB>& procs, const std::vector<Segment>& timeline, int delayMs) {
    stop();
    int gen = ++gen_;
    engine_ = std::make_unique<SimulationEngine>(procs, timeline, EngineOptions{delayMs, false});
    done_ = false;
    // Observer: runs on the engine's dispatcher (clock) thread -> only emit a signal.
    engine_->setObserver([this, gen](const EngineEvent& e) {
        emit rawEvent(gen, e.tick, e.pid, (int)e.state, QString::fromStdString(e.text));
    });
    engine_->start();
}

// GUI thread: drop events of old runs, join on the final line, re-emit the rest.
void EngineBridge::onRawEvent(int gen, int tick, int pid, int state, const QString& text) {
    if (gen != gen_) return;
    emit engineEvent(tick, pid, state, text);
    if (text.contains("CLOCK finished") || text.contains("CLOCK stopped")) {
        if (engine_) engine_->join();
        done_ = true;
        emit engineFinished();
    }
}

void EngineBridge::pause()  { if (engine_) engine_->pause(); }
void EngineBridge::resume() { if (engine_) engine_->resume(); }
void EngineBridge::step()   { if (engine_) engine_->step(); }
void EngineBridge::setDelay(int ms) { if (engine_) engine_->setTickDelay(ms); }

// Stops the run and joins every thread (blocks for at most one tick).
void EngineBridge::stop() {
    if (!engine_) return;
    engine_->stop();
    engine_->join();
    done_ = true;
}

EngineSnapshot EngineBridge::snapshot() const { return engine_ ? engine_->snapshot() : EngineSnapshot{}; }
std::vector<PCB> EngineBridge::pcbs() const { return engine_ ? engine_->pcbs() : std::vector<PCB>{}; }
