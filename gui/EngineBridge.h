// EngineBridge.h - connects the pthread engine to Qt.
// The engine's clock thread calls our observer; it only emits a signal. Qt
// delivers it through a queued connection to the GUI thread, so worker threads
// never touch a widget. Each run has a generation number so that events still
// queued from an old run are ignored.
#pragma once
#include <QObject>
#include <atomic>
#include <memory>
#include <vector>
#include "engine.h"

class EngineBridge : public QObject {
    Q_OBJECT
public:
    explicit EngineBridge(QObject* parent = nullptr);
    ~EngineBridge() override;              // stops and joins a running engine

    // Replays a timeline with the threaded engine (starts the pthreads).
    void start(const std::vector<PCB>& procs, const std::vector<Segment>& timeline, int delayMs);
    void pause();
    void resume();
    void step();
    void stop();                           // stop + join
    void setDelay(int ms);
    bool running() const { return engine_ != nullptr && !done_; }
    EngineSnapshot snapshot() const;       // thread-safe state copy
    std::vector<PCB> pcbs() const;

signals:
    // Emitted in the GUI thread for every engine log line of the current run.
    void engineEvent(int tick, int pid, int state, const QString& text);
    void engineFinished();
    // Internal: emitted on the engine thread, delivered queued to the GUI thread.
    void rawEvent(int gen, int tick, int pid, int state, const QString& text);

private slots:
    void onRawEvent(int gen, int tick, int pid, int state, const QString& text);

private:
    std::unique_ptr<SimulationEngine> engine_;
    std::atomic<int> gen_{0};
    bool done_ = true;
};
