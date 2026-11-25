// engine.h - multithreaded replay of a schedule (OS concepts: threads, mutex,
// semaphores, condition variables, pipe IPC, fork/exec/wait).
//
// core/ computes the schedule; this engine only REPLAYS it with real POSIX
// primitives, so the result is identical on every run:
//   * one clock thread drives the ticks (it is also the single event dispatcher),
//   * one thread per simulated process executes its CPU ticks,
//   * the handshake  clock --sem_post(runSem[i])--> process --sem_post(doneSem)--> clock
//     serialises the threads, so the order of the log lines is always the same,
//   * a mutex protects the PCBs/ready queue/tick, a condition variable implements pause/step,
//   * all log lines travel through a pipe (PipeLogger).
#pragma once
#include <functional>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <vector>
#include "logger.h"
#include "pcb.h"
#include "scheduler.h"

struct EngineOptions {
    int tickDelayMs = 0;       // 0 for tests, e.g. 200 for animation
    bool useRealDemo = false;  // run realProcessDemo(2) (fork/exec/wait) before the first tick
};

// One log line, decoded. pid = -1 for CLOCK/MEM/DEADLOCK/REAL lines.
struct EngineEvent { int tick; int pid; ProcState state; std::string text; };

struct ProcSnapshot { int pid; ProcState state; int remaining; };
struct EngineSnapshot { int tick = 0; bool finished = false; bool paused = false;
                        std::vector<ProcSnapshot> procs; };

class SimulationEngine {
public:
    SimulationEngine(std::vector<PCB> procs, std::vector<Segment> timeline, EngineOptions opt = {});
    ~SimulationEngine();                  // stops and joins if still running
    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;

    // Called by the dispatcher (clock thread) for every log line, never while the mutex is held.
    void setObserver(std::function<void(const EngineEvent&)> obs) { observer_ = std::move(obs); }

    void start(bool startPaused = false); // creates all threads
    void pause();                         // clock waits on the condition variable
    void resume();                        // signals the condition variable
    void step();                          // runs exactly one tick, then stays paused
    void stop();                          // ends the run early
    void join();                          // pthread_join all threads (idempotent)

    EngineSnapshot snapshot() const;      // thread-safe copy of tick + states
    std::vector<std::string> log() const; // every line drained so far
    std::vector<PCB> pcbs() const;        // thread-safe copy of the PCBs
    bool finished() const;

private:
    static void* clockMain(void* self);   // pthread entry points
    static void* procMain(void* arg);
    void clockLoop();
    void procLoop(int idx);
    void logLocked(const std::string& line);  // writes "[t=..] ..." into the pipe
    void dispatch();                      // drain pipe, call observer
    int pidAt(int t) const;               // which pid the timeline runs at tick t

    std::vector<PCB> procs_;
    std::vector<Segment> timeline_;
    EngineOptions opt_;
    std::function<void(const EngineEvent&)> observer_;
    PipeLogger logger_;

    mutable pthread_mutex_t mtx_;         // protects everything below
    pthread_cond_t cond_;                 // pause / resume / step / timed sleep
    sem_t* runSem_ = nullptr;             // one per process: "run one tick now"
    sem_t doneSem_;                       // process -> clock: "tick executed"
    pthread_t clockThread_{};
    std::vector<pthread_t> procThreads_;
    struct ProcArg { SimulationEngine* eng; int idx; };
    std::vector<ProcArg> procArgs_;

    int currentTick_ = 0;
    int running_ = -1;                    // index of the Running process
    std::vector<int> readyQueue_;         // indices of Ready processes (FIFO of arrival)
    bool paused_ = false;
    int stepBudget_ = 0;                  // ticks allowed while paused (step())
    bool stopRequested_ = false;          // stop(): end the clock loop
    bool exitThreads_ = false;            // set by the clock thread: process threads must exit
    bool finished_ = false;
    bool started_ = false, joined_ = false;
};

// Decodes "[t=3] P2 RUNNING remaining=1" into an EngineEvent (pid -1 for other lines).
EngineEvent parseLogLine(const std::string& line);

// Real-process demo (OS concept: process creation): fork() n children, each
// execlp("sleep","sleep","1"); the parent waitpid()s each one. Returns log lines.
std::vector<std::string> realProcessDemo(int n);
