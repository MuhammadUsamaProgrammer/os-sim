// sync_demo.h - REAL concurrent demonstrations of the synchronization primitives
// (OS concepts: pthread_create/join, pthread_mutex, POSIX semaphores, condition
// variables, Peterson's algorithm with sequentially consistent atomics, pipe IPC).
//
// core/sync.* shows the algorithms step by step (deterministic). This file runs
// the same ideas with real threads, so the results are real but the interleaving
// is chosen by the OS scheduler (not reproducible, except the final checks).
//
// Thread safety rules used here:
//   * shared data is only touched with the primitive under test held, or through std::atomic,
//   * the "race" demo uses std::atomic with a separate load and store (read-modify-write
//     split in two) - updates can be lost, but there is no undefined behaviour,
//   * log lines from worker threads travel through a pipe (PipeLogger); the thread that
//     called run() is the single dispatcher that forwards them to the observer.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <vector>
#include "logger.h"

enum class SyncDemoKind { RACE, PETERSON, MUTEX, BINARY_SEM, COUNTING_SEM, PRODUCER_CONSUMER, MONITOR };
const char* syncDemoName(SyncDemoKind);

struct SyncDemoParams {
    int threads = 4;          // worker threads (Peterson always uses 2)
    int iterations = 20000;   // increments per thread (counter demos) / uses per thread (counting)
    int resources = 2;        // counting semaphore initial value
    int producers = 2, consumers = 2, capacity = 4, items = 20;   // items per producer
    int delayUs = 0;          // sleep inside the critical section / per item (makes blocking visible)
};
// Throws std::invalid_argument on a bad parameter.
void validateSyncDemoParams(SyncDemoKind, const SyncDemoParams&);

struct SyncDemoResult {
    SyncDemoKind kind = SyncDemoKind::MUTEX;
    long expected = 0, actual = 0;   // counter / uses / consumed-items checksum
    int maxConcurrent = 0;           // most threads seen inside the critical section at once
    int limit = 1;                   // allowed number inside (1, or S for a counting semaphore)
    int overflows = 0, underflows = 0;
    int threadsCreated = 0;
    bool cancelled = false;
    bool ok = false;                 // all checks of this demo passed
    double millis = 0;
    std::string summary;             // one-line result
    std::vector<std::string> log;    // every log line of the run
};

class SyncDemoRunner {
public:
    SyncDemoRunner();
    ~SyncDemoRunner();
    SyncDemoRunner(const SyncDemoRunner&) = delete;
    SyncDemoRunner& operator=(const SyncDemoRunner&) = delete;

    // Called on the thread that runs run() (the dispatcher), once per log line.
    void setObserver(std::function<void(const std::string&)> obs) { observer_ = std::move(obs); }
    // Runs one demo to the end (blocking): creates, runs and joins all threads.
    SyncDemoResult run(SyncDemoKind kind, const SyncDemoParams& p);
    // Asks a running demo to stop early. Safe from any thread; wakes blocked threads.
    void cancel();

    // ---- used by the worker threads (public so the static thread functions can reach them)
    struct Worker { SyncDemoRunner* self; int index; };
    void logLine(const std::string& s);
    bool cancelled() const { return cancel_.load(); }
    void enterCS();                   // counts threads inside the critical section
    void leaveCS();

    SyncDemoParams p_;
    SyncDemoKind kind_ = SyncDemoKind::MUTEX;
    long counter_ = 0;                          // protected by the primitive under test
    std::atomic<long> raceCounter_{0};          // race demo: split load/store
    std::atomic<bool> interested_[2];           // Peterson (seq_cst)
    std::atomic<int> turn_{0};
    pthread_mutex_t mtx_;                       // mutex demo, buffer mutex, monitor lock
    pthread_cond_t notFull_, notEmpty_;         // monitor condition variables
    sem_t semA_, semB_, semC_;                  // S / empty, full, mutex
    std::vector<int> buffer_;
    int in_ = 0, out_ = 0, count_ = 0;          // protected by the buffer mutex
    long producedSum_ = 0, consumedSum_ = 0, consumedCount_ = 0;
    std::atomic<int> inCS_{0}, maxInCS_{0}, overflows_{0}, underflows_{0}, uses_{0};
    std::atomic<int> running_{0};               // worker threads not finished yet

private:
    std::function<void(const std::string&)> observer_;
    std::atomic<bool> cancel_{false};
    pthread_mutex_t pokeMtx_;                   // protects active_ (cancel() vs. init/destroy)
    bool active_ = false;                       // primitives of the current demo exist
    bool semsReady_ = false, condReady_ = false;
    std::unique_ptr<PipeLogger> logger_;        // pipe: workers write, dispatcher reads
    std::vector<std::string> lines_;
    void drain();                               // forwards new pipe lines to the observer
};
