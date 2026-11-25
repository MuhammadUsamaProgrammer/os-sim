// engine.cpp - clock thread + one pthread per process, synchronised with a
// mutex, semaphores and a condition variable; logs travel through a pipe.
#include "engine.h"
#include <algorithm>
#include <cerrno>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

// Stores the processes and the precomputed timeline; creates the primitives.
SimulationEngine::SimulationEngine(std::vector<PCB> procs, std::vector<Segment> timeline, EngineOptions opt)
    : procs_(std::move(procs)), timeline_(std::move(timeline)), opt_(opt) {
    for (PCB& p : procs_) {                         // fresh PCBs: every process starts as New
        p.remaining = p.burst; p.state = ProcState::New; p.firstRun = -1; p.finish = -1;
    }
    pthread_mutex_init(&mtx_, nullptr);
    pthread_cond_init(&cond_, nullptr);
    runSem_ = new sem_t[procs_.size() ? procs_.size() : 1];
    for (size_t i = 0; i < procs_.size(); ++i) sem_init(&runSem_[i], 0, 0);
    sem_init(&doneSem_, 0, 0);
}

// Shutdown: stop the clock, join every thread, destroy the primitives.
SimulationEngine::~SimulationEngine() {
    stop();
    join();
    for (size_t i = 0; i < procs_.size(); ++i) sem_destroy(&runSem_[i]);
    delete[] runSem_;
    sem_destroy(&doneSem_);
    pthread_cond_destroy(&cond_);
    pthread_mutex_destroy(&mtx_);
}

// Creates the process threads and the clock thread (pthread_create).
void SimulationEngine::start(bool startPaused) {
    pthread_mutex_lock(&mtx_);
    if (started_) { pthread_mutex_unlock(&mtx_); return; }
    started_ = true;
    paused_ = startPaused;
    pthread_mutex_unlock(&mtx_);

    if (opt_.useRealDemo)
        for (const std::string& l : realProcessDemo(2)) logger_.logWrite("[t=0] " + l);

    procThreads_.resize(procs_.size());
    procArgs_.resize(procs_.size());
    for (size_t i = 0; i < procs_.size(); ++i) {
        procArgs_[i] = {this, (int)i};
        pthread_create(&procThreads_[i], nullptr, &SimulationEngine::procMain, &procArgs_[i]);
    }
    pthread_create(&clockThread_, nullptr, &SimulationEngine::clockMain, this);
}

// Pause: the clock thread will block on the condition variable before the next tick.
void SimulationEngine::pause() {
    pthread_mutex_lock(&mtx_);
    paused_ = true;
    pthread_mutex_unlock(&mtx_);
}

// Resume: clear the flag and wake the clock thread.
void SimulationEngine::resume() {
    pthread_mutex_lock(&mtx_);
    paused_ = false;
    pthread_cond_broadcast(&cond_);
    pthread_mutex_unlock(&mtx_);
}

// Single step: allow exactly one more tick while staying paused.
void SimulationEngine::step() {
    pthread_mutex_lock(&mtx_);
    paused_ = true;
    stepBudget_++;
    pthread_cond_broadcast(&cond_);
    pthread_mutex_unlock(&mtx_);
}

// Stop: the clock loop ends after the current tick; it then releases the process threads.
void SimulationEngine::stop() {
    pthread_mutex_lock(&mtx_);
    stopRequested_ = true;
    pthread_cond_broadcast(&cond_);
    pthread_mutex_unlock(&mtx_);
}

// Waits for every thread (pthread_join). Safe to call more than once.
void SimulationEngine::join() {
    if (!started_ || joined_) return;
    joined_ = true;
    pthread_join(clockThread_, nullptr);
    for (pthread_t& t : procThreads_) pthread_join(t, nullptr);
}

// Changes the delay between ticks (protected by the mutex, read by the clock thread).
void SimulationEngine::setTickDelay(int ms) {
    pthread_mutex_lock(&mtx_);
    opt_.tickDelayMs = ms < 0 ? 0 : ms;
    pthread_cond_broadcast(&cond_);
    pthread_mutex_unlock(&mtx_);
}

EngineSnapshot SimulationEngine::snapshot() const {
    pthread_mutex_lock(&mtx_);
    EngineSnapshot s;
    s.tick = currentTick_; s.finished = finished_; s.paused = paused_;
    for (const PCB& p : procs_) s.procs.push_back({p.pid, p.state, p.remaining});
    pthread_mutex_unlock(&mtx_);
    return s;
}

std::vector<std::string> SimulationEngine::log() const { return logger_.lines(); }

std::vector<PCB> SimulationEngine::pcbs() const {
    pthread_mutex_lock(&mtx_);
    std::vector<PCB> copy = procs_;
    pthread_mutex_unlock(&mtx_);
    return copy;
}

bool SimulationEngine::finished() const {
    pthread_mutex_lock(&mtx_);
    bool f = finished_;
    pthread_mutex_unlock(&mtx_);
    return f;
}

void* SimulationEngine::clockMain(void* self) {
    static_cast<SimulationEngine*>(self)->clockLoop();
    return nullptr;
}

void* SimulationEngine::procMain(void* arg) {
    ProcArg* a = static_cast<ProcArg*>(arg);
    a->eng->procLoop(a->idx);
    return nullptr;
}

// Writes one log line prefixed with the current tick. Caller holds the mutex.
void SimulationEngine::logLocked(const std::string& line) {
    logger_.logWrite("[t=" + std::to_string(currentTick_) + "] " + line);
}

// The single dispatcher: read new lines from the pipe and hand them to the observer.
void SimulationEngine::dispatch() {
    for (const std::string& line : logger_.drainLog())
        if (observer_) observer_(parseLogLine(line));
}

int SimulationEngine::pidAt(int t) const {
    for (const Segment& s : timeline_) if (t >= s.start && t < s.end) return s.pid;
    return -1;
}

// Clock thread: one iteration per tick of the precomputed timeline.
void SimulationEngine::clockLoop() {
    int endTime = timeline_.empty() ? 0 : timeline_.back().end;
    for (int t = 0; t < endTime; ++t) {
        pthread_mutex_lock(&mtx_);
        // 1. Pause support: wait on the condition variable (loop guards spurious wake-ups).
        while (paused_ && stepBudget_ == 0 && !stopRequested_) pthread_cond_wait(&cond_, &mtx_);
        if (stopRequested_) { pthread_mutex_unlock(&mtx_); break; }
        if (paused_ && stepBudget_ > 0) stepBudget_--;

        // 2. Advance the clock and update process states.
        currentTick_ = t;
        logLocked("CLOCK tick");
        for (size_t i = 0; i < procs_.size(); ++i)                // New -> Ready
            if (procs_[i].arrival == t && procs_[i].state == ProcState::New) {
                procs_[i].state = ProcState::Ready;
                readyQueue_.push_back((int)i);
                logLocked("P" + std::to_string(procs_[i].pid) + " READY arrived");
            }
        int pid = pidAt(t), idx = -1;
        for (size_t i = 0; i < procs_.size(); ++i) if (procs_[i].pid == pid) idx = (int)i;
        if (running_ >= 0 && running_ != idx && procs_[running_].state == ProcState::Running) {
            procs_[running_].state = ProcState::Ready;            // preempted: Running -> Ready
            readyQueue_.push_back(running_);
            logLocked("P" + std::to_string(procs_[running_].pid) + " READY preempted remaining=" +
                      std::to_string(procs_[running_].remaining));
        }
        if (idx >= 0) {                                           // dispatch: Ready -> Running
            readyQueue_.erase(std::remove(readyQueue_.begin(), readyQueue_.end(), idx), readyQueue_.end());
            procs_[idx].state = ProcState::Running;
        } else {
            logLocked("CPU idle");
        }
        running_ = idx;
        pthread_mutex_unlock(&mtx_);

        // 3. Handshake with the process thread: let it run one tick, wait until it is done.
        if (idx >= 0) {
            sem_post(&runSem_[idx]);
            while (sem_wait(&doneSem_) != 0 && errno == EINTR) {}
        }
        dispatch();

        // 4. Optional animation delay (a timed wait, so stop()/step() interrupt it).
        pthread_mutex_lock(&mtx_);
        int delay = opt_.tickDelayMs;
        if (delay > 0) {
            timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            long ns = until.tv_nsec + (long)delay * 1000000L;
            until.tv_sec += ns / 1000000000L;
            until.tv_nsec = ns % 1000000000L;
            while (!stopRequested_ && !(paused_ && stepBudget_ > 0))
                if (pthread_cond_timedwait(&cond_, &mtx_, &until) == ETIMEDOUT) break;
        }
        pthread_mutex_unlock(&mtx_);
    }

    // End of the run: tell the process threads to exit and wake each of them.
    pthread_mutex_lock(&mtx_);
    currentTick_ = stopRequested_ ? currentTick_ : endTime;
    logLocked(stopRequested_ ? "CLOCK stopped" : "CLOCK finished");
    finished_ = true;
    exitThreads_ = true;
    pthread_mutex_unlock(&mtx_);
    for (size_t i = 0; i < procs_.size(); ++i) sem_post(&runSem_[i]);
    dispatch();
}

// Process thread: waits for its semaphore, executes one CPU tick, signals the clock.
void SimulationEngine::procLoop(int idx) {
    for (;;) {
        while (sem_wait(&runSem_[idx]) != 0 && errno == EINTR) {}
        pthread_mutex_lock(&mtx_);
        if (exitThreads_) { pthread_mutex_unlock(&mtx_); break; }
        PCB& p = procs_[idx];
        int t = currentTick_;
        p.remaining--;
        if (p.firstRun < 0) p.firstRun = t;
        if (p.remaining == 0) {                                   // Running -> Terminated
            p.state = ProcState::Terminated;
            p.finish = t + 1;
            logLocked("P" + std::to_string(p.pid) + " TERMINATED finish=" + std::to_string(p.finish));
        } else {
            logLocked("P" + std::to_string(p.pid) + " RUNNING remaining=" + std::to_string(p.remaining));
        }
        pthread_mutex_unlock(&mtx_);
        sem_post(&doneSem_);
    }
}

// Parses the fixed log format back into an event.
EngineEvent parseLogLine(const std::string& line) {
    EngineEvent e{0, -1, ProcState::New, line};
    std::istringstream in(line);
    std::string tickTok, who, state;
    in >> tickTok >> who >> state;
    if (tickTok.rfind("[t=", 0) == 0) e.tick = std::atoi(tickTok.c_str() + 3);
    if (who.size() > 1 && who[0] == 'P' && who[1] != '-') {
        e.pid = std::atoi(who.c_str() + 1);
        for (ProcState s : {ProcState::New, ProcState::Ready, ProcState::Running,
                            ProcState::Waiting, ProcState::Terminated})
            if (state == stateName(s)) e.state = s;
    }
    return e;
}

// fork() + execlp() + waitpid(): real processes created by the kernel.
std::vector<std::string> realProcessDemo(int n) {
    std::vector<std::string> out;
    std::vector<pid_t> kids;
    for (int i = 1; i <= n; ++i) {
        pid_t pid = fork();
        if (pid < 0) { out.push_back("REAL fork failed"); continue; }
        if (pid == 0) {                                // child: replace image with `sleep 1`
            execlp("sleep", "sleep", "1", (char*)nullptr);
            _exit(127);                                // exec failed
        }
        kids.push_back(pid);
        out.push_back("REAL fork child " + std::to_string(i) + " pid=" + std::to_string(pid));
    }
    for (pid_t k : kids) {                             // parent: reap every child
        int status = 0;
        waitpid(k, &status, 0);
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        out.push_back("REAL child pid=" + std::to_string(k) + " exited code=" + std::to_string(code));
    }
    return out;
}
