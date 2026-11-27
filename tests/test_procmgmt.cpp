// test_procmgmt.cpp - M2 process lifecycle (core/procmgmt.*): reference I/O example,
// invariants after every event, transition validation, preemption, idle CPU, process tree,
// consistency with the CPU scheduler module, reset/determinism, sample file.
#include <algorithm>
#include <set>
#include "check.h"
#include "procmgmt.h"

// One CPU; queues contain exactly the processes in the matching state.
static bool invariantsHold(const LcSnapshot& s) {
    int running = 0;
    for (const LcPCB& p : s.pcbs) {
        if (p.state == ProcState::Running) { ++running; if (s.running != p.pid) return false; }
        bool inReady = std::count(s.readyQueue.begin(), s.readyQueue.end(), p.pid) == 1;
        bool inWait = std::count(s.waitQueue.begin(), s.waitQueue.end(), p.pid) == 1;
        if ((p.state == ProcState::Ready) != inReady) return false;
        if ((p.state == ProcState::Waiting) != inWait) return false;
        if (p.cpuUsed > p.cpuTotal) return false;
    }
    if (running > 1) return false;
    if (running == 0 && s.running != -1) return false;
    return true;
}

static std::vector<std::pair<LcEvent, int>> transitions(const LifecycleSim& sim) {
    std::vector<std::pair<LcEvent, int>> v;
    for (const LcSnapshot& s : sim.trace())
        if (s.event != LcEvent::Execute && s.event != LcEvent::Idle) v.push_back({s.event, s.pid});
    return v;
}

static int finishOf(const LifecycleSim& sim, int pid) {
    for (const LcPCB& p : sim.current().pcbs) if (p.pid == pid) return p.finish;
    return -99;
}

int main() {
    std::vector<LcPreset> presets = lifecyclePresets();
    CHECK(presets.size() >= 4);

    // Reference: P2 runs, blocks on I/O, P3 runs, I/O completes, P2 Ready, both terminate.
    LifecycleSim ref(presets[0].procs, presets[0].policy, presets[0].quantum);
    ref.runAuto();
    using E = LcEvent;
    std::vector<std::pair<LcEvent, int>> expect = {
        {E::Create, 2}, {E::Admit, 2}, {E::Create, 3}, {E::Admit, 3}, {E::Dispatch, 2}, {E::BlockIO, 2},
        {E::Dispatch, 3}, {E::IOComplete, 2}, {E::Exit, 3}, {E::Dispatch, 2}, {E::Exit, 2}};
    CHECK(transitions(ref) == expect);
    CHECK(ref.allTerminated());
    CHECK(finishOf(ref, 3) == 6 && finishOf(ref, 2) == 8);
    bool allOk = true;
    for (const LcSnapshot& s : ref.trace()) allOk = allOk && invariantsHold(s);
    CHECK(allOk);
    // While P2 waits, P3 is running (the CPU is not wasted).
    bool overlap = false;
    for (const LcSnapshot& s : ref.trace())
        if (s.running == 3 && std::count(s.waitQueue.begin(), s.waitQueue.end(), 2)) overlap = true;
    CHECK(overlap);
    // The I/O completion puts P2 into Ready (never directly Running).
    for (const LcSnapshot& s : ref.trace())
        if (s.event == E::IOComplete) CHECK(s.from == ProcState::Waiting && s.to == ProcState::Ready);

    // Consistency with the CPU scheduler: CPU-only workloads give the same completion times.
    std::vector<LcProcSpec> cpuOnly = {{2, 1, "a", 0, 3, {5}, 0}, {3, 1, "b", 1, 1, {3}, 0},
                                       {4, 1, "c", 2, 2, {4}, 0}, {5, 1, "d", 6, 1, {2}, 0}};
    for (Algo a : allAlgos()) {
        LifecycleSim sim(cpuOnly, a, 2);
        sim.runAuto();
        std::vector<PCB> pcbs;
        for (const LcProcSpec& s : cpuOnly) {
            PCB p; p.pid = s.pid; p.arrival = s.arrival; p.burst = s.bursts[0]; p.priority = s.priority;
            pcbs.push_back(p);
        }
        SchedResult r = runScheduler(a, pcbs, 2);
        bool same = true;
        for (const ProcMetrics& m : r.metrics) same = same && finishOf(sim, m.pid) == m.completion;
        CHECK(same);
        bool inv = true;
        for (const LcSnapshot& s : sim.trace()) inv = inv && invariantsHold(s);
        CHECK(inv);
    }

    // Round robin preset: preemptions happen, each is Running -> Ready.
    LifecycleSim rr(presets[1].procs, presets[1].policy, presets[1].quantum);
    rr.runAuto();
    int preempts = 0;
    for (const LcSnapshot& s : rr.trace())
        if (s.event == E::Preempt) { ++preempts; CHECK(s.from == ProcState::Running && s.to == ProcState::Ready); }
    CHECK(preempts >= 2);
    CHECK(rr.allTerminated());

    // Edge: CPU idle while every process waits.
    LifecycleSim idle(presets[2].procs, presets[2].policy, presets[2].quantum);
    idle.runAuto();
    bool idleAllBlocked = false;
    for (const LcSnapshot& s : idle.trace())
        if (s.event == E::Idle && s.running == -1 && s.waitQueue.size() == 2) idleAllBlocked = true;
    CHECK(idleAllBlocked);
    CHECK(idle.allTerminated());

    // Process tree preset: parent links preserved; finishes.
    LifecycleSim tree(presets[3].procs, presets[3].policy, presets[3].quantum);
    tree.runAuto();
    CHECK(tree.allTerminated());
    std::set<std::pair<int, int>> links;
    for (const LcPCB& p : tree.current().pcbs) links.insert({p.pid, p.ppid});
    CHECK(links.count({3, 2}) && links.count({4, 2}) && links.count({5, 4}) && links.count({2, 1}));

    // Manual mode: validation of every transition.
    LifecycleSim m({{2, 1, "x", 0, 1, {3}, 0}, {3, 1, "y", 0, 1, {3}, 0}}, Algo::FCFS, 2, true);
    CHECK(m.dispatch(2) != "");                       // New -> Running is not allowed
    CHECK(m.admit(2) == "" && m.admit(3) == "");
    CHECK(m.admit(2) != "");                          // already Ready
    CHECK(m.dispatch(2) == "");
    CHECK(m.dispatch(3) != "");                       // CPU busy (one CPU)
    CHECK(m.completeIO(3) != "");                     // Ready process has no I/O
    CHECK(m.kill(2) != "");                           // running: must exit, not kill
    CHECK(m.tick() == "");
    CHECK(m.blockIO(2) == "");                        // P2 Running -> Waiting
    CHECK(m.dispatch(2) != "");                       // Waiting -> Running is forbidden
    CHECK(m.dispatch(3) == "");
    CHECK(m.preempt() == "");                         // P3 Running -> Ready
    CHECK(m.completeIO(2) == "");                     // Waiting -> Ready
    CHECK(m.current().pcbs[0].burstLeft == 2);        // P2 kept its remaining CPU work (3 - 1)
    CHECK(m.kill(3) == "");                           // Ready -> Terminated (labelled kill)
    CHECK(m.current().event == E::Kill && m.current().pcbs[1].killed);
    CHECK(m.dispatch(2) == "" && m.exitRunning() == "");
    CHECK(m.allTerminated());
    CHECK(m.exitRunning() != "" && m.preempt() != "" && m.blockIO(1) != "");
    LcProcSpec child{9, 2, "child", 0, 1, {1}, 0};
    CHECK(m.create(child) != "");                     // parent P2 has terminated
    bool inv = true;
    for (const LcSnapshot& s : m.trace()) inv = inv && invariantsHold(s);
    CHECK(inv);

    // Stepping back and issuing a different command discards the future.
    LifecycleSim b({{2, 1, "x", 0, 1, {3}, 0}}, Algo::FCFS, 2, true);
    b.admit(2);
    b.dispatch(2);
    b.tick();
    int before = (int)b.trace().size();
    b.truncate(2);                                    // back to "admitted"
    CHECK((int)b.trace().size() == 3 && b.trace().size() < (size_t)before);
    CHECK(b.current().pcbs[0].state == ProcState::Ready && b.current().running == -1);
    CHECK(b.kill(2) == "");                           // a new branch

    // Determinism: a reset (new object) gives the identical trace.
    LifecycleSim r1(presets[3].procs, presets[3].policy, 2), r2(presets[3].procs, presets[3].policy, 2);
    r1.runAuto();
    r2.runAuto();
    bool same = r1.trace().size() == r2.trace().size();
    for (size_t i = 0; same && i < r1.trace().size(); ++i) same = r1.trace()[i].text == r2.trace()[i].text;
    CHECK(same);

    // Validation
    CHECK_THROWS(LifecycleSim({{2, 1, "x", 0, 1, {3, 2}, 0}}, Algo::FCFS, 2), std::invalid_argument);   // even bursts
    CHECK_THROWS(LifecycleSim({{2, 1, "x", 0, 1, {0}, 0}}, Algo::FCFS, 2), std::invalid_argument);
    CHECK_THROWS(LifecycleSim({{2, 1, "x", 0, 1, {1}, 0}, {2, 1, "y", 0, 1, {1}, 0}}, Algo::FCFS, 2), std::invalid_argument);
    CHECK_THROWS(LifecycleSim({{2, 7, "x", 0, 1, {1}, 0}}, Algo::FCFS, 2), std::invalid_argument);   // unknown parent
    CHECK_THROWS(LifecycleSim({{2, 1, "x", 0, 1, {1}, 0}}, Algo::RR, 0), std::invalid_argument);

    // Sample file
    std::vector<LcProcSpec> f = loadLifecycleCsv(samplePath("lifecycle_reference.csv"));
    CHECK(f.size() == 2 && f[0].bursts == std::vector<int>({2, 3, 2}));
    CHECK_THROWS(loadLifecycleCsv(samplePath("banker_sample.txt")), std::runtime_error);
    std::string twoState = twoStateName(ProcState::Waiting);
    CHECK(twoState == "Not Running");
    return report("test_procmgmt");
}
