// scheduler.cpp - FCFS, SJF, SRTF, Round Robin and Priority scheduling.
#include "scheduler.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string>

// Returns the display name of an algorithm.
const char* algoName(Algo a) {
    switch (a) {
        case Algo::FCFS:        return "FCFS";
        case Algo::SJF:         return "SJF";
        case Algo::SRTF:        return "SRTF";
        case Algo::RR:          return "Round Robin";
        case Algo::PRIORITY_NP: return "Priority (NP)";
        case Algo::PRIORITY_P:  return "Priority (P)";
        case Algo::LJF:         return "LJF";
        case Algo::LRTF:        return "LRTF";
    }
    return "?";
}

// Short identifier used by the console demo and sample files.
const char* algoId(Algo a) {
    switch (a) {
        case Algo::FCFS:        return "fcfs";
        case Algo::SJF:         return "sjf";
        case Algo::SRTF:        return "srtf";
        case Algo::RR:          return "rr";
        case Algo::PRIORITY_NP: return "prio";
        case Algo::PRIORITY_P:  return "prio-p";
        case Algo::LJF:         return "ljf";
        case Algo::LRTF:        return "lrtf";
    }
    return "?";
}

// Reverse of algoId(); case-insensitive.
bool algoFromId(const std::string& id, Algo& out) {
    std::string low;
    for (char c : id) low += (char)std::tolower((unsigned char)c);
    for (Algo a : allAlgos())
        if (low == algoId(a)) { out = a; return true; }
    return false;
}

// Preemptive algorithms can move Running -> Ready without the process finishing.
bool isPreemptive(Algo a) {
    return a == Algo::SRTF || a == Algo::LRTF || a == Algo::RR || a == Algo::PRIORITY_P;
}

// Fixed list of all algorithms.
const std::vector<Algo>& allAlgos() {
    static const std::vector<Algo> v = {Algo::FCFS, Algo::SJF, Algo::SRTF, Algo::LJF, Algo::LRTF,
                                        Algo::RR, Algo::PRIORITY_NP, Algo::PRIORITY_P};
    return v;
}

// Input validation: a scheduler cannot run a process with no CPU burst, etc.
void validateProcesses(const std::vector<PCB>& procs, Algo algo, int quantum) {
    if (procs.empty()) throw std::invalid_argument("process list is empty");
    std::set<int> seen;
    for (const PCB& p : procs) {
        std::string id = "P" + std::to_string(p.pid);
        if (p.burst <= 0)   throw std::invalid_argument(id + ": burst must be > 0");
        if (p.arrival < 0)  throw std::invalid_argument(id + ": arrival must be >= 0");
        if (p.pages < 0)    throw std::invalid_argument(id + ": pages must be >= 0");
        if (!seen.insert(p.pid).second)
            throw std::invalid_argument("duplicate pid " + std::to_string(p.pid));
    }
    if (algo == Algo::RR && quantum <= 0)
        throw std::invalid_argument("time quantum must be > 0");
}

// Ready-queue admission (state transition New/Waiting/Running -> Ready).
void makeReady(std::vector<PCB>& procs, SchedState& st, int idx) {
    procs[idx].state = ProcState::Ready;
    st.ready.push_back(idx);
}

// Returns true if process a should be chosen before process b for this algorithm.
// Tie rule (deterministic): same key -> earlier arrival -> lower pid.
// "Longest" algorithms negate the key so that the smaller key still wins.
static bool better(Algo algo, const PCB& a, const PCB& b) {
    int ka = 0, kb = 0;
    switch (algo) {
        case Algo::LJF:                                    // longest CPU burst first
            ka = -a.burst; kb = -b.burst; break;
        case Algo::LRTF:                                   // longest remaining time first
            ka = -a.remaining; kb = -b.remaining; break;
        case Algo::SJF: case Algo::SRTF:
            ka = a.remaining; kb = b.remaining; break;     // shortest (remaining) job
        case Algo::PRIORITY_NP: case Algo::PRIORITY_P:
            ka = a.priority; kb = b.priority; break;       // lower number = higher priority
        default:
            ka = a.arrival; kb = b.arrival; break;         // FCFS: first come
    }
    if (ka != kb) return ka < kb;
    if (a.arrival != b.arrival) return a.arrival < b.arrival;
    return a.pid < b.pid;
}

// The dispatcher: decide which process uses the CPU during the current tick.
int schedulerPick(Algo algo, std::vector<PCB>& procs, SchedState& st, int quantum) {
    if (st.running >= 0) {
        bool preempt = false;
        if (algo == Algo::RR)
            preempt = st.sliceUsed >= quantum;          // quantum expired
        else if (algo == Algo::SRTF || algo == Algo::PRIORITY_P)
            preempt = true;                              // re-compete every tick
        else if (algo == Algo::LRTF) {
            // LRTF: preempt only if a ready process has STRICTLY more remaining time.
            // On a tie the running process keeps the CPU (no needless context switch).
            for (int r : st.ready)
                if (procs[r].remaining > procs[st.running].remaining) { preempt = true; break; }
        }
        if (preempt) {
            // New arrivals were already queued by the caller, so in RR the
            // preempted process goes behind them (required rule).
            int prev = st.running;
            st.running = -1;
            makeReady(procs, st, prev);
        } else {
            return st.running;                           // non-preemptive: keep running
        }
    }
    if (st.ready.empty()) return -1;                     // CPU idle

    size_t best = 0;
    if (algo != Algo::RR) {                              // RR always takes the queue head
        for (size_t i = 1; i < st.ready.size(); ++i)
            if (better(algo, procs[st.ready[i]], procs[st.ready[best]])) best = i;
    }
    int idx = st.ready[best];
    st.ready.erase(st.ready.begin() + best);
    st.sliceUsed = 0;                                    // a fresh dispatch starts a new quantum
    st.running = idx;
    procs[idx].state = ProcState::Running;
    return idx;
}

// Adds one tick to the Gantt timeline, merging with the previous segment if same pid.
void appendTick(std::vector<Segment>& timeline, int pid, int t) {
    if (!timeline.empty() && timeline.back().pid == pid && timeline.back().end == t)
        timeline.back().end = t + 1;
    else
        timeline.push_back({pid, t, t + 1});
}

// Simulates the chosen algorithm tick by tick and computes all metrics.
SchedResult runScheduler(Algo algo, std::vector<PCB> procs, int quantum) {
    validateProcesses(procs, algo, quantum);
    for (PCB& p : procs) {
        p.remaining = p.burst; p.state = ProcState::New; p.firstRun = -1; p.finish = -1;
    }
    // Admission order for processes arriving at the same tick: lower pid first.
    std::vector<int> order(procs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (procs[a].arrival != procs[b].arrival) return procs[a].arrival < procs[b].arrival;
        return procs[a].pid < procs[b].pid;
    });

    SchedResult r;
    r.algo = algo;
    r.quantum = (algo == Algo::RR) ? quantum : 0;
    SchedState st;
    size_t done = 0, nextArrival = 0;
    for (int t = 0; done < procs.size(); ++t) {
        // 1. Long-term scheduler: admit processes that arrive now (New -> Ready).
        while (nextArrival < order.size() && procs[order[nextArrival]].arrival <= t)
            makeReady(procs, st, order[nextArrival++]);
        // 2. Short-term scheduler: choose who runs during [t, t+1).
        int idx = schedulerPick(algo, procs, st, quantum);
        if (idx < 0) { appendTick(r.timeline, -1, t); continue; }
        // 3. Execute one tick on the CPU.
        PCB& p = procs[idx];
        if (p.firstRun < 0) p.firstRun = t;
        p.remaining--;
        st.sliceUsed++;
        appendTick(r.timeline, p.pid, t);
        if (p.remaining == 0) {                  // Running -> Terminated
            p.state = ProcState::Terminated;
            p.finish = t + 1;
            st.running = -1;
            st.sliceUsed = 0;
            ++done;
        }
    }

    // Metrics: TAT = completion - arrival, WT = TAT - burst, RT = firstRun - arrival.
    for (const PCB& p : procs) {
        ProcMetrics m{p.pid, p.arrival, p.burst, p.finish, 0, 0, 0};
        m.turnaround = p.finish - p.arrival;
        m.waiting = m.turnaround - p.burst;
        m.response = p.firstRun - p.arrival;
        r.metrics.push_back(m);
        r.avgWaiting += m.waiting; r.avgTurnaround += m.turnaround; r.avgResponse += m.response;
    }
    std::sort(r.metrics.begin(), r.metrics.end(),
              [](const ProcMetrics& a, const ProcMetrics& b) { return a.pid < b.pid; });
    double n = (double)procs.size();
    r.avgWaiting /= n; r.avgTurnaround /= n; r.avgResponse /= n;

    // Context switches: count pid changes in the timeline, ignoring idle gaps.
    int last = -1;
    for (const Segment& s : r.timeline) {
        if (s.pid < 0) continue;
        if (last >= 0 && s.pid != last) r.contextSwitches++;
        last = s.pid;
    }
    return r;
}

// Runs every algorithm on identical input so they can be compared.
std::vector<SchedResult> runAll(const std::vector<PCB>& procs, int quantum) {
    std::vector<SchedResult> out;
    for (Algo a : allAlgos()) out.push_back(runScheduler(a, procs, quantum));
    return out;
}

// Busy ticks divided by total length of the timeline, in percent.
double cpuUtilization(const SchedResult& r) {
    if (r.timeline.empty()) return 0.0;
    int busy = 0;
    for (const Segment& s : r.timeline) if (s.pid >= 0) busy += s.end - s.start;
    return 100.0 * busy / r.timeline.back().end;
}
