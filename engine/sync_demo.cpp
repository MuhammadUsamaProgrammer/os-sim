// sync_demo.cpp - real pthread demonstrations of synchronization (see sync_demo.h).
#include "sync_demo.h"
#include <chrono>
#include <sched.h>
#include <stdexcept>
#include <unistd.h>

const char* syncDemoName(SyncDemoKind k) {
    switch (k) {
        case SyncDemoKind::RACE: return "Race (atomic load + store, split)";
        case SyncDemoKind::PETERSON: return "Peterson (std::atomic, seq_cst)";
        case SyncDemoKind::MUTEX: return "pthread_mutex";
        case SyncDemoKind::BINARY_SEM: return "Binary semaphore (sem_t = 1)";
        case SyncDemoKind::COUNTING_SEM: return "Counting semaphore (sem_t = S)";
        case SyncDemoKind::PRODUCER_CONSUMER: return "Producer-consumer (sem_t empty/full + mutex)";
        case SyncDemoKind::MONITOR: return "Monitor (mutex + pthread_cond)";
    }
    return "?";
}

void validateSyncDemoParams(SyncDemoKind k, const SyncDemoParams& p) {
    auto need = [](bool ok, const char* msg) { if (!ok) throw std::invalid_argument(msg); };
    need(p.threads >= 1 && p.threads <= 16, "threads must be between 1 and 16");
    need(p.iterations >= 1 && p.iterations <= 1000000, "iterations must be between 1 and 1000000");
    need(p.delayUs >= 0 && p.delayUs <= 200000, "delay must be between 0 and 200000 microseconds");
    if (k == SyncDemoKind::COUNTING_SEM) need(p.resources >= 1 && p.resources <= 16, "resources must be between 1 and 16");
    if (k == SyncDemoKind::PRODUCER_CONSUMER || k == SyncDemoKind::MONITOR) {
        need(p.producers >= 1 && p.producers <= 8 && p.consumers >= 1 && p.consumers <= 8,
             "producers and consumers must be between 1 and 8");
        need(p.capacity >= 1 && p.capacity <= 64, "buffer capacity must be between 1 and 64");
        need(p.items >= 1 && p.items <= 500, "items per producer must be between 1 and 500");
        need(p.producers * p.items >= p.consumers, "producers x items must be >= consumers");
    }
}

SyncDemoRunner::SyncDemoRunner() {
    interested_[0] = false;
    interested_[1] = false;
    pthread_mutex_init(&pokeMtx_, nullptr);
}

SyncDemoRunner::~SyncDemoRunner() { pthread_mutex_destroy(&pokeMtx_); }

void SyncDemoRunner::logLine(const std::string& s) { if (logger_) logger_->logWrite(s); }

// Dispatcher: moves complete lines from the pipe to the log and the observer.
void SyncDemoRunner::drain() {
    for (const std::string& l : logger_->drainLog()) {
        lines_.push_back(l);
        if (observer_) observer_(l);
    }
}

// Counts the threads inside the critical section and remembers the maximum.
void SyncDemoRunner::enterCS() {
    int now = ++inCS_;
    int old = maxInCS_.load();
    while (now > old && !maxInCS_.compare_exchange_weak(old, now)) {}
}
void SyncDemoRunner::leaveCS() { --inCS_; }

// Stops a running demo early: sets the flag, then wakes every thread that may sleep.
void SyncDemoRunner::cancel() {
    cancel_ = true;
    pthread_mutex_lock(&pokeMtx_);
    if (active_) {
        if (semsReady_) { sem_post(&semA_); sem_post(&semB_); }
        if (condReady_) {
            pthread_mutex_lock(&mtx_);              // waiters check the flag under this lock,
            pthread_cond_broadcast(&notFull_);      // so no wake-up can be missed
            pthread_cond_broadcast(&notEmpty_);
            pthread_mutex_unlock(&mtx_);
        }
    }
    pthread_mutex_unlock(&pokeMtx_);
}

// ---------------------------------------------------------------- worker bodies

static void usleepIf(int us) { if (us > 0) usleep((useconds_t)us); }

// Race: counter++ as an atomic LOAD followed by a separate atomic STORE.
// Another thread can store in between, so an increment is lost (no undefined behaviour).
static void raceBody(SyncDemoRunner& r, int me) {
    for (int i = 0; i < r.p_.iterations && !r.cancelled(); ++i) {
        long v = r.raceCounter_.load(std::memory_order_relaxed);      // READ
        r.raceCounter_.store(v + 1, std::memory_order_relaxed);       // MODIFY + WRITE
    }
    r.logLine("T" + std::to_string(me) + " finished its increments (unsynchronized)");
}

// Peterson's solution exactly as in the lecture, with seq_cst atomics (default ordering).
static void petersonBody(SyncDemoRunner& r, int process) {
    for (int i = 0; i < r.p_.iterations; ++i) {
        int other = 1 - process;
        r.interested_[process] = true;                                // seq_cst store
        r.turn_ = process;                                            // seq_cst store
        while (r.interested_[other] == true && r.turn_ == process) {  // busy wait (seq_cst loads)
            if (r.cancelled()) { r.interested_[process] = false; return; }
            sched_yield();                                            // be kind to a 1-CPU VM
        }
        r.enterCS();
        r.counter_++;                                                 // critical section
        usleepIf(r.p_.delayUs);
        r.leaveCS();
        r.interested_[process] = false;                               // exit section
        if (r.cancelled()) return;
    }
    r.logLine("T" + std::to_string(process) + " finished (Peterson)");
}

static void mutexBody(SyncDemoRunner& r, int me) {
    for (int i = 0; i < r.p_.iterations && !r.cancelled(); ++i) {
        pthread_mutex_lock(&r.mtx_);
        r.enterCS();
        r.counter_++;
        usleepIf(r.p_.delayUs);
        r.leaveCS();
        pthread_mutex_unlock(&r.mtx_);
    }
    r.logLine("T" + std::to_string(me) + " finished (pthread_mutex_lock / unlock)");
}

// Binary (S = 1) or counting (S = resources) semaphore around the critical section.
static void semBody(SyncDemoRunner& r, int me, bool counting) {
    for (int i = 0; i < r.p_.iterations && !r.cancelled(); ++i) {
        sem_wait(&r.semA_);
        if (r.cancelled()) { sem_post(&r.semA_); break; }             // pass the wake-up on
        r.enterCS();
        if (counting) {
            int now = r.inCS_.load();
            r.logLine("T" + std::to_string(me) + " got an instance (" + std::to_string(now) + " in use)");
            usleepIf(r.p_.delayUs > 0 ? r.p_.delayUs : 200);
            r.uses_++;
        } else {
            r.counter_++;
            usleepIf(r.p_.delayUs);
        }
        r.leaveCS();
        sem_post(&r.semA_);
    }
    r.logLine("T" + std::to_string(me) + " finished (sem_wait / sem_post)");
}

// Producer with semaphores: wait(empty), lock, insert, unlock, post(full).
static void producerBody(SyncDemoRunner& r, int id) {
    for (int i = 0; i < r.p_.items && !r.cancelled(); ++i) {
        int item = id * 1000 + i;
        sem_wait(&r.semA_);                                           // empty
        if (r.cancelled()) { sem_post(&r.semA_); break; }
        pthread_mutex_lock(&r.mtx_);
        r.enterCS();
        int count;
        if (r.count_ >= r.p_.capacity) r.overflows_++;
        else {
            r.buffer_[r.in_] = item;
            r.in_ = (r.in_ + 1) % r.p_.capacity;
            r.count_++;
            r.producedSum_ += item;
        }
        count = r.count_;
        r.leaveCS();
        pthread_mutex_unlock(&r.mtx_);
        sem_post(&r.semB_);                                           // full
        r.logLine("Producer " + std::to_string(id) + " put item " + std::to_string(item) +
                  " (count " + std::to_string(count) + ")");
        usleepIf(r.p_.delayUs);
    }
}

static void consumerBody(SyncDemoRunner& r, int id, int myItems) {
    for (int i = 0; i < myItems && !r.cancelled(); ++i) {
        sem_wait(&r.semB_);                                           // full
        if (r.cancelled()) { sem_post(&r.semB_); break; }
        pthread_mutex_lock(&r.mtx_);
        r.enterCS();
        int item = -1, count;
        if (r.count_ <= 0) r.underflows_++;
        else {
            item = r.buffer_[r.out_];
            r.out_ = (r.out_ + 1) % r.p_.capacity;
            r.count_--;
            r.consumedSum_ += item;
            r.consumedCount_++;
        }
        count = r.count_;
        r.leaveCS();
        pthread_mutex_unlock(&r.mtx_);
        sem_post(&r.semA_);                                           // empty
        r.logLine("Consumer " + std::to_string(id) + " took item " + std::to_string(item) +
                  " (count " + std::to_string(count) + ")");
        usleepIf(r.p_.delayUs);
    }
}

// Monitor operations: the lock is held for the whole operation; while-loops re-check (Mesa).
static bool monitorInsert(SyncDemoRunner& r, int item, int& countAfter) {
    pthread_mutex_lock(&r.mtx_);
    while (r.count_ == r.p_.capacity && !r.cancelled()) pthread_cond_wait(&r.notFull_, &r.mtx_);
    if (r.cancelled()) { pthread_mutex_unlock(&r.mtx_); return false; }
    r.enterCS();
    if (r.count_ >= r.p_.capacity) r.overflows_++;
    else {
        r.buffer_[r.in_] = item;
        r.in_ = (r.in_ + 1) % r.p_.capacity;
        r.count_++;
        r.producedSum_ += item;
    }
    countAfter = r.count_;
    r.leaveCS();
    pthread_cond_signal(&r.notEmpty_);
    pthread_mutex_unlock(&r.mtx_);
    return true;
}

static bool monitorRemove(SyncDemoRunner& r, int& item, int& countAfter) {
    pthread_mutex_lock(&r.mtx_);
    while (r.count_ == 0 && !r.cancelled()) pthread_cond_wait(&r.notEmpty_, &r.mtx_);
    if (r.cancelled()) { pthread_mutex_unlock(&r.mtx_); return false; }
    r.enterCS();
    if (r.count_ <= 0) r.underflows_++;
    else {
        item = r.buffer_[r.out_];
        r.out_ = (r.out_ + 1) % r.p_.capacity;
        r.count_--;
        r.consumedSum_ += item;
        r.consumedCount_++;
    }
    countAfter = r.count_;
    r.leaveCS();
    pthread_cond_signal(&r.notFull_);
    pthread_mutex_unlock(&r.mtx_);
    return true;
}

// Thread entry point (pthread_create): runs the body that belongs to this demo and index.
static void* workerMain(void* arg) {
    auto* w = static_cast<SyncDemoRunner::Worker*>(arg);
    SyncDemoRunner& r = *w->self;
    int i = w->index;
    switch (r.kind_) {
        case SyncDemoKind::RACE: raceBody(r, i); break;
        case SyncDemoKind::PETERSON: petersonBody(r, i); break;
        case SyncDemoKind::MUTEX: mutexBody(r, i); break;
        case SyncDemoKind::BINARY_SEM: semBody(r, i, false); break;
        case SyncDemoKind::COUNTING_SEM: semBody(r, i, true); break;
        case SyncDemoKind::PRODUCER_CONSUMER:
        case SyncDemoKind::MONITOR: {
            int total = r.p_.producers * r.p_.items;
            bool producer = i < r.p_.producers;
            int c = i - r.p_.producers;
            int mine = producer ? r.p_.items : total / r.p_.consumers + (c < total % r.p_.consumers ? 1 : 0);
            if (r.kind_ == SyncDemoKind::PRODUCER_CONSUMER) {
                if (producer) producerBody(r, i); else consumerBody(r, c, mine);
            } else {
                for (int k = 0; k < mine && !r.cancelled(); ++k) {
                    int item = i * 1000 + k, count = 0;
                    if (producer) {
                        if (!monitorInsert(r, item, count)) break;
                        r.logLine("Producer " + std::to_string(i) + " insert(" + std::to_string(item) +
                                  ") count " + std::to_string(count));
                    } else {
                        if (!monitorRemove(r, item, count)) break;
                        r.logLine("Consumer " + std::to_string(c) + " remove() = " + std::to_string(item) +
                                  " count " + std::to_string(count));
                    }
                    usleepIf(r.p_.delayUs);
                }
            }
            break;
        }
    }
    r.running_--;
    return nullptr;
}

// ---------------------------------------------------------------- run

SyncDemoResult SyncDemoRunner::run(SyncDemoKind kind, const SyncDemoParams& params) {
    validateSyncDemoParams(kind, params);
    auto t0 = std::chrono::steady_clock::now();
    kind_ = kind;
    p_ = params;
    if (kind == SyncDemoKind::PETERSON) p_.threads = 2;              // Peterson is for 2 processes
    // cancel_ is NOT cleared here: a cancel() that arrives just before run() still counts.
    counter_ = 0;
    raceCounter_ = 0;
    interested_[0] = false;
    interested_[1] = false;
    turn_ = 0;
    in_ = out_ = count_ = 0;
    producedSum_ = consumedSum_ = consumedCount_ = 0;
    inCS_ = maxInCS_ = overflows_ = underflows_ = uses_ = 0;
    lines_.clear();
    logger_ = std::make_unique<PipeLogger>();
    bool buffer = kind == SyncDemoKind::PRODUCER_CONSUMER || kind == SyncDemoKind::MONITOR;
    int nThreads = buffer ? p_.producers + p_.consumers : p_.threads;
    buffer_.assign(buffer ? p_.capacity : 0, -1);

    // Create the primitives of this demo.
    pthread_mutex_init(&mtx_, nullptr);
    bool sems = kind == SyncDemoKind::BINARY_SEM || kind == SyncDemoKind::COUNTING_SEM ||
                kind == SyncDemoKind::PRODUCER_CONSUMER;
    bool conds = kind == SyncDemoKind::MONITOR;
    if (sems) {
        unsigned a = kind == SyncDemoKind::BINARY_SEM ? 1u : kind == SyncDemoKind::COUNTING_SEM
                     ? (unsigned)p_.resources : (unsigned)p_.capacity;
        sem_init(&semA_, 0, a);                                       // S, or empty = N
        sem_init(&semB_, 0, 0);                                       // full = 0
    }
    if (conds) {
        pthread_cond_init(&notFull_, nullptr);
        pthread_cond_init(&notEmpty_, nullptr);
    }
    pthread_mutex_lock(&pokeMtx_);
    semsReady_ = sems;
    condReady_ = conds;
    active_ = true;
    pthread_mutex_unlock(&pokeMtx_);
    logLine(std::string("start: ") + syncDemoName(kind) + ", " + std::to_string(nThreads) + " threads");

    // One pthread per worker.
    std::vector<Worker> workers(nThreads);
    std::vector<pthread_t> tids(nThreads);
    int created = 0;
    running_ = nThreads;
    for (int i = 0; i < nThreads; ++i) {
        workers[i] = Worker{this, i};
        if (pthread_create(&tids[i], nullptr, workerMain, &workers[i]) != 0) {
            running_ -= nThreads - i;                                 // these never start
            cancel();
            break;
        }
        ++created;
    }
    // This thread is the single dispatcher: forward pipe lines while the workers run.
    while (running_.load() > 0) {
        drain();
        usleep(2000);
    }
    for (int i = 0; i < created; ++i) pthread_join(tids[i], nullptr);
    logLine(cancel_ ? "cancelled: all threads joined" : "finished: all threads joined");
    drain();

    pthread_mutex_lock(&pokeMtx_);
    active_ = false;
    semsReady_ = condReady_ = false;
    pthread_mutex_unlock(&pokeMtx_);
    if (sems) { sem_destroy(&semA_); sem_destroy(&semB_); }
    if (conds) { pthread_cond_destroy(&notFull_); pthread_cond_destroy(&notEmpty_); }
    pthread_mutex_destroy(&mtx_);

    // Results and checks.
    SyncDemoResult r;
    r.kind = kind;
    r.threadsCreated = created;
    r.cancelled = cancel_;
    r.maxConcurrent = maxInCS_;
    r.overflows = overflows_;
    r.underflows = underflows_;
    r.limit = kind == SyncDemoKind::COUNTING_SEM ? p_.resources : 1;
    switch (kind) {
        case SyncDemoKind::RACE:
            r.expected = (long)nThreads * p_.iterations;
            r.actual = raceCounter_;
            r.ok = r.actual <= r.expected;
            r.summary = "expected " + std::to_string(r.expected) + ", got " + std::to_string(r.actual) + ": " +
                        std::to_string(r.expected - r.actual) +
                        " update(s) lost (depends on the OS scheduler; can be 0 on a 1-CPU machine)";
            break;
        case SyncDemoKind::PETERSON: case SyncDemoKind::MUTEX: case SyncDemoKind::BINARY_SEM:
            r.expected = (long)nThreads * p_.iterations;
            r.actual = counter_;
            r.ok = r.actual == r.expected && r.maxConcurrent <= 1;
            r.summary = "expected " + std::to_string(r.expected) + ", got " + std::to_string(r.actual) +
                        ", max threads in the critical section " + std::to_string(r.maxConcurrent);
            break;
        case SyncDemoKind::COUNTING_SEM:
            r.expected = (long)nThreads * p_.iterations;
            r.actual = uses_;
            r.ok = r.actual == r.expected && r.maxConcurrent <= r.limit;
            r.summary = std::to_string(r.actual) + " of " + std::to_string(r.expected) +
                        " uses, at most " + std::to_string(r.maxConcurrent) + " at once (limit S = " +
                        std::to_string(r.limit) + ")";
            break;
        case SyncDemoKind::PRODUCER_CONSUMER: case SyncDemoKind::MONITOR:
            r.expected = (long)p_.producers * p_.items;
            r.actual = consumedCount_;
            r.ok = r.actual == r.expected && producedSum_ == consumedSum_ && r.overflows == 0 &&
                   r.underflows == 0 && r.maxConcurrent <= 1;
            r.summary = std::to_string(r.actual) + " of " + std::to_string(r.expected) + " items consumed, checksum " +
                        (producedSum_ == consumedSum_ ? "equal" : "DIFFERENT") + ", overflows " +
                        std::to_string(r.overflows) + ", underflows " + std::to_string(r.underflows);
            break;
    }
    if (r.cancelled) { r.ok = false; r.summary = "cancelled - " + r.summary; }
    r.millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.log = lines_;
    logger_.reset();
    cancel_ = false;                                                  // ready for the next run
    return r;
}
