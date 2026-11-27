// procmgmt.cpp - validated 5-state transitions and the automatic event-driven run.
#include "procmgmt.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

const char* lcEventName(LcEvent e) {
    switch (e) {
        case LcEvent::Create: return "create";
        case LcEvent::Admit: return "admit";
        case LcEvent::Dispatch: return "dispatch";
        case LcEvent::Preempt: return "preempt";
        case LcEvent::BlockIO: return "block on I/O";
        case LcEvent::IOComplete: return "I/O complete";
        case LcEvent::Exit: return "exit";
        case LcEvent::Kill: return "kill";
        case LcEvent::Execute: return "execute";
        case LcEvent::Idle: return "CPU idle";
    }
    return "?";
}

const char* twoStateName(ProcState s) {
    return s == ProcState::Running ? "Running" : s == ProcState::Terminated ? "(gone)" : "Not Running";
}

static void validateSpec(const LcProcSpec& s) {
    auto need = [&](bool ok, const std::string& m) {
        if (!ok) throw std::invalid_argument("P" + std::to_string(s.pid) + ": " + m);
    };
    need(s.pid >= 2 && s.pid <= 99999, "pid must be between 2 and 99999 (1 is the modelled init)");
    need(s.arrival >= 0 && s.arrival <= 1000, "arrival must be between 0 and 1000");
    need(s.bursts.size() % 2 == 1 && s.bursts.size() <= 21, "bursts must alternate CPU, I/O, ..., CPU (odd count, at most 21)");
    for (int b : s.bursts) need(b >= 1 && b <= 100, "every burst must be between 1 and 100 ticks");
    need(s.pages >= 0 && s.pages <= 1024, "pages must be between 0 and 1024");
}

LifecycleSim::LifecycleSim(const std::vector<LcProcSpec>& procs, Algo policy, int quantum, bool manual)
    : policy_(policy), quantum_(quantum), manual_(manual) {
    if (procs.size() > 16) throw std::invalid_argument("at most 16 processes are supported");
    if (quantum < 1 || quantum > 100) throw std::invalid_argument("quantum must be between 1 and 100");
    std::set<int> seen;
    for (const LcProcSpec& s : procs) {
        validateSpec(s);
        if (!seen.insert(s.pid).second) throw std::invalid_argument("duplicate pid " + std::to_string(s.pid));
    }
    for (const LcProcSpec& s : procs)
        if (s.ppid != 1 && !seen.count(s.ppid))
            throw std::invalid_argument("P" + std::to_string(s.pid) + ": parent " + std::to_string(s.ppid) + " does not exist");
    // In the automatic mode processes appear at their arrival time; in the manual mode they are
    // created at once (New) and the user admits / dispatches them.
    record(LcEvent::Idle, -1, ProcState::New, ProcState::New, manual ? "Manual mode: processes are created; use the commands"
                                                                      : "Start: no process has arrived yet");
    pending_ = procs;
    std::stable_sort(pending_.begin(), pending_.end(), [](const LcProcSpec& a, const LcProcSpec& b) { return a.arrival < b.arrival; });
    if (manual) {
        for (const LcProcSpec& s : pending_) create(s);
        pending_.clear();
    }
}

int LifecycleSim::idx(int pid) const {
    for (int i = 0; i < (int)pcbs_.size(); ++i) if (pcbs_[i].pid == pid) return i;
    return -1;
}

bool LifecycleSim::allTerminated() const {
    if (!pending_.empty()) return false;
    for (const LcPCB& p : pcbs_) if (p.state != ProcState::Terminated) return false;
    return true;
}

// Illustrative register context: values change with the program counter so the PCB
// inspector can show what a context switch saves (NOT real Linux registers).
void LifecycleSim::saveContext(LcPCB& p) {
    p.regs[0] = p.pc;                         // modelled PC
    p.regs[1] = 0x1000 + p.pid * 0x100 + p.pc * 4;
    p.regs[2] = p.cpuUsed * 7 + p.pid;
    p.regs[3] = 0x7ff0 - p.pid * 0x40;        // modelled stack pointer
}

void LifecycleSim::record(LcEvent ev, int pid, ProcState from, ProcState to, const std::string& text) {
    LcSnapshot s;
    s.clock = clock_;
    s.event = ev;
    s.pid = pid;
    s.from = from;
    s.to = to;
    s.text = "[t=" + std::to_string(clock_) + "] " + text;
    s.pcbs = pcbs_;
    for (int i : ready_) s.readyQueue.push_back(pcbs_[i].pid);
    for (int i : wait_) s.waitQueue.push_back(pcbs_[i].pid);
    s.running = running_ >= 0 ? pcbs_[running_].pid : -1;
    s.contextSwitches = switches_;
    trace_.push_back(std::move(s));
}

void LifecycleSim::truncate(int n) {
    if (n < 0 || n + 1 >= (int)trace_.size()) return;
    trace_.resize(n + 1);
    // restore the live state from the snapshot
    const LcSnapshot& s = trace_.back();
    pcbs_ = s.pcbs;
    ready_.clear();
    wait_.clear();
    for (int pid : s.readyQueue) ready_.push_back(idx(pid));
    for (int pid : s.waitQueue) wait_.push_back(idx(pid));
    running_ = s.running >= 0 ? idx(s.running) : -1;
    clock_ = s.clock;
    switches_ = s.contextSwitches;
}

static std::string pname(const LcPCB& p) { return "P" + std::to_string(p.pid) + (p.name.empty() ? "" : " (" + p.name + ")"); }

std::string LifecycleSim::create(const LcProcSpec& spec) {
    try { validateSpec(spec); } catch (const std::exception& e) { return e.what(); }
    if (idx(spec.pid) >= 0) return "pid " + std::to_string(spec.pid) + " already exists";
    if (pcbs_.size() >= 16) return "at most 16 processes";
    if (spec.ppid != 1) {
        int pi = idx(spec.ppid);
        if (pi < 0) return "parent P" + std::to_string(spec.ppid) + " does not exist";
        if (pcbs_[pi].state == ProcState::Terminated) return "parent P" + std::to_string(spec.ppid) + " has terminated";
    }
    LcPCB p;
    p.pid = spec.pid; p.ppid = spec.ppid; p.name = spec.name; p.priority = spec.priority;
    p.arrival = spec.arrival; p.pages = spec.pages; p.bursts = spec.bursts;
    p.burstIndex = 0;
    p.burstLeft = spec.bursts[0];
    for (size_t i = 0; i < spec.bursts.size(); i += 2) p.cpuTotal += spec.bursts[i];
    saveContext(p);
    pcbs_.push_back(p);
    PCB s;
    s.pid = p.pid; s.arrival = p.arrival; s.burst = p.burstLeft; s.remaining = p.burstLeft; s.priority = p.priority;
    sched_.push_back(s);
    record(LcEvent::Create, p.pid, ProcState::New, ProcState::New,
           pname(p) + " created by " + (p.ppid == 1 ? std::string("init (modelled)") : "P" + std::to_string(p.ppid)) +
           ": PCB allocated, state New");
    return "";
}

std::string LifecycleSim::admit(int pid) {
    int i = idx(pid);
    if (i < 0) return "no process " + std::to_string(pid);
    if (pcbs_[i].state != ProcState::New) return pname(pcbs_[i]) + " is not New (admission is New -> Ready only)";
    pcbs_[i].state = ProcState::Ready;
    ready_.push_back(i);
    sched_[i].state = ProcState::Ready;
    st_.ready.push_back(i);
    record(LcEvent::Admit, pid, ProcState::New, ProcState::Ready, pname(pcbs_[i]) + " admitted: New -> Ready (joins the ready queue)");
    return "";
}

std::string LifecycleSim::dispatch(int pid) {
    int i = idx(pid);
    if (i < 0) return "no process " + std::to_string(pid);
    if (pcbs_[i].state != ProcState::Ready)
        return pname(pcbs_[i]) + " is " + stateName(pcbs_[i].state) + ": only a Ready process can be dispatched";
    if (running_ >= 0) return "the CPU is busy with " + pname(pcbs_[running_]) + " (one CPU: preempt or block it first)";
    ready_.erase(std::find(ready_.begin(), ready_.end(), i));
    auto it = std::find(st_.ready.begin(), st_.ready.end(), i);
    if (it != st_.ready.end()) st_.ready.erase(it);
    st_.running = i;
    st_.sliceUsed = 0;
    LcPCB& p = pcbs_[i];
    p.state = ProcState::Running;
    sched_[i].state = ProcState::Running;
    if (p.firstRun < 0) p.firstRun = clock_;
    p.dispatches++;
    running_ = i;
    ++switches_;
    record(LcEvent::Dispatch, pid, ProcState::Ready, ProcState::Running,
           pname(p) + " dispatched: context restored from its PCB (PC=" + std::to_string(p.pc) + "), Ready -> Running");
    return "";
}

std::string LifecycleSim::preempt() {
    if (running_ < 0) return "no process is running";
    LcPCB& p = pcbs_[running_];
    saveContext(p);
    p.state = ProcState::Ready;
    ready_.push_back(running_);
    sched_[running_].state = ProcState::Ready;
    st_.ready.push_back(running_);
    st_.running = -1;
    int pid = p.pid;
    running_ = -1;
    record(LcEvent::Preempt, pid, ProcState::Running, ProcState::Ready,
           pname(pcbs_[idx(pid)]) + " preempted: context saved in the PCB, Running -> Ready (back of the ready queue)");
    return "";
}

std::string LifecycleSim::blockIO(int ticks) {
    if (running_ < 0) return "no process is running (only the running process can request I/O)";
    if (ticks < 1 || ticks > 100) return "I/O time must be between 1 and 100 ticks";
    LcPCB& p = pcbs_[running_];
    saveContext(p);
    p.state = ProcState::Waiting;
    p.ioLeft = ticks;
    p.waitReason = "I/O request (" + std::to_string(ticks) + " ticks)";
    wait_.push_back(running_);
    sched_[running_].state = ProcState::Waiting;
    st_.running = -1;
    int pid = p.pid;
    running_ = -1;
    record(LcEvent::BlockIO, pid, ProcState::Running, ProcState::Waiting,
           pname(pcbs_[idx(pid)]) + " requests I/O: Running -> Waiting (" + std::to_string(ticks) + " ticks); the CPU is free");
    return "";
}

std::string LifecycleSim::completeIO(int pid) {
    int i = idx(pid);
    if (i < 0) return "no process " + std::to_string(pid);
    if (pcbs_[i].state != ProcState::Waiting)
        return pname(pcbs_[i]) + " is " + stateName(pcbs_[i].state) + ": only a Waiting process can complete I/O";
    LcPCB& p = pcbs_[i];
    wait_.erase(std::find(wait_.begin(), wait_.end(), i));
    p.state = ProcState::Ready;
    p.waitReason.clear();
    p.ioLeft = 0;
    if (!manual_ && p.burstIndex % 2 == 1 && p.burstIndex + 1 < (int)p.bursts.size()) {   // next CPU burst
        p.burstIndex++;
        p.burstLeft = p.bursts[p.burstIndex];
    }
    sched_[i].remaining = p.burstLeft;
    sched_[i].state = ProcState::Ready;
    ready_.push_back(i);
    st_.ready.push_back(i);
    record(LcEvent::IOComplete, pid, ProcState::Waiting, ProcState::Ready,
           pname(p) + " I/O complete (interrupt): Waiting -> Ready (it must be dispatched again, never Waiting -> Running)");
    return "";
}

std::string LifecycleSim::exitRunning() {
    if (running_ < 0) return "no process is running (exit is called by the running process)";
    LcPCB& p = pcbs_[running_];
    p.state = ProcState::Terminated;
    p.finish = clock_;
    sched_[running_].state = ProcState::Terminated;
    st_.running = -1;
    int pid = p.pid;
    running_ = -1;
    record(LcEvent::Exit, pid, ProcState::Running, ProcState::Terminated,
           pname(pcbs_[idx(pid)]) + " calls exit(): Running -> Terminated, resources released, parent notified");
    return "";
}

std::string LifecycleSim::kill(int pid) {
    int i = idx(pid);
    if (i < 0) return "no process " + std::to_string(pid);
    LcPCB& p = pcbs_[i];
    if (p.state == ProcState::Terminated) return pname(p) + " has already terminated";
    if (p.state == ProcState::Running) return pname(p) + " is running: use Exit (kill is only for processes that are not running)";
    ProcState from = p.state;
    auto drop = [&](std::vector<int>& q) { auto it = std::find(q.begin(), q.end(), i); if (it != q.end()) q.erase(it); };
    drop(ready_);
    drop(wait_);
    drop(st_.ready);
    p.state = ProcState::Terminated;
    p.killed = true;
    p.finish = clock_;
    sched_[i].state = ProcState::Terminated;
    record(LcEvent::Kill, pid, from, ProcState::Terminated,
           pname(p) + " killed by the user (separate event, not exit): " + stateName(from) + " -> Terminated");
    return "";
}

// One tick of logical time: the running process executes one instruction of its CPU burst,
// waiting processes progress their I/O, ready processes accumulate waiting time.
std::string LifecycleSim::tick() {
    std::string text;
    if (running_ >= 0) {
        LcPCB& p = pcbs_[running_];
        p.cpuUsed++;
        p.pc++;
        if (p.burstLeft > 0) p.burstLeft--;
        sched_[running_].remaining = p.burstLeft;
        st_.sliceUsed++;
        saveContext(p);
        text = pname(p) + " executes on the CPU (PC=" + std::to_string(p.pc) + ", " + std::to_string(p.burstLeft) +
               " tick(s) left in this CPU burst)";
    } else {
        text = "CPU idle: no Ready process";
    }
    for (int i : ready_) pcbs_[i].readyTicks++;
    for (int i : wait_) {
        pcbs_[i].waitTicks++;
        if (pcbs_[i].ioLeft > 0) pcbs_[i].ioLeft--;
    }
    clock_++;
    record(running_ >= 0 ? LcEvent::Execute : LcEvent::Idle, running_ >= 0 ? pcbs_[running_].pid : -1,
           running_ >= 0 ? ProcState::Running : ProcState::New, running_ >= 0 ? ProcState::Running : ProcState::New, text);
    return "";
}

// Automatic run: arrivals -> I/O completions -> dispatcher (schedulerPick) -> execute -> end of burst.
int LifecycleSim::runAuto(int maxTicks) {
    if (manual_) return 0;
    while (!allTerminated() && clock_ < maxTicks && trace_.size() < 20000) {
        // 1. arrivals
        while (!pending_.empty() && pending_.front().arrival <= clock_) {
            LcProcSpec s = pending_.front();
            pending_.erase(pending_.begin());
            create(s);
            admit(s.pid);
        }
        // 2. I/O completions (in the order the processes blocked)
        for (int i : std::vector<int>(wait_))
            if (pcbs_[i].ioLeft == 0) completeIO(pcbs_[i].pid);
        // 3. dispatcher decision of the selected scheduling policy
        // The decision is taken by schedulerPick() on COPIES of the scheduler state, then
        // applied through the validated transitions so that queues and PCBs stay consistent.
        int prev = st_.running;
        if (prev >= 0 || !st_.ready.empty()) {
            std::vector<PCB> simPcbs = sched_;
            SchedState simSt = st_;
            int pick = schedulerPick(policy_, simPcbs, simSt, quantum_);
            if (prev >= 0 && pick != prev) {
                preempt();
                std::string why = policy_ == Algo::RR ? "quantum of " + std::to_string(quantum_) + " expired"
                                                       : std::string(algoName(policy_)) + " chose another process";
                trace_.back().text += " (" + why + ")";
            }
            if (pick >= 0 && pick != prev) {
                dispatch(pcbs_[pick].pid);
                trace_.back().text += std::string(" [") + algoName(policy_) + " decision]";
            } else if (pick == prev) {
                st_.sliceUsed = simSt.sliceUsed;          // RR: re-picked alone, new quantum
            }
        }
        // 4. execute one tick
        tick();
        // 5. end of the CPU burst: block for the next I/O burst, or exit
        if (running_ >= 0 && pcbs_[running_].burstLeft == 0) {
            LcPCB& p = pcbs_[running_];
            if (p.burstIndex + 1 < (int)p.bursts.size()) {
                p.burstIndex++;
                blockIO(p.bursts[p.burstIndex]);
            } else {
                exitRunning();
            }
        }
        // jump over long idle periods quickly (still one event per tick for a clear timeline)
    }
    return (int)trace_.size() - 1;
}

std::vector<LcProcSpec> loadLifecycleCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<LcProcSpec> v;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("pid,", 0) == 0) continue;               // header
        std::stringstream ss(line);
        std::string f[7];
        for (int k = 0; k < 7; ++k) std::getline(ss, f[k], ',');
        try {
            LcProcSpec s;
            s.pid = std::stoi(f[0]); s.ppid = std::stoi(f[1]); s.name = f[2];
            s.arrival = std::stoi(f[3]); s.priority = std::stoi(f[4]);
            std::istringstream bs(f[5]);
            int b;
            while (bs >> b) s.bursts.push_back(b);
            s.pages = f[6].empty() ? 0 : std::stoi(f[6]);
            v.push_back(s);
        } catch (const std::exception&) {
            throw std::runtime_error(path + " line " + std::to_string(n) + ": expected pid,ppid,name,arrival,priority,bursts,pages");
        }
    }
    if (v.empty()) throw std::runtime_error(path + ": no processes");
    try { LifecycleSim check(v, Algo::FCFS, 2); } catch (const std::exception& e) { throw std::runtime_error(path + ": " + e.what()); }
    return v;
}

std::vector<LcPreset> lifecyclePresets() {
    std::vector<LcPreset> p;
    p.push_back({"Two processes and one I/O (reference)",
                 "P2 and P3 arrive at t=0. P2 runs, blocks on I/O, P3 runs, P2's I/O completes (Waiting -> Ready), both terminate.",
                 {{2, 1, "editor", 0, 1, {2, 3, 2}, 4}, {3, 1, "compiler", 0, 2, {4}, 6}}, Algo::FCFS, 2});
    p.push_back({"Round robin with preemption (q=2)",
                 "Three CPU-bound processes share the CPU; every quantum expiry is a Running -> Ready preemption.",
                 {{2, 1, "A", 0, 1, {5}, 2}, {3, 1, "B", 1, 1, {3}, 2}, {4, 1, "C", 2, 1, {4}, 2}}, Algo::RR, 2});
    p.push_back({"Edge: CPU idle while every process waits for I/O",
                 "Both processes block on long I/O at the same time: the CPU is idle until an I/O completes.",
                 {{2, 1, "reader", 0, 1, {1, 5, 1}, 3}, {3, 1, "writer", 0, 1, {1, 4, 1}, 3}}, Algo::FCFS, 2});
    p.push_back({"Process tree: shell with children and a grandchild",
                 "A shell (P2) has two children (P3, P4); P4 has a child P5. SRTF dispatching.",
                 {{2, 1, "bash", 0, 3, {1, 2, 1}, 8}, {3, 2, "ls", 1, 2, {2}, 2}, {4, 2, "make", 1, 2, {3, 2, 2}, 6},
                  {5, 4, "gcc", 3, 1, {4}, 12}}, Algo::SRTF, 2});
    return p;
}
