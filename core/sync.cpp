// sync.cpp - deterministic instruction-level simulation of synchronization algorithms.
// Every function here is pure C++ (no threads); one step() = one pseudocode line.
#include "sync.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

const char* syncAlgoName(SyncAlgo a) {
    switch (a) {
        case SyncAlgo::RACE: return "Race condition (no synchronization)";
        case SyncAlgo::PETERSON: return "Peterson's solution";
        case SyncAlgo::MUTEX: return "Mutex lock";
        case SyncAlgo::BINARY_SEM: return "Binary semaphore";
        case SyncAlgo::COUNTING_SEM: return "Counting semaphore";
        case SyncAlgo::PRODUCER_CONSUMER: return "Producer-consumer (semaphores)";
        case SyncAlgo::MONITOR: return "Monitor + condition variables";
        case SyncAlgo::READERS_WRITERS: return "Readers-writers (semaphores)";
        case SyncAlgo::DINING_PHILOSOPHERS: return "Dining philosophers (semaphores)";
    }
    return "?";
}

const char* syncStateName(SyncProcState s) {
    switch (s) {
        case SyncProcState::Ready: return "READY";
        case SyncProcState::Running: return "RUNNING";
        case SyncProcState::Spinning: return "BUSY-WAIT";
        case SyncProcState::Blocked: return "BLOCKED";
        case SyncProcState::Done: return "DONE";
    }
    return "?";
}

const char* objKindName(ObjKind k) {
    switch (k) {
        case ObjKind::Mutex: return "mutex";
        case ObjKind::Semaphore: return "semaphore";
        case ObjKind::Condition: return "condition";
        case ObjKind::Monitor: return "monitor";
    }
    return "?";
}

// Checks the configuration (OS concept: validate before creating processes).
void validateSyncConfig(const SyncConfig& c) {
    auto need = [](bool ok, const std::string& msg) { if (!ok) throw std::invalid_argument(msg); };
    need(c.iterations >= 1 && c.iterations <= 50, "iterations must be between 1 and 50");
    need(c.slice >= 1 && c.slice <= 20, "instructions per turn must be between 1 and 20");
    bool buffer = c.algo == SyncAlgo::PRODUCER_CONSUMER || c.algo == SyncAlgo::MONITOR;
    if (buffer) {
        need(c.producers >= 1 && c.producers <= 6, "producers must be between 1 and 6");
        need(c.consumers >= 1 && c.consumers <= 6, "consumers must be between 1 and 6");
        need(c.capacity >= 1 && c.capacity <= 12, "buffer capacity must be between 1 and 12");
        need(c.producerSpeed >= 1 && c.producerSpeed <= 20 && c.consumerSpeed >= 1 && c.consumerSpeed <= 20,
             "producer / consumer speed must be between 1 and 20 instructions per turn");
        need(c.producers * c.iterations >= c.consumers,
             "every consumer needs at least one item: producers x items must be >= consumers");
    } else if (c.algo == SyncAlgo::PETERSON) {
        need(c.processes == 2, "Peterson's solution works for exactly 2 processes");
    } else if (c.algo == SyncAlgo::READERS_WRITERS) {
        need(c.readers >= 1 && c.readers <= 6 && c.writers >= 1 && c.writers <= 4 && c.readers + c.writers <= 8,
             "readers 1..6, writers 1..4, at most 8 processes");
    } else if (c.algo == SyncAlgo::DINING_PHILOSOPHERS) {
        need(c.philosophers >= 2 && c.philosophers <= 6, "philosophers must be between 2 and 6");
    } else {
        need(c.processes >= 1 && c.processes <= 8, "processes must be between 1 and 8");
    }
    if (c.algo == SyncAlgo::COUNTING_SEM)
        need(c.resources >= 1 && c.resources <= 8, "resource instances must be between 1 and 8");
    int n = buffer ? c.producers + c.consumers : c.algo == SyncAlgo::READERS_WRITERS ? c.readers + c.writers
                   : c.algo == SyncAlgo::DINING_PHILOSOPHERS ? c.philosophers : c.processes;
    for (int s : c.script) need(s >= 0 && s < n, "scenario script names a process that does not exist");
}

int SyncSim::addObj(const std::string& name, ObjKind kind, int value) {
    SyncObject o;
    o.name = name;
    o.kind = kind;
    o.value = value;
    objs_.push_back(o);
    return (int)objs_.size() - 1;
}

// Builds the processes, their pseudocode programs and the synchronization objects.
SyncSim::SyncSim(const SyncConfig& cfg) : cfg_(cfg), rng_(cfg.seed) {
    validateSyncConfig(cfg);
    // The read-modify-write body that every counter algorithm protects.
    auto body = [](std::vector<Instr>& p, const std::string& indent, bool enterOnRead, bool leaveOnWrite) {
        p.push_back({Op::Read, -1, 0, indent + "reg = counter;        // READ", enterOnRead, false});
        p.push_back({Op::Inc, -1, 0, indent + "reg = reg + 1;        // MODIFY", false, false});
        p.push_back({Op::Write, -1, 0, indent + "counter = reg;        // WRITE", false, leaveOnWrite});
    };
    std::vector<Instr> prog;
    switch (cfg.algo) {
        case SyncAlgo::RACE:
            body(prog, "", true, true);
            titles_.push_back("Every process: counter++ split into 3 machine steps (no lock)");
            break;
        case SyncAlgo::PETERSON:
            prog.push_back({Op::Nop, -1, 0, "int other = 1 - process;"});
            prog.push_back({Op::SetInterested, -1, 1, "interested[process] = true;"});
            prog.push_back({Op::SetTurn, -1, 0, "turn = process;"});
            prog.push_back({Op::PetersonWait, -1, 0, "while (interested[other] == true && turn == process);", true, false});
            body(prog, "    ", false, false);
            prog.push_back({Op::SetInterested, -1, 0, "interested[process] = false;", false, true});
            titles_.push_back("Process i (i = 0 or 1) - Peterson's solution");
            break;
        case SyncAlgo::MUTEX: {
            int m = addObj("lock", ObjKind::Mutex, 0);
            prog.push_back({Op::Lock, m, 0, "acquire(lock);       // pthread_mutex_lock", true, false});
            body(prog, "    ", false, false);
            prog.push_back({Op::Unlock, m, 0, "release(lock);       // pthread_mutex_unlock", false, true});
            titles_.push_back("Every process - mutex lock");
            break;
        }
        case SyncAlgo::BINARY_SEM: {
            int s = addObj("S", ObjKind::Semaphore, 1);
            prog.push_back({Op::SemWait, s, 0, "wait(S);             // sem_wait: S 1 -> 0", true, false});
            body(prog, "    ", false, false);
            prog.push_back({Op::SemSignal, s, 0, "signal(S);           // sem_post", false, true});
            titles_.push_back("Every process - binary semaphore S = 1");
            break;
        }
        case SyncAlgo::COUNTING_SEM: {
            int s = addObj("S", ObjKind::Semaphore, cfg.resources);
            prog.push_back({Op::SemWait, s, 0, "wait(S);             // take a permit (S--)"});
            prog.push_back({Op::UseResource, -1, 0, "    use one resource instance;   // in_use++", true, false});
            prog.push_back({Op::ReleaseResource, -1, 0, "    finish with the instance;    // in_use--", false, true});
            prog.push_back({Op::SemSignal, s, 0, "signal(S);           // return the permit (S++)"});
            titles_.push_back("Every process - counting semaphore S = " + std::to_string(cfg.resources));
            break;
        }
        case SyncAlgo::PRODUCER_CONSUMER: {
            int empty = addObj("empty", ObjKind::Semaphore, cfg.capacity);
            int full = addObj("full", ObjKind::Semaphore, 0);
            int mutex = addObj("mutex", ObjKind::Semaphore, 1);
            std::vector<Instr> prod, cons;
            prod.push_back({Op::Produce, -1, 0, "item = produce_item();"});
            prod.push_back({Op::SemWait, empty, 0, "wait(empty);         // a free slot?"});
            prod.push_back({Op::SemWait, mutex, 0, "wait(mutex);", true, false});
            prod.push_back({Op::Insert, -1, 0, "    buffer[in] = item; in = (in + 1) % N;"});
            prod.push_back({Op::SemSignal, mutex, 0, "signal(mutex);", false, true});
            prod.push_back({Op::SemSignal, full, 0, "signal(full);        // one more item"});
            cons.push_back({Op::SemWait, full, 0, "wait(full);          // an item?"});
            cons.push_back({Op::SemWait, mutex, 0, "wait(mutex);", true, false});
            cons.push_back({Op::Remove, -1, 0, "    item = buffer[out]; out = (out + 1) % N;"});
            cons.push_back({Op::SemSignal, mutex, 0, "signal(mutex);", false, true});
            cons.push_back({Op::SemSignal, empty, 0, "signal(empty);       // one more free slot"});
            cons.push_back({Op::Consume, -1, 0, "consume_item(item);"});
            programs_ = {prod, cons};
            titles_ = {"Producer", "Consumer"};
            break;
        }
        case SyncAlgo::MONITOR: {
            monitor_ = addObj("monitor", ObjKind::Monitor, 0);
            int notFull = addObj("notFull", ObjKind::Condition, 0);
            int notEmpty = addObj("notEmpty", ObjKind::Condition, 0);
            std::vector<Instr> prod, cons;
            prod.push_back({Op::Produce, -1, 0, "item = produce_item();"});
            prod.push_back({Op::MonEnter, monitor_, 0, "buffer.insert(item) {    // enter monitor (lock)", true, false});
            prod.push_back({Op::CondWhile, -1, 0, "    while (count == N)"});
            prod.push_back({Op::CondWait, notFull, 0, "        notFull.wait();   // unlock + sleep"});
            prod.push_back({Op::Insert, -1, 0, "    buffer[in] = item; in = (in + 1) % N; count++;"});
            prod.push_back({Op::CondSignal, notEmpty, 0, "    notEmpty.signal();"});
            prod.push_back({Op::MonExit, monitor_, 0, "}                        // leave monitor (unlock)", false, true});
            cons.push_back({Op::MonEnter, monitor_, 0, "buffer.remove() {        // enter monitor (lock)", true, false});
            cons.push_back({Op::CondWhile, -1, 1, "    while (count == 0)"});
            cons.push_back({Op::CondWait, notEmpty, 0, "        notEmpty.wait();  // unlock + sleep"});
            cons.push_back({Op::Remove, -1, 0, "    item = buffer[out]; out = (out + 1) % N; count--;"});
            cons.push_back({Op::CondSignal, notFull, 0, "    notFull.signal();"});
            cons.push_back({Op::MonExit, monitor_, 0, "}                        // leave monitor (unlock)", false, true});
            cons.push_back({Op::Consume, -1, 0, "consume_item(item);"});
            programs_ = {prod, cons};
            titles_ = {"Producer (calls the monitor)", "Consumer (calls the monitor)"};
            break;
        }
        case SyncAlgo::READERS_WRITERS: {
            int rw = addObj("rw_mutex", ObjKind::Semaphore, 1);
            int mx = addObj("mutex", ObjKind::Semaphore, 1);
            std::vector<Instr> rd, wr;
            rd.push_back({Op::SemWait, mx, 0, "wait(mutex);"});
            rd.push_back({Op::RcInc, -1, 0, "    read_count++;"});
            rd.push_back({Op::RcFirstWait, rw, 0, "    if (read_count == 1) wait(rw_mutex);   // first reader locks out writers"});
            rd.push_back({Op::SemSignal, mx, 0, "signal(mutex);"});
            rd.push_back({Op::Nop, -1, 0, "read the shared data ...;               // many readers at once", true, false});
            rd.push_back({Op::DoneRead, -1, 0, "... done reading;", false, true});
            rd.push_back({Op::SemWait, mx, 0, "wait(mutex);"});
            rd.push_back({Op::RcDec, -1, 0, "    read_count--;"});
            rd.push_back({Op::RcLastSignal, rw, 0, "    if (read_count == 0) signal(rw_mutex); // last reader lets writers in"});
            rd.push_back({Op::SemSignal, mx, 0, "signal(mutex);"});
            wr.push_back({Op::SemWait, rw, 0, "wait(rw_mutex);", true, false});
            wr.push_back({Op::WriteData, -1, 0, "    write the shared data;             // exclusive"});
            wr.push_back({Op::SemSignal, rw, 0, "signal(rw_mutex);", false, true});
            programs_ = {rd, wr};
            titles_ = {"Reader", "Writer"};
            break;
        }
        case SyncAlgo::DINING_PHILOSOPHERS: {
            int n = cfg.philosophers;
            std::vector<int> chop;
            for (int i = 0; i < n; ++i) chop.push_back(addObj("chopstick[" + std::to_string(i) + "]", ObjKind::Semaphore, 1));
            for (int i = 0; i < n; ++i) {
                int L = i, R = (i + 1) % n;
                bool swap = cfg.asymmetric && i % 2 == 1;            // odd philosophers: right first
                int a = swap ? R : L, b = swap ? L : R;
                std::vector<Instr> ph;
                ph.push_back({Op::Nop, -1, 0, "think();"});
                ph.push_back({Op::SemWait, chop[a], 0, "wait(chopstick[" + std::to_string(a) + "]);   // " + (swap ? "right" : "left") + " first"});
                ph.push_back({Op::SemWait, chop[b], 0, "wait(chopstick[" + std::to_string(b) + "]);", true, false});
                ph.push_back({Op::Eat, -1, 0, "    eat();"});
                ph.push_back({Op::SemSignal, chop[a], 0, "signal(chopstick[" + std::to_string(a) + "]);", false, true});
                ph.push_back({Op::SemSignal, chop[b], 0, "signal(chopstick[" + std::to_string(b) + "]);"});
                programs_.push_back(ph);
                titles_.push_back("Philosopher " + std::to_string(i) + " (chopsticks " + std::to_string(L) + " and " + std::to_string(R) + ")");
            }
            break;
        }
    }
    if (programs_.empty()) programs_.push_back(prog);

    // Processes: workers P0..Pn-1, or producers then consumers.
    bool buffer = cfg.algo == SyncAlgo::PRODUCER_CONSUMER || cfg.algo == SyncAlgo::MONITOR;
    if (buffer) {
        int total = cfg.producers * cfg.iterations;
        for (int i = 0; i < cfg.producers; ++i) {
            SyncProc p;
            p.id = (int)procs_.size(); p.name = "Prod" + std::to_string(i); p.program = 0;
            p.iterations = cfg.iterations;
            procs_.push_back(p);
        }
        for (int i = 0; i < cfg.consumers; ++i) {
            SyncProc p;
            p.id = (int)procs_.size(); p.name = "Cons" + std::to_string(i); p.program = 1;
            p.iterations = total / cfg.consumers + (i < total % cfg.consumers ? 1 : 0);
            procs_.push_back(p);
        }
        buffer_.assign(cfg.capacity, -1);
    } else if (cfg.algo == SyncAlgo::READERS_WRITERS) {
        for (int i = 0; i < cfg.readers + cfg.writers; ++i) {
            SyncProc p;
            bool w = i >= cfg.readers;
            p.id = i; p.name = w ? "Writer" + std::to_string(i - cfg.readers) : "Reader" + std::to_string(i);
            p.program = w ? 1 : 0; p.iterations = cfg.iterations;
            procs_.push_back(p);
        }
    } else if (cfg.algo == SyncAlgo::DINING_PHILOSOPHERS) {
        for (int i = 0; i < cfg.philosophers; ++i) {
            SyncProc p;
            p.id = i; p.name = "Phil" + std::to_string(i); p.program = i; p.iterations = cfg.iterations;
            procs_.push_back(p);
        }
    } else {
        for (int i = 0; i < cfg.processes; ++i) {
            SyncProc p;
            p.id = i; p.name = "P" + std::to_string(i); p.program = 0; p.iterations = cfg.iterations;
            procs_.push_back(p);
        }
    }
    log_.push_back("[s=0] start: " + std::string(syncAlgoName(cfg.algo)) + ", " +
                   std::to_string(procs_.size()) + " processes");
}

bool SyncSim::canRun(int pid) const {
    if (pid < 0 || pid >= (int)procs_.size()) return false;
    SyncProcState s = procs_[pid].state;
    return s != SyncProcState::Blocked && s != SyncProcState::Done;
}

std::vector<int> SyncSim::runnable() const {
    std::vector<int> r;
    for (int i = 0; i < (int)procs_.size(); ++i) if (canRun(i)) r.push_back(i);
    return r;
}

bool SyncSim::finished() const { return runnable().empty(); }

int SyncSim::inCSCount() const {
    int n = 0;
    for (const SyncProc& p : procs_) n += p.inCS ? 1 : 0;
    return n;
}

// Entering the critical section: count it, and record a violation if it was already full.
int SyncSim::csCapacity() const {
    switch (cfg_.algo) {
        case SyncAlgo::COUNTING_SEM: return cfg_.resources;
        case SyncAlgo::READERS_WRITERS: return cfg_.readers;          // readers share; a writer needs it alone
        case SyncAlgo::DINING_PHILOSOPHERS: return cfg_.philosophers / 2;
        default: return 1;
    }
}

void SyncSim::enterCS(int p) {
    if (procs_[p].inCS) return;
    bool violation;
    if (cfg_.algo == SyncAlgo::READERS_WRITERS) {
        bool writer = procs_[p].program == 1, writerInside = false;
        for (const SyncProc& q : procs_) if (q.inCS && q.program == 1) writerInside = true;
        violation = writerInside || (writer && inCSCount() > 0);
    } else if (cfg_.algo == SyncAlgo::DINING_PHILOSOPHERS) {
        int n = (int)procs_.size();
        violation = procs_[(p + 1) % n].inCS || procs_[(p + n - 1) % n].inCS;      // a neighbour eats with a shared chopstick
    } else violation = inCSCount() >= csCapacity();
    if (violation) {
        ++csViolations_;
        log_.push_back("[s=" + std::to_string(steps_) + "] VIOLATION: " + procs_[p].name +
                       " enters the critical section while it is already full");
    }
    procs_[p].inCS = true;
    procs_[p].csEntries++;
    maxInCS_ = std::max(maxInCS_, inCSCount());
}

void SyncSim::leaveCS(int p) { procs_[p].inCS = false; }

// Puts p to sleep in the FIFO waiting queue of an object (no CPU used while blocked).
void SyncSim::block(int p, int obj) {
    objs_[obj].queue.push_back(p);
    procs_[p].state = SyncProcState::Blocked;
    procs_[p].waitingOn = obj;
}

// Wakes a blocked process: its blocking line has now completed.
void SyncSim::wake(int p) {
    SyncProc& P = procs_[p];
    P.state = SyncProcState::Ready;
    P.waitingOn = -1;
    if (P.resumePc >= 0) {                 // monitor: back from a condition wait,
        P.pc = P.resumePc;                 // re-check the while condition (Mesa semantics)
        P.resumePc = -1;
        enterCS(p);                        // it owns the monitor again
        return;
    }
    if (programs_[P.program][P.pc].enterCS) enterCS(p);
    finishLine(p);
}

// Gives a free mutex / monitor to p.
void SyncSim::grant(int obj, int p) {
    objs_[obj].owner = p;
}

// Frees a mutex / monitor held by p. FIFO hand-off: the first waiter becomes the owner.
// (Simulation policy - real POSIX mutexes do not promise FIFO order.)
void SyncSim::release(int obj, int p) {
    SyncObject& o = objs_[obj];
    if (o.owner != p) {
        log_.push_back("[s=" + std::to_string(steps_) + "] ERROR: " + procs_[p].name + " releases " + o.name +
                       " that it does not own");
        return;
    }
    o.owner = -1;
    if (!o.queue.empty()) {
        int q = o.queue.front();
        o.queue.pop_front();
        grant(obj, q);
        wake(q);
    }
}

// Moves p to its next line; at the end of the program it loops or finishes.
void SyncSim::finishLine(int p) {
    SyncProc& P = procs_[p];
    P.pc++;
    if (P.pc >= (int)programs_[P.program].size()) {
        P.itersDone++;
        if (P.itersDone < P.iterations) P.pc = 0;
        else { P.state = SyncProcState::Done; P.pc = (int)programs_[P.program].size(); }
    }
}

static std::string nameList(const std::vector<SyncProc>& procs, const std::deque<int>& q) {
    std::string s;
    for (int p : q) s += (s.empty() ? "" : ", ") + procs[p].name;
    return s.empty() ? "-" : s;
}

// Executes exactly one pseudocode line of process pid (OS concept: one atomic machine step).
bool SyncSim::step(int pid) {
    if (!canRun(pid)) return false;
    SyncProc& P = procs_[pid];
    const int prog = P.program, line = P.pc;
    const Instr in = programs_[prog][line];
    ++steps_;
    P.steps++;
    P.lastLine = line;
    bool blocked = false, spinning = false;
    int next = line + 1;
    std::string what;
    if (in.leaveCS) leaveCS(pid);          // leave before a hand-off lets the next process in

    switch (in.op) {
        case Op::Nop: what = "local statement"; break;
        case Op::Read: P.reg = counter_; what = "reg = " + std::to_string(P.reg); break;
        case Op::Inc: P.reg += 1; what = "reg = " + std::to_string(P.reg); break;
        case Op::Write:
            counter_ = P.reg;
            ++writes_;
            what = "counter = " + std::to_string(counter_);
            if (counter_ < writes_) what += "  (" + std::to_string(writes_ - counter_) + " update(s) lost so far)";
            break;
        case Op::SetInterested:
            interested_[pid & 1] = in.arg != 0;
            what = "interested[" + std::to_string(pid) + "] = " + (in.arg ? "true" : "false");
            break;
        case Op::SetTurn: turn_ = pid; what = "turn = " + std::to_string(pid); break;
        case Op::PetersonWait: {
            int other = 1 - pid;
            if (interested_[other] && turn_ == pid) {
                spinning = true;
                what = "interested[" + std::to_string(other) + "] == true && turn == " + std::to_string(pid) +
                       " -> keep spinning (busy wait)";
            } else {
                what = interested_[other] ? "turn != " + std::to_string(pid) + " -> enter critical section"
                                          : "interested[" + std::to_string(other) + "] == false -> enter critical section";
            }
            break;
        }
        case Op::Lock:
        case Op::MonEnter: {
            SyncObject& o = objs_[in.obj];
            if (o.owner < 0) { grant(in.obj, pid); what = o.name + " acquired"; }
            else {
                what = o.name + " is held by " + procs_[o.owner].name + " -> blocked in its queue";
                block(pid, in.obj);
                blocked = true;
            }
            break;
        }
        case Op::Unlock:
        case Op::MonExit: {
            SyncObject& o = objs_[in.obj];
            int first = o.queue.empty() ? -1 : o.queue.front();
            release(in.obj, pid);
            what = o.name + " released" + (first >= 0 ? " -> handed to " + procs_[first].name : "");
            break;
        }
        case Op::RcFirstWait:
            if (readCount_ != 1) { what = "read_count = " + std::to_string(readCount_) + " != 1: another reader already holds rw_mutex"; break; }
            [[fallthrough]];
        case Op::SemWait: {
            SyncObject& s = objs_[in.obj];
            if (s.value > 0) {
                s.value--;
                what = s.name + " = " + std::to_string(s.value + 1) + " -> " + std::to_string(s.value) + ", continue";
            } else {
                what = s.name + " = 0 -> blocked in the queue of " + s.name;
                block(pid, in.obj);
                blocked = true;
            }
            break;
        }
        case Op::RcLastSignal:
            if (readCount_ != 0) { what = "read_count = " + std::to_string(readCount_) + " != 0: readers still inside, rw_mutex stays taken"; break; }
            [[fallthrough]];
        case Op::SemSignal: {
            SyncObject& s = objs_[in.obj];
            if (!s.queue.empty()) {
                int q = s.queue.front();
                s.queue.pop_front();
                what = "signal(" + s.name + ") wakes " + procs_[q].name + " (" + s.name + " stays " +
                       std::to_string(s.value) + ")";
                wake(q);
            } else {
                s.value++;
                what = s.name + " = " + std::to_string(s.value - 1) + " -> " + std::to_string(s.value);
            }
            break;
        }
        case Op::RcInc: readCount_++; what = "read_count = " + std::to_string(readCount_); break;
        case Op::RcDec: readCount_--; what = "read_count = " + std::to_string(readCount_); break;
        case Op::DoneRead: reads_++; what = "read finished (" + std::to_string(reads_) + " reads)"; break;
        case Op::WriteData: rwWrites_++; what = "shared data written (" + std::to_string(rwWrites_) + " writes)"; break;
        case Op::Eat: meals_++; what = P.name + " eats (meal " + std::to_string(meals_) + ")"; break;
        case Op::UseResource:
            inUse_++;
            what = "using an instance, in_use = " + std::to_string(inUse_);
            break;
        case Op::ReleaseResource:
            inUse_--;
            uses_++;
            what = "instance returned, in_use = " + std::to_string(inUse_);
            break;
        case Op::Produce:
            P.item = nextItem_++;
            producedItems_.push_back(P.item);
            what = "produced item " + std::to_string(P.item);
            break;
        case Op::Insert:
            if (count_ >= cfg_.capacity) { ++overflows_; what = "OVERFLOW: buffer is full"; break; }
            buffer_[in_] = P.item;
            what = "buffer[" + std::to_string(in_) + "] = " + std::to_string(P.item);
            in_ = (in_ + 1) % cfg_.capacity;
            count_++;
            what += ", count = " + std::to_string(count_);
            break;
        case Op::Remove:
            if (count_ <= 0) { ++underflows_; what = "UNDERFLOW: buffer is empty"; break; }
            P.item = buffer_[out_];
            buffer_[out_] = -1;
            what = "item = buffer[" + std::to_string(out_) + "] = " + std::to_string(P.item);
            out_ = (out_ + 1) % cfg_.capacity;
            count_--;
            consumedItems_.push_back(P.item);
            what += ", count = " + std::to_string(count_);
            break;
        case Op::Consume: what = "consumed item " + std::to_string(P.item); break;
        case Op::CondWhile: {
            bool cond = in.arg == 0 ? count_ == cfg_.capacity : count_ == 0;
            what = std::string(in.arg == 0 ? "count == N" : "count == 0") + " is " + (cond ? "true -> wait" : "false -> go on");
            if (!cond) next = line + 2;    // skip the wait() line
            break;
        }
        case Op::CondWait: {
            leaveCS(pid);
            P.resumePc = line - 1;          // after waking: re-check the while condition
            block(pid, in.obj);
            blocked = true;
            what = "sleeps on " + objs_[in.obj].name + " and releases the monitor";
            release(monitor_, pid);
            break;
        }
        case Op::CondSignal: {
            SyncObject& c = objs_[in.obj];
            if (c.queue.empty()) { what = c.name + ".signal(): no waiter, nothing happens"; break; }
            int q = c.queue.front();
            c.queue.pop_front();
            objs_[monitor_].queue.push_back(q);   // Mesa: the waiter must re-enter the monitor
            procs_[q].waitingOn = monitor_;
            what = c.name + ".signal(): " + procs_[q].name + " moves to the monitor entry queue";
            break;
        }
    }

    if (spinning) {
        P.state = SyncProcState::Spinning;
        P.busyWaitSteps++;
    } else if (!blocked) {
        if (in.enterCS) enterCS(pid);
        P.pc = next - 1;                    // finishLine() adds the 1 back
        finishLine(pid);
    }
    // States after the step: the executed process is Running (unless it waits or finished).
    for (SyncProc& q : procs_) {
        if (q.id != pid && q.state == SyncProcState::Running) q.state = SyncProcState::Ready;
        if (q.state == SyncProcState::Blocked) q.blockedSteps++;
    }
    if (P.state == SyncProcState::Ready) P.state = SyncProcState::Running;

    SyncStepRecord rec;
    rec.step = steps_;
    rec.pid = pid;
    rec.program = prog;
    rec.line = line;
    for (const SyncProc& q : procs_) {
        SyncCell c = SyncCell::Idle;
        if (q.id == pid) c = spinning ? SyncCell::Spin : blocked ? SyncCell::Blocked
                         : (q.inCS || in.leaveCS || in.enterCS) ? SyncCell::RunCS : SyncCell::Run;
        else if (q.state == SyncProcState::Blocked) c = SyncCell::Blocked;
        else if (q.state == SyncProcState::Done) c = SyncCell::Done;
        rec.cells.push_back(c);
    }
    std::string code = in.code;
    code.erase(0, code.find_first_not_of(' '));
    rec.text = "[s=" + std::to_string(steps_) + "] " + P.name + " line " + std::to_string(line + 1) + ": " + code +
               "  -> " + what;
    log_.push_back(rec.text);
    history_.push_back(rec);
    if (finished()) {
        SyncStats st = stats();
        log_.push_back("[s=" + std::to_string(steps_) + "] " +
                       (st.completed ? "all processes finished" : "STUCK: no process can run"));
    }
    return true;
}

// Chooses the next process: scenario script first, then round robin or seeded random.
int SyncSim::pickNext() {
    std::vector<int> run = runnable();
    if (run.empty()) return -1;
    while (scriptPos_ < cfg_.script.size()) {
        int s = cfg_.script[scriptPos_++];
        if (canRun(s)) { current_ = s; usedSlice_ = 1; return s; }
    }
    if (cfg_.randomOrder) {
        std::uniform_int_distribution<int> d(0, (int)run.size() - 1);
        current_ = run[d(rng_)];
        return current_;
    }
    auto sliceOf = [&](int p) {
        bool buffer = cfg_.algo == SyncAlgo::PRODUCER_CONSUMER || cfg_.algo == SyncAlgo::MONITOR;
        if (!buffer) return cfg_.slice;
        return procs_[p].program == 0 ? cfg_.producerSpeed : cfg_.consumerSpeed;
    };
    if (current_ >= 0 && canRun(current_) && usedSlice_ < sliceOf(current_)) {
        usedSlice_++;
        return current_;
    }
    int n = (int)procs_.size();
    int start = current_ < 0 ? 0 : current_ + 1;
    for (int k = 0; k < n; ++k) {
        int c = (start + k) % n;
        if (canRun(c)) { current_ = c; usedSlice_ = 1; return c; }
    }
    return -1;
}

bool SyncSim::stepAuto() {
    int p = pickNext();
    return p >= 0 && step(p);
}

int SyncSim::runToEnd(int maxSteps) {
    int n = 0;
    while (!finished() && n < maxSteps && stepAuto()) ++n;
    return n;
}

SyncStats SyncSim::stats() const {
    SyncStats s;
    s.steps = steps_;
    s.csViolations = csViolations_;
    s.maxInCS = maxInCS_;
    s.overflows = overflows_;
    s.underflows = underflows_;
    s.produced = (int)producedItems_.size();
    s.consumed = (int)consumedItems_.size();
    bool allDone = true;
    for (const SyncProc& p : procs_) {
        s.busyWaitSteps += p.busyWaitSteps;
        s.blockedSteps += p.blockedSteps;
        s.csEntries += p.csEntries;
        allDone = allDone && p.state == SyncProcState::Done;
    }
    s.completed = allDone;
    s.stuck = !allDone && runnable().empty();
    switch (cfg_.algo) {
        case SyncAlgo::RACE: case SyncAlgo::PETERSON: case SyncAlgo::MUTEX: case SyncAlgo::BINARY_SEM:
            s.expected = cfg_.processes * cfg_.iterations;
            s.actual = counter_;
            s.lostUpdates = writes_ - counter_;
            break;
        case SyncAlgo::COUNTING_SEM:
            s.expected = cfg_.processes * cfg_.iterations;
            s.actual = uses_;
            break;
        case SyncAlgo::PRODUCER_CONSUMER: case SyncAlgo::MONITOR:
            s.expected = cfg_.producers * cfg_.iterations;
            s.actual = s.consumed;
            s.lostUpdates = allDone ? s.expected - s.consumed : 0;
            break;
        case SyncAlgo::READERS_WRITERS:
            s.expected = (cfg_.readers + cfg_.writers) * cfg_.iterations;
            s.actual = reads_ + rwWrites_;
            break;
        case SyncAlgo::DINING_PHILOSOPHERS:
            s.expected = cfg_.philosophers * cfg_.iterations;
            s.actual = meals_;
            break;
    }
    return s;
}

// Live view of the shared memory and of every synchronization object.
std::vector<std::pair<std::string, std::string>> SyncSim::variables() const {
    std::vector<std::pair<std::string, std::string>> v;
    auto procName = [&](int p) { return p < 0 ? std::string("free") : "held by " + procs_[p].name; };
    switch (cfg_.algo) {
        case SyncAlgo::RACE: case SyncAlgo::PETERSON: case SyncAlgo::MUTEX: case SyncAlgo::BINARY_SEM:
            v.push_back({"counter", std::to_string(counter_)});
            v.push_back({"increments written", std::to_string(writes_)});
            if (cfg_.algo == SyncAlgo::PETERSON) {
                v.push_back({"interested[0]", interested_[0] ? "true" : "false"});
                v.push_back({"interested[1]", interested_[1] ? "true" : "false"});
                v.push_back({"turn", std::to_string(turn_)});
            }
            for (const SyncProc& p : procs_) v.push_back({p.name + ".reg (private)", std::to_string(p.reg)});
            break;
        case SyncAlgo::COUNTING_SEM:
            v.push_back({"in_use", std::to_string(inUse_) + " of " + std::to_string(cfg_.resources)});
            v.push_back({"uses completed", std::to_string(uses_)});
            break;
        case SyncAlgo::PRODUCER_CONSUMER: case SyncAlgo::MONITOR: {
            std::string b = "[";
            for (size_t i = 0; i < buffer_.size(); ++i)
                b += (i ? " " : "") + (buffer_[i] < 0 ? std::string("_") : std::to_string(buffer_[i]));
            v.push_back({"buffer", b + "]"});
            v.push_back({"count", std::to_string(count_) + " of " + std::to_string(cfg_.capacity)});
            v.push_back({"in", std::to_string(in_)});
            v.push_back({"out", std::to_string(out_)});
            v.push_back({"produced / consumed", std::to_string(producedItems_.size()) + " / " +
                                                    std::to_string(consumedItems_.size())});
            break;
        }
        case SyncAlgo::READERS_WRITERS: {
            int rIn = 0, wIn = 0;
            for (const SyncProc& p : procs_) if (p.inCS) (p.program == 1 ? wIn : rIn)++;
            v.push_back({"read_count", std::to_string(readCount_)});
            v.push_back({"readers reading now", std::to_string(rIn)});
            v.push_back({"writers writing now", std::to_string(wIn)});
            v.push_back({"reads / writes done", std::to_string(reads_) + " / " + std::to_string(rwWrites_)});
            break;
        }
        case SyncAlgo::DINING_PHILOSOPHERS: {
            std::string eating;
            for (const SyncProc& p : procs_) if (p.inCS) eating += (eating.empty() ? "" : ", ") + p.name;
            v.push_back({"eating now", eating.empty() ? "nobody" : eating});
            v.push_back({"meals eaten", std::to_string(meals_)});
            for (const SyncProc& p : procs_) v.push_back({p.name + " meals", std::to_string(p.itersDone)});
            break;
        }
    }
    for (const SyncObject& o : objs_) {
        if (o.kind == ObjKind::Semaphore) v.push_back({o.name + " (semaphore)", std::to_string(o.value)});
        else if (o.kind != ObjKind::Condition) v.push_back({o.name + " (" + objKindName(o.kind) + ")", procName(o.owner)});
        std::string qn = o.kind == ObjKind::Monitor ? o.name + " entry queue" : o.name + " queue";
        v.push_back({qn, nameList(procs_, o.queue)});
    }
    v.push_back({"in critical section", std::to_string(inCSCount()) + " (capacity " + std::to_string(csCapacity()) + ")"});
    return v;
}

// Compact text of the whole state; two equal keys = identical simulations from here on.
std::string SyncSim::stateKey() const {
    std::ostringstream k;
    for (const SyncProc& p : procs_)
        k << p.pc << ',' << p.itersDone << ',' << p.reg << ',' << p.item << ',' << (int)p.state << ','
          << p.inCS << ',' << p.resumePc << ';';
    k << '|' << counter_ << ',' << writes_ << ',' << interested_[0] << interested_[1] << ',' << turn_ << ','
      << in_ << ',' << out_ << ',' << count_ << ',' << inUse_ << ',' << nextItem_ << ',' << readCount_ << ',' << reads_ << ','
      << rwWrites_ << ',' << meals_ << '|';
    for (const SyncObject& o : objs_) {
        k << o.value << ',' << o.owner << ':';
        for (int q : o.queue) k << q << ' ';
        k << ';';
    }
    for (int b : buffer_) k << b << ' ';
    return k.str();
}

// ---------------------------------------------------------------- scenarios

std::vector<SyncScenario> syncScenarios(SyncAlgo a) {
    std::vector<SyncScenario> s;
    SyncConfig c;
    c.algo = a;
    switch (a) {
        case SyncAlgo::RACE: {
            SyncConfig lost = c;
            lost.processes = 2; lost.iterations = 1;
            lost.script = {0, 1, 0, 0, 1, 1};
            s.push_back({"Lost update (textbook interleaving)",
                         "P0 reads 0, P1 reads 0, both write 1: two increments, counter = 1. One update is lost.", lost});
            SyncConfig serial = lost;
            serial.script = {0, 0, 0, 1, 1, 1};
            s.push_back({"Serial order (no overlap)",
                         "P0 finishes read-modify-write before P1 starts: counter = 2, nothing is lost.", serial});
            SyncConfig rr = c;
            rr.processes = 3; rr.iterations = 3; rr.slice = 1;
            s.push_back({"3 processes, round robin, 1 instruction per turn",
                         "Fine-grained switching: most increments overlap and are lost.", rr});
            SyncConfig rnd = c;
            rnd.processes = 3; rnd.iterations = 4; rnd.randomOrder = true; rnd.seed = 7;
            s.push_back({"Random interleaving (seed 7)", "A different, but reproducible, interleaving.", rnd});
            break;
        }
        case SyncAlgo::PETERSON: {
            SyncConfig both = c;
            both.processes = 2; both.iterations = 1;
            both.script = {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 0, 1};
            s.push_back({"Both want to enter at the same time",
                         "Both set interested = true; P1 writes turn last, so P1 waits and P0 enters (mutual exclusion).", both});
            SyncConfig alone = c;
            alone.processes = 2; alone.iterations = 1;
            alone.script = {0, 0, 0, 0, 0, 0, 0, 0};
            s.push_back({"P0 alone (progress)",
                         "P1 is not interested, so P0 enters at once without waiting (progress).", alone});
            SyncConfig bounded = c;
            bounded.processes = 2; bounded.iterations = 3; bounded.slice = 1;
            s.push_back({"Round robin, 3 rounds each (bounded waiting)",
                         "A waiting process enters after at most one entry of the other process (bounded waiting).", bounded});
            SyncConfig rnd = c;
            rnd.processes = 2; rnd.iterations = 3; rnd.randomOrder = true; rnd.seed = 11;
            s.push_back({"Random interleaving (seed 11)", "Any interleaving keeps mutual exclusion.", rnd});
            break;
        }
        case SyncAlgo::MUTEX: {
            SyncConfig cont = c;
            cont.processes = 4; cont.iterations = 2; cont.slice = 1;
            s.push_back({"4 processes contend for the lock",
                         "Waiters sleep in the lock's FIFO queue; release hands the lock to the first waiter.", cont});
            SyncConfig coarse = c;
            coarse.processes = 3; coarse.iterations = 2; coarse.slice = 5;
            s.push_back({"Long turns (no contention)",
                         "Each process finishes its critical section in one turn, so nobody waits.", coarse});
            SyncConfig rnd = c;
            rnd.processes = 3; rnd.iterations = 3; rnd.randomOrder = true; rnd.seed = 5;
            s.push_back({"Random interleaving (seed 5)", "Counter is always correct.", rnd});
            break;
        }
        case SyncAlgo::BINARY_SEM: {
            SyncConfig three = c;
            three.processes = 3; three.iterations = 2; three.slice = 1;
            s.push_back({"3 processes, S = 1", "wait(S) takes the only permit; the others block until signal(S).", three});
            SyncConfig rnd = c;
            rnd.processes = 4; rnd.iterations = 2; rnd.randomOrder = true; rnd.seed = 3;
            s.push_back({"Random interleaving (seed 3)", "S never goes above 1, the counter is always correct.", rnd});
            break;
        }
        case SyncAlgo::COUNTING_SEM: {
            SyncConfig printers = c;
            printers.processes = 5; printers.resources = 2; printers.iterations = 1; printers.slice = 1;
            s.push_back({"5 processes share 2 printers",
                         "At most 2 processes use a printer; the others block on S = 0.", printers});
            SyncConfig rnd = c;
            rnd.processes = 6; rnd.resources = 3; rnd.iterations = 2; rnd.randomOrder = true; rnd.seed = 9;
            s.push_back({"6 processes, 3 instances, random (seed 9)", "in_use never exceeds 3.", rnd});
            break;
        }
        case SyncAlgo::PRODUCER_CONSUMER:
        case SyncAlgo::MONITOR: {
            int lines = a == SyncAlgo::MONITOR ? 7 : 6;      // one whole item per turn
            SyncConfig fastP = c;
            fastP.producers = 1; fastP.consumers = 1; fastP.capacity = 3; fastP.iterations = 6;
            fastP.producerSpeed = lines; fastP.consumerSpeed = 1;
            s.push_back({"Fast producer: buffer fills up",
                         "The producer makes one item per turn, the consumer runs one line per turn: "
                         "the buffer becomes full and the producer blocks.", fastP});
            SyncConfig fastC = fastP;
            fastC.producerSpeed = 1; fastC.consumerSpeed = lines;
            s.push_back({"Fast consumer: buffer is empty",
                         "The consumer is faster: it finds the buffer empty and blocks.", fastC});
            SyncConfig many = c;
            many.producers = 2; many.consumers = 2; many.capacity = 2; many.iterations = 3;
            many.randomOrder = true; many.seed = 3;
            s.push_back({"2 producers, 2 consumers, N = 2, random (seed 3)",
                         "The buffer never overflows or underflows.", many});
            break;
        }
        case SyncAlgo::READERS_WRITERS: {
            SyncConfig share = c;
            share.readers = 3; share.writers = 1; share.iterations = 1;
            share.script = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 3, 2, 2, 2, 2, 2};
            s.push_back({"Readers share, the writer waits",
                         "Reader0 and Reader1 read at the same time; Writer0 blocks on rw_mutex until the LAST reader leaves "
                         "(read_count == 0 -> signal(rw_mutex)).", share});
            SyncConfig starve = c;
            starve.readers = 3; starve.writers = 1; starve.iterations = 3; starve.slice = 2;
            starve.script = {0, 0, 0, 0, 0, 3};
            s.push_back({"Readers' preference: the writer can starve",
                         "While readers keep overlapping, read_count never drops to 0 and the writer keeps waiting "
                         "(first readers-writers problem).", starve});
            SyncConfig rnd = c;
            rnd.readers = 3; rnd.writers = 2; rnd.iterations = 2; rnd.randomOrder = true; rnd.seed = 5;
            s.push_back({"3 readers, 2 writers, random order (seed 5)",
                         "Any interleaving: a writer is never inside together with anybody else.", rnd});
            break;
        }
        case SyncAlgo::DINING_PHILOSOPHERS: {
            SyncConfig dead = c;
            dead.philosophers = 5; dead.iterations = 1; dead.asymmetric = false;
            dead.script = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
            s.push_back({"Naive: every philosopher takes the left chopstick -> DEADLOCK",
                         "All five think and pick up their LEFT chopstick; then each waits for the right one, held by the "
                         "neighbour - a circular wait: nobody can run (stuck).", dead});
            SyncConfig asym = dead;
            asym.asymmetric = true;
            s.push_back({"Asymmetric: odd philosophers take the right chopstick first",
                         "Same start, but the circular wait is broken: some philosopher always gets both chopsticks; "
                         "everybody eats.", asym});
            SyncConfig rnd = c;
            rnd.philosophers = 5; rnd.iterations = 2; rnd.asymmetric = true; rnd.randomOrder = true; rnd.seed = 4;
            s.push_back({"Asymmetric, random order (seed 4)", "Neighbours never eat at the same time; no deadlock.", rnd});
            break;
        }
    }
    return s;
}

// ---------------------------------------------------------------- comparison

std::string syncGuarantee(SyncAlgo a) {
    switch (a) {
        case SyncAlgo::RACE: return "None: updates can be lost, mutual exclusion is not guaranteed";
        case SyncAlgo::PETERSON:
            return "Mutual exclusion, progress and bounded waiting (2 processes; needs sequentially consistent memory)";
        case SyncAlgo::MUTEX:
            return "Mutual exclusion; POSIX does not promise FIFO order or bounded waiting (simulation uses FIFO)";
        case SyncAlgo::BINARY_SEM:
            return "Mutual exclusion; POSIX does not specify which waiter wakes first (simulation uses FIFO)";
        case SyncAlgo::COUNTING_SEM: return "At most S processes use the resource at the same time";
        case SyncAlgo::PRODUCER_CONSUMER: return "No overflow, no underflow, mutual exclusion on the buffer";
        case SyncAlgo::MONITOR: return "One process inside the monitor; waits only while the condition is false";
        case SyncAlgo::READERS_WRITERS: return "Many readers OR one writer; readers' preference: writers can starve";
        case SyncAlgo::DINING_PHILOSOPHERS:
            return "Neighbours never eat together; naive order can deadlock (circular wait), the asymmetric order cannot";
    }
    return "";
}

std::vector<SyncComparisonRow> compareSync(int iterations, int slice, bool randomOrder, unsigned seed) {
    std::vector<SyncComparisonRow> rows;
    for (SyncAlgo a : {SyncAlgo::RACE, SyncAlgo::PETERSON, SyncAlgo::MUTEX, SyncAlgo::BINARY_SEM}) {
        SyncConfig c;
        c.algo = a;
        c.processes = 2;
        c.iterations = iterations;
        c.slice = slice;
        c.randomOrder = randomOrder;
        c.seed = seed;
        SyncSim sim(c);
        sim.runToEnd();
        const char* label = a == SyncAlgo::RACE ? "Unsynchronized" : a == SyncAlgo::PETERSON ? "Peterson"
                          : a == SyncAlgo::MUTEX ? "Mutex lock" : "Binary semaphore";
        rows.push_back({a, label, sim.stats(), syncGuarantee(a)});
    }
    return rows;
}

std::string syncExplanation(SyncAlgo a) {
    switch (a) {
        case SyncAlgo::RACE:
            return "Race condition\n"
                   "counter++ is not one instruction: the CPU reads counter into a register, adds 1 and writes it back. "
                   "If another process runs between the read and the write, both read the same old value and one "
                   "increment is lost (lost update). The read..write region is the critical section; without "
                   "synchronization two processes can be inside it at the same time (shown as a violation).\n"
                   "Try it: press a process card's Step button to choose who runs the next instruction.";
        case SyncAlgo::PETERSON:
            return "Peterson's solution (2 processes)\n"
                   "interested[i] says that process i wants to enter; turn decides who waits when both want to. "
                   "The process that writes turn LAST waits (turn == process), so only one can pass the while loop.\n"
                   "- Mutual exclusion: both inside would need turn == 0 and turn == 1 at the same time.\n"
                   "- Progress: a process that is not interested never blocks the other.\n"
                   "- Bounded waiting: a waiting process enters after at most one entry of the other.\n"
                   "Waiting is a busy wait (the process uses the CPU while it spins). On real hardware the "
                   "algorithm needs sequentially consistent memory (std::atomic with memory_order_seq_cst).";
        case SyncAlgo::MUTEX:
            return "Mutex lock\n"
                   "acquire(lock) succeeds if the lock is free and makes the caller its owner; otherwise the caller "
                   "sleeps in the lock's waiting queue (no busy wait). release(lock) by the owner frees it or hands it "
                   "to a waiter.\n"
                   "The simulation wakes waiters in FIFO order. A real POSIX pthread_mutex gives mutual exclusion but "
                   "does NOT promise FIFO order or bounded waiting - the scheduler decides who gets it next.";
        case SyncAlgo::BINARY_SEM:
            return "Binary semaphore\n"
                   "S starts at 1. wait(S): if S > 0 then S-- and continue, else sleep in S's queue. signal(S): wake one "
                   "sleeper (it continues), or S++ if nobody waits. With S in {0, 1} it works like a lock, but any "
                   "process may signal (there is no owner).\n"
                   "Real code: sem_init(&S, 0, 1), sem_wait, sem_post, sem_destroy.";
        case SyncAlgo::COUNTING_SEM:
            return "Counting semaphore\n"
                   "S starts at the number of identical resource instances (e.g. 2 printers). Each wait(S) takes one "
                   "permit; when S reaches 0 further callers block until someone signals. So at most S processes use "
                   "the resource at the same time (in_use <= S).";
        case SyncAlgo::PRODUCER_CONSUMER:
            return "Producer-consumer (bounded buffer)\n"
                   "Three semaphores: empty = N (free slots), full = 0 (items), mutex = 1 (buffer access). "
                   "A producer waits on empty (blocks when the buffer is full), a consumer waits on full (blocks when "
                   "it is empty). mutex protects in/out. The order wait(empty) then wait(mutex) matters: swapping them "
                   "can deadlock.";
        case SyncAlgo::MONITOR:
            return "Monitor with condition variables\n"
                   "The buffer and count are hidden inside the monitor; insert() and remove() run with the monitor lock "
                   "held, so only one process is inside. If it cannot continue it calls cond.wait(), which releases the "
                   "lock and sleeps; cond.signal() moves one sleeper to the entry queue (Mesa / pthread semantics: the "
                   "signaller keeps the lock), so the woken process must re-check its condition - hence while, not if.\n"
                   "Real code: pthread_mutex_t + pthread_cond_wait / pthread_cond_signal.";
        case SyncAlgo::READERS_WRITERS:
            return "Readers-writers (first problem: readers' preference)\n"
                   "Many readers may read the shared data at the same time, but a writer needs it alone. rw_mutex = 1 "
                   "excludes writers; read_count counts the readers inside and is protected by mutex = 1. The FIRST reader "
                   "waits on rw_mutex (locking writers out), the LAST reader signals it.\n"
                   "Consequence: while readers keep overlapping, read_count never reaches 0 and a waiting writer can starve. "
                   "Writer-preference or fair variants add more semaphores (not shown).\n"
                   "Real code: sem_t or pthread_rwlock_t (which has its own fairness policy).";
        case SyncAlgo::DINING_PHILOSOPHERS:
            return "Dining philosophers\n"
                   "N philosophers share N chopsticks (binary semaphores); philosopher i needs chopsticks i and (i+1) mod N. "
                   "If every philosopher takes the left chopstick first, all four deadlock conditions hold (mutual "
                   "exclusion, hold and wait, no preemption, CIRCULAR WAIT) and nobody can continue.\n"
                   "The asymmetric solution lets odd philosophers take the right chopstick first; the circular wait "
                   "cannot form, so there is no deadlock (resource ordering - compare deadlock prevention). Neighbours "
                   "never eat at the same time; at most N/2 eat at once.";
    }
    return "";
}
