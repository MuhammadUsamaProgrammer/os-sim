// threads.h - multithreading models (M6): user threads, kernel execution contexts, CPU cores.
// Educational abstraction - it does NOT change how Linux maps pthreads (Linux uses one-to-one).
//
// Rules (one logical tick per step, deterministic):
//  * Many-to-One: all user threads share ONE kernel context, so at most one runs at a time
//    whatever the number of cores; a blocking call blocks the context and so the whole process.
//  * One-to-One: every user thread has its own kernel context; up to `cores` run in parallel;
//    a blocking call blocks only that thread.
//  * Many-to-Many: user threads are multiplexed over a pool of K contexts; a thread that blocks
//    keeps its context (no new context is created); parallelism <= min(cores, free contexts).
//  * Dispatch: round robin with a time quantum (ready queue order; a thread whose quantum
//    expires goes to the back if others are waiting). Threads keep their previous core / context
//    when possible; a change of core is counted as a migration.
//  * A thread with joinTarget >= 0 finishes its own work, then waits (Joining) until the target
//    has completed; pthread_join returns in the tick the target completes.
//  * Each context runs at most one thread, each core at most one context, no thread on two cores.
#pragma once
#include <string>
#include <vector>

enum class ThreadModel { ManyToOne, OneToOne, ManyToMany };
const char* threadModelName(ThreadModel);
enum class TState { New, Ready, Running, Blocked, Joining, Completed };
const char* tStateName(TState);

struct UThreadSpec {
    int id = 0;
    std::string name;
    int arrival = 0;
    int work = 4;          // logical CPU ticks of work
    int blockAt = -1;      // after this many work ticks the thread makes a blocking call (-1 = never)
    int blockFor = 0;      // ticks the blocking call lasts
    int joinTarget = -1;   // thread id this thread joins after its own work (-1 = none)
    int arg = 0;           // argument passed at creation (shown in the inspector)
};

struct ThreadConfig {
    ThreadModel model = ThreadModel::OneToOne;
    int contexts = 2;      // kernel contexts for Many-to-Many (1 for Many-to-One, n for One-to-One)
    int cores = 2;
    int quantum = 1;
    std::vector<UThreadSpec> threads;
};
void validateThreadConfig(const ThreadConfig&);   // throws std::invalid_argument
int effectiveContexts(const ThreadConfig&);

struct TSnap {
    TState state = TState::New;
    int context = -1, core = -1, done = 0, blockLeft = 0, finish = -1;
    std::string reason;    // why it is not running
};

struct ThreadTick {
    int t = 0;
    std::vector<TSnap> threads;
    std::vector<int> coreThread;      // thread index on each core (-1 idle)
    std::vector<int> contextThread;   // thread index on each context (-1 free)
    std::vector<bool> contextBlocked;
    std::vector<std::string> events;
};

struct ThreadMetrics {
    int completion = 0;               // ticks until every thread completed
    int busyCoreTicks = 0;
    int maxParallel = 0;              // most threads running in the same tick
    int waitingTicks = 0;             // Ready but not running (summed over threads)
    int blockedTicks = 0, stalledTicks = 0;   // stalled = Ready but the process / contexts are blocked
    int migrations = 0, dispatches = 0;
    double occupancy(int cores) const { return completion > 0 ? 100.0 * busyCoreTicks / (cores * completion) : 0; }
};

struct ThreadResult {
    ThreadConfig cfg;
    std::vector<ThreadTick> ticks;    // ticks[k] = state DURING tick k (after its decisions)
    ThreadMetrics m;
    std::vector<int> completionOrder; // thread ids
    std::vector<TSnap> finalState;    // after the last tick (every thread Completed when completed)
    bool completed = false;
};

ThreadResult runThreads(const ThreadConfig& cfg);

struct ThreadPreset { std::string name, description; ThreadConfig cfg; };
std::vector<ThreadPreset> threadPresets();
