// test_rtsched.cpp - RM / EDF real-time scheduling (core/rtsched.h): hand-checked traces,
// periodic releases, preemption, deadline equality, overload, ties, idle, no duplicate execution,
// horizon handling, and the specification example A(2,5) B(4,7) horizon 35.
#include <cstdio>
#include "check.h"
#include "rtsched.h"

static RtConfig make(std::vector<RtTask> t, int h, RtAlgo a, MissPolicy p = MissPolicy::Continue) {
    RtConfig c;
    c.tasks = t; c.horizon = h; c.algo = a; c.policy = p;
    return c;
}
static int slotTask(const RtResult& r, int t) { return r.slotJob[t] < 0 ? -1 : r.jobs[r.slotJob[t]].task; }
static bool noDuplicateExecution(const RtResult& r) {
    std::vector<int> ran(r.jobs.size(), 0);
    for (int j : r.slotJob) if (j >= 0) ++ran[j];
    for (size_t j = 0; j < r.jobs.size(); ++j) {
        int C = r.cfg.tasks[r.jobs[j].task].C;
        if (ran[j] > C || ran[j] != C - r.jobs[j].remaining) return false;
        if (r.jobs[j].finish >= 0 && ran[j] != C) return false;
    }
    return true;
}

static void testSpecExample() {
    std::vector<RtTask> ts = {{"A", 2, 5, 5, 0}, {"B", 4, 7, 7, 0}};
    RtResult rm = runRt(make(ts, 35, RtAlgo::RM));
    RtResult edf = runRt(make(ts, 35, RtAlgo::EDF));
    CHECK_NEAR(rm.utilization, 34.0 / 35.0);
    // RM by hand: A 0-2, B 2-5, A preempts at 5 (A#1 released, shorter period), A 5-7, B#0 still needs 1 at t=7 -> miss
    CHECK(slotTask(rm, 0) == 0 && slotTask(rm, 1) == 0 && slotTask(rm, 2) == 1 && slotTask(rm, 4) == 1 && slotTask(rm, 5) == 0 && slotTask(rm, 6) == 0);
    CHECK(rm.misses >= 1 && rm.firstMiss == 7 && rm.stats[1].missed >= 1 && rm.stats[0].missed == 0);
    bool preemptAt5 = false;
    for (const RtEvent& e : rm.events) if (e.kind == RtEventKind::Preempt && e.time == 5 && e.task == 1) preemptAt5 = true;
    CHECK(preemptAt5);
    CHECK(rm.jobs[1].task == 1 && rm.jobs[1].finish == 8 && rm.jobs[1].missed);          // B#0 late, continues, done at 8
    // EDF by hand: at 5 B#0 (deadline 7) beats A#1 (deadline 10): B 5-6, no miss over the whole horizon
    CHECK(edf.misses == 0 && slotTask(edf, 5) == 1 && slotTask(edf, 6) == 0);
    CHECK(edf.jobs[1].finish == 6);
    CHECK(noDuplicateExecution(rm) && noDuplicateExecution(edf));
    CHECK(rm.stats[0].released == 7 && rm.stats[1].released == 5);                     // releases 0,5..30 and 0,7..28
    RtAnalysis a = analyseRt(make(ts, 35, RtAlgo::RM));
    CHECK(!a.rmGuaranteed && a.edfFeasible && a.implicitDeadlines);
    CHECK_NEAR(a.rmBound, 2 * (std::sqrt(2.0) - 1));
}

static void testSemantics() {
    // completing exactly at the deadline is on time
    RtResult eq = runRt(make({{"E", 2, 2, 2, 0}}, 10, RtAlgo::EDF));
    CHECK(eq.misses == 0 && eq.idleSlots == 0 && eq.stats[0].completed == 5);
    for (const RtJob& j : eq.jobs) CHECK(j.finish == j.deadline);
    // idle intervals and offsets
    RtResult idle = runRt(make({{"I", 1, 4, 4, 2}}, 12, RtAlgo::RM));
    CHECK(idle.idleSlots == 12 - 3 && slotTask(idle, 0) == -1 && slotTask(idle, 2) == 0 && slotTask(idle, 6) == 0 && slotTask(idle, 10) == 0);
    CHECK(idle.stats[0].released == 3);
    // RM tie: equal periods -> lower task index first
    RtResult tie = runRt(make({{"P", 1, 4, 4, 0}, {"Q", 1, 4, 4, 0}}, 4, RtAlgo::RM));
    CHECK(slotTask(tie, 0) == 0 && slotTask(tie, 1) == 1);
    // EDF tie: equal deadlines -> earlier release, then lower index
    RtResult et = runRt(make({{"P", 1, 6, 6, 0}, {"Q", 1, 3, 3, 0}}, 6, RtAlgo::EDF));
    CHECK(slotTask(et, 0) == 1 && slotTask(et, 1) == 0);
    RtResult et2 = runRt(make({{"P", 1, 6, 6, 0}, {"Q", 1, 6, 6, 0}}, 6, RtAlgo::EDF));
    CHECK(slotTask(et2, 0) == 0 && slotTask(et2, 1) == 1);
    // horizon: an unfinished job whose deadline lies beyond the horizon is not a miss
    RtResult hz = runRt(make({{"H", 3, 10, 10, 0}}, 2, RtAlgo::RM));
    CHECK(hz.misses == 0 && hz.stats[0].incomplete == 1);
    // a deadline exactly at the horizon is still checked
    RtResult hz2 = runRt(make({{"A", 2, 3, 3, 0}, {"B", 2, 3, 3, 0}}, 3, RtAlgo::RM));
    CHECK(hz2.misses == 1 && hz2.firstMiss == 3);
}

static void testOverload() {
    std::vector<RtTask> ts = {{"X", 3, 5, 5, 0}, {"Y", 4, 7, 7, 0}};
    for (RtAlgo a : {RtAlgo::RM, RtAlgo::EDF}) {
        RtResult c = runRt(make(ts, 35, a, MissPolicy::Continue));
        RtResult d = runRt(make(ts, 35, a, MissPolicy::Abort));
        CHECK(c.misses > 0 && d.misses > 0);
        int perTask = 0, dropped = 0;
        for (const RtTaskStats& s : d.stats) { perTask += s.missed; dropped += s.dropped; }
        CHECK(perTask == d.misses && dropped == d.misses);                                // counted once, every miss aborted
        for (const RtJob& j : d.jobs) CHECK(!(j.dropped && j.finish >= 0));
        int missEvents = 0;
        for (const RtEvent& e : c.events) if (e.kind == RtEventKind::DeadlineMiss) ++missEvents;
        CHECK(missEvents == c.misses);                                                     // no double counting of late jobs
        CHECK(noDuplicateExecution(c) && noDuplicateExecution(d));
        CHECK(c.idleSlots == 0);                                                           // overloaded CPU never idles
    }
    RtAnalysis a = analyseRt(make(ts, 35, RtAlgo::EDF));
    CHECK(!a.edfFeasible && a.U > 1);
}

static void testPresetsAndValidation() {
    for (const RtPreset& p : rtPresets())
        for (RtAlgo a : {RtAlgo::RM, RtAlgo::EDF}) {
            RtConfig c = p.cfg;
            c.algo = a;
            RtResult r = runRt(c);
            CHECK(noDuplicateExecution(r) && (int)r.slotJob.size() == c.horizon);
        }
    RtConfig f = rtPresets()[1].cfg;
    CHECK(analyseRt(f).rmGuaranteed && runRt(f).misses == 0);
    RtConfig m = rtPresets()[3].cfg;
    CHECK(runRt(m).misses == 0);
    m.algo = RtAlgo::EDF;
    CHECK(runRt(m).misses == 0);
    CHECK(!analyseRt(rtPresets()[4].cfg).implicitDeadlines);
    CHECK_THROWS(runRt(make({{"Z", 3, 4, 5, 0}}, 10, RtAlgo::RM)), std::invalid_argument);   // D > T
    CHECK_THROWS(runRt(make({{"Z", 5, 8, 4, 0}}, 10, RtAlgo::RM)), std::invalid_argument);   // C > D
    CHECK_THROWS(runRt(make({{"Z", 1, 4, 4, 0}, {"Z", 1, 4, 4, 0}}, 10, RtAlgo::RM)), std::invalid_argument);
    CHECK_THROWS(runRt(make({{"Z", 0, 4, 4, 0}}, 10, RtAlgo::RM)), std::invalid_argument);
    CHECK_THROWS(runRt(make({}, 10, RtAlgo::RM)), std::invalid_argument);
    CHECK_THROWS(runRt(make({{"Z", 1, 4, 4, -1}}, 10, RtAlgo::RM)), std::invalid_argument);
}

int main() {
    testSpecExample();
    testSemantics();
    testOverload();
    testPresetsAndValidation();
    return report("test_rtsched");
}
