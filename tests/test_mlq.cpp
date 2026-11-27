// test_mlq.cpp - Multilevel Queue / Multilevel Feedback Queue (core/mlq.h): hand-checked traces,
// fixed queue priority, demotion, preemption by a higher queue, priority boost, validation.
#include <algorithm>
#include <cstdio>
#include "check.h"
#include "mlq.h"

static bool seg(const SchedResult& r, size_t i, int pid, int s, int e) {
    return i < r.timeline.size() && r.timeline[i].pid == pid && r.timeline[i].start == s && r.timeline[i].end == e;
}
static const ProcMetrics& m(const SchedResult& r, int pid) {
    for (const ProcMetrics& x : r.metrics) if (x.pid == pid) return x;
    return r.metrics.front();
}

static void testMlq() {
    MlqPreset p = mlqPresets()[0];
    MlqResult r = runMlq(p.cfg, p.procs);
    // hand trace: P1 0-1, P2 1-2 (preempts batch), P3 2-4 (system preempts interactive), P5 4-5, P2 5-7, P4 7-11, P1 11-16
    CHECK(r.sched.timeline.size() == 7);
    CHECK(seg(r.sched, 0, 1, 0, 1) && seg(r.sched, 1, 2, 1, 2) && seg(r.sched, 2, 3, 2, 4) && seg(r.sched, 3, 5, 4, 5));
    CHECK(seg(r.sched, 4, 2, 5, 7) && seg(r.sched, 5, 4, 7, 11) && seg(r.sched, 6, 1, 11, 16));
    CHECK(m(r.sched, 1).completion == 16 && m(r.sched, 1).waiting == 10 && m(r.sched, 3).waiting == 0 && m(r.sched, 4).waiting == 4);
    CHECK_NEAR(r.sched.avgWaiting, 3.4);
    CHECK(r.preemptions == 2 && r.demotions == 0);
    CHECK(r.queueOf[0] == 2 && r.queueOf[2] == 0);                     // fixed queues (priority column)
    // without preemption the running process keeps the CPU until its quantum / completion
    MlqConfig np = p.cfg;
    np.preemptive = false;
    MlqResult n = runMlq(np, p.procs);
    CHECK(n.preemptions == 0 && seg(n.sched, 0, 1, 0, 6));            // batch FCFS job runs to completion
}

static void testMlfq() {
    MlqPreset p = mlqPresets()[1];
    MlqResult r = runMlq(p.cfg, p.procs);
    // hand trace (Q0 q=2, Q1 q=4, Q2 FCFS): P1 0-2 (demoted), P2 2-4, P3 4-6 (demoted), P1 6-9 (preempted by P4),
    // P4 9-11, P3 11-12, P1 12-16 (demoted to Q2), P1 16-19
    CHECK(seg(r.sched, 0, 1, 0, 2) && seg(r.sched, 1, 2, 2, 4) && seg(r.sched, 2, 3, 4, 6) && seg(r.sched, 3, 1, 6, 9));
    CHECK(seg(r.sched, 4, 4, 9, 11) && seg(r.sched, 5, 3, 11, 12) && seg(r.sched, 6, 1, 12, 19));
    CHECK(r.demotions == 3 && r.preemptions == 1);
    CHECK(m(r.sched, 1).completion == 19 && m(r.sched, 2).completion == 4 && m(r.sched, 3).completion == 12 && m(r.sched, 4).completion == 11);
    CHECK(r.queueOf[0] == 2 && r.queueOf[1] == 0);                     // the long job sank, the short one stayed on top
    bool demoted = false;
    for (const MlqTick& t : r.ticks) for (const std::string& e : t.events) if (e.find("demoted") != std::string::npos) demoted = true;
    CHECK(demoted);
    // every tick of the timeline is accounted for and every process ran exactly its burst
    int busy = 0;
    for (const Segment& s : r.sched.timeline) if (s.pid >= 0) busy += s.end - s.start;
    CHECK(busy == 12 + 2 + 3 + 2);
}

static void testBoost() {
    MlqPreset p = mlqPresets()[2];
    MlqResult b = runMlq(p.cfg, p.procs);
    CHECK(b.boosts >= 1);
    bool lifted = false;
    for (const MlqTick& t : b.ticks)
        if (t.t == 12) {
            lifted = t.pid == 1 ? t.queue == 0 : std::find(t.queues[0].begin(), t.queues[0].end(), 1) != t.queues[0].end();
        }
    CHECK(lifted);                                                       // P1 is back in Q0 at the boost
    MlqConfig off = p.cfg;
    off.boostInterval = 0;
    MlqResult nb = runMlq(off, p.procs);
    CHECK(nb.boosts == 0 && m(b.sched, 1).response == 0);
    CHECK(m(b.sched, 1).completion <= m(nb.sched, 1).completion);        // the boost never delays the long job here
    for (const MlqResult* x : {&b, &nb}) {
        int busy = 0;
        for (const Segment& s : x->sched.timeline) if (s.pid >= 0) busy += s.end - s.start;
        CHECK(busy == 10 + 2 * 7);
    }
}

static void testValidation() {
    MlqPreset p = mlqPresets()[1];
    MlqConfig c = p.cfg;
    c.queues[0].quantum = 0;
    CHECK_THROWS(runMlq(c, p.procs), std::invalid_argument);
    c = p.cfg;
    c.queues.assign(5, MlqQueue{"Q", QueuePolicy::RR, 2});
    CHECK_THROWS(runMlq(c, p.procs), std::invalid_argument);
    std::vector<PCB> dup = p.procs;
    dup[1].pid = dup[0].pid;
    CHECK_THROWS(runMlq(p.cfg, dup), std::invalid_argument);
    std::vector<PCB> clamp = mlqPresets()[0].procs;
    clamp[0].priority = 9;                                               // queue index clamped to the last queue
    MlqResult r = runMlq(mlqPresets()[0].cfg, clamp);
    CHECK(r.queueOf[0] == 2);
    // idle gap: arrival after a pause
    PCB a; a.pid = 1; a.arrival = 3; a.burst = 2; a.priority = 0;
    MlqResult g = runMlq(mlqPresets()[1].cfg, {a});
    CHECK(seg(g.sched, 0, -1, 0, 3) && seg(g.sched, 1, 1, 3, 5) && g.sched.metrics[0].waiting == 0);
}

int main() {
    testMlq();
    testMlfq();
    testBoost();
    testValidation();
    return report("test_mlq");
}
