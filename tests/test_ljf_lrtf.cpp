// test_ljf_lrtf.cpp - Longest Job First (non-preemptive) and Longest Remaining
// Time First (preemptive). Every expected value below was derived by hand,
// tick by tick (see docs/REPORT.md "LJF and LRTF" for the traces).
// Tie rules: LJF  same burst -> earlier arrival -> lower pid.
//            LRTF a ready process preempts only with STRICTLY more remaining time;
//                 when the CPU is free, same remaining -> earlier arrival -> lower pid.
#include "check.h"
#include "engine.h"
#include "integrated.h"
#include "io_formats.h"
#include "scheduler.h"
#include <stdexcept>

static PCB P(int pid, int arr, int burst, int prio = 0) {
    PCB p; p.pid = pid; p.arrival = arr; p.burst = burst; p.priority = prio; return p;
}
static const ProcMetrics& M(const SchedResult& r, int pid) {
    for (const auto& m : r.metrics) if (m.pid == pid) return m;
    static ProcMetrics none{}; return none;
}
static bool seg(const Segment& s, int pid, int a, int b) { return s.pid == pid && s.start == a && s.end == b; }
// Checks the whole timeline against {pid, start, end} triples.
static bool timelineIs(const SchedResult& r, const std::vector<Segment>& want) {
    if (r.timeline.size() != want.size()) return false;
    for (size_t i = 0; i < want.size(); ++i)
        if (!seg(r.timeline[i], want[i].pid, want[i].start, want[i].end)) return false;
    return true;
}

int main() {
    // ---- names, ids, inventory
    CHECK(std::string(algoName(Algo::LJF)) == "LJF" && std::string(algoName(Algo::LRTF)) == "LRTF");
    CHECK(allAlgos().size() == 8);
    CHECK(allAlgos()[3] == Algo::LJF && allAlgos()[4] == Algo::LRTF);
    CHECK((int)Algo::FCFS == 0 && (int)Algo::PRIORITY_P == 5);        // old enum values unchanged
    Algo parsed = Algo::FCFS;
    CHECK(algoFromId("LJF", parsed) && parsed == Algo::LJF);
    CHECK(algoFromId("lrtf", parsed) && parsed == Algo::LRTF);
    CHECK(!algoFromId("lifo", parsed));
    CHECK(!isPreemptive(Algo::LJF) && isPreemptive(Algo::LRTF));

    // ---- LJF, simultaneous arrivals with an equal-burst tie (P2 and P4 both 6 -> lower pid first)
    std::vector<PCB> simul = {P(1, 0, 3), P(2, 0, 6), P(3, 0, 4), P(4, 0, 6)};
    SchedResult r = runScheduler(Algo::LJF, simul);
    CHECK(timelineIs(r, {{2, 0, 6}, {4, 6, 12}, {3, 12, 16}, {1, 16, 19}}));
    CHECK(M(r, 1).completion == 19 && M(r, 2).completion == 6 && M(r, 3).completion == 16 && M(r, 4).completion == 12);
    CHECK(M(r, 1).waiting == 16 && M(r, 2).waiting == 0 && M(r, 3).waiting == 12 && M(r, 4).waiting == 6);
    CHECK_NEAR(r.avgWaiting, 8.5);
    CHECK_NEAR(r.avgTurnaround, 13.25);
    CHECK_NEAR(r.avgResponse, 8.5);                    // non-preemptive, all arrive at 0: RT == WT
    CHECK(r.contextSwitches == 3);

    // ---- LJF, different arrival times (the SRTF textbook input)
    std::vector<PCB> srtf = loadProcessesCsv(samplePath("processes_ljf_lrtf.csv"));
    CHECK(srtf.size() == 4);
    r = runScheduler(Algo::LJF, srtf);
    CHECK(timelineIs(r, {{1, 0, 8}, {3, 8, 17}, {4, 17, 22}, {2, 22, 26}}));   // P1 alone at t=0 is not preempted
    CHECK(M(r, 1).waiting == 0 && M(r, 2).waiting == 21 && M(r, 3).waiting == 6 && M(r, 4).waiting == 14);
    CHECK(M(r, 2).turnaround == 25 && M(r, 3).response == 6);
    CHECK_NEAR(r.avgWaiting, 10.25);
    CHECK_NEAR(r.avgTurnaround, 16.75);
    CHECK(r.contextSwitches == 3);

    // ---- LJF tie by arrival: equal bursts, the earlier arrival wins even with a higher pid
    r = runScheduler(Algo::LJF, {P(1, 0, 2), P(5, 1, 4), P(3, 2, 4)});
    CHECK(timelineIs(r, {{1, 0, 2}, {5, 2, 6}, {3, 6, 10}}));

    // ---- LJF starvation: the 1-tick job waits for every longer job
    r = runScheduler(Algo::LJF, loadProcessesCsv(samplePath("processes_ljf_starvation.csv")));
    CHECK(timelineIs(r, {{1, 0, 10}, {3, 10, 19}, {4, 19, 27}, {2, 27, 28}}));
    CHECK(M(r, 2).waiting == 26);
    SchedResult sjfStarve = runScheduler(Algo::SJF, loadProcessesCsv(samplePath("processes_ljf_starvation.csv")));
    CHECK(M(sjfStarve, 2).waiting == 9);               // SJF serves it right after P1

    // ---- idle gap (both algorithms)
    for (Algo a : {Algo::LJF, Algo::LRTF}) {
        r = runScheduler(a, {P(1, 0, 2), P(2, 5, 3)});
        CHECK(timelineIs(r, {{1, 0, 2}, {-1, 2, 5}, {2, 5, 8}}));
        CHECK(r.contextSwitches == 1);
        CHECK_NEAR(cpuUtilization(r), 62.5);
    }

    // ---- LRTF basic preemption: P2 (6) takes the CPU from P1 (2 left) at t=1
    r = runScheduler(Algo::LRTF, {P(1, 0, 3), P(2, 1, 6)});
    CHECK(timelineIs(r, {{1, 0, 1}, {2, 1, 6}, {1, 6, 8}, {2, 8, 9}}));
    CHECK(M(r, 1).completion == 8 && M(r, 2).completion == 9);
    CHECK(M(r, 1).waiting == 5 && M(r, 2).waiting == 2);
    CHECK(M(r, 1).response == 0 && M(r, 2).response == 0);
    CHECK(r.contextSwitches == 3);

    // ---- LRTF equal remaining: the running process keeps the CPU
    // t0 P1 (tie -> lower pid); t1 P2 has 2 > 1 -> preempt; t2 both 1 -> P2 continues (no switch).
    r = runScheduler(Algo::LRTF, {P(1, 0, 2), P(2, 0, 2)});
    CHECK(timelineIs(r, {{1, 0, 1}, {2, 1, 3}, {1, 3, 4}}));
    CHECK(r.contextSwitches == 2);
    // sample file processes_ljf_ties.csv: P1 3, P2 6, P3 4, P4 6, all at t=0 (hand trace, 14 segments)
    r = runScheduler(Algo::LRTF, loadProcessesCsv(samplePath("processes_ljf_ties.csv")));
    CHECK(timelineIs(r, {{2, 0, 1}, {4, 1, 3}, {2, 3, 5}, {3, 5, 6}, {4, 6, 8}, {1, 8, 9}, {2, 9, 10}, {3, 10, 12},
                         {1, 12, 13}, {2, 13, 14}, {4, 14, 16}, {1, 16, 17}, {2, 17, 18}, {3, 18, 19}}));
    CHECK(r.contextSwitches == 13);
    CHECK_NEAR(r.avgWaiting, 12.75);
    // equal bursts arriving together, idle CPU: earlier arrival, then lower pid
    r = runScheduler(Algo::LRTF, {P(7, 0, 1), P(3, 0, 1), P(9, 0, 1)});
    CHECK(timelineIs(r, {{3, 0, 1}, {7, 1, 2}, {9, 2, 3}}));

    // ---- LRTF different arrivals (hand trace, 17 segments)
    r = runScheduler(Algo::LRTF, srtf);
    CHECK(timelineIs(r, {{1, 0, 2}, {3, 2, 6}, {1, 6, 8}, {3, 8, 9}, {4, 9, 11}, {1, 11, 12}, {2, 12, 13},
                         {3, 13, 15}, {1, 15, 16}, {2, 16, 17}, {4, 17, 19}, {1, 19, 20}, {2, 20, 21},
                         {3, 21, 23}, {1, 23, 24}, {2, 24, 25}, {4, 25, 26}}));
    CHECK(M(r, 1).completion == 24 && M(r, 2).completion == 25 && M(r, 3).completion == 23 && M(r, 4).completion == 26);
    CHECK(M(r, 1).waiting == 16 && M(r, 2).waiting == 20 && M(r, 3).waiting == 12 && M(r, 4).waiting == 18);
    CHECK(M(r, 1).response == 0 && M(r, 2).response == 11 && M(r, 3).response == 0 && M(r, 4).response == 6);
    CHECK_NEAR(r.avgWaiting, 16.5);
    CHECK_NEAR(r.avgTurnaround, 23.0);
    CHECK_NEAR(r.avgResponse, 4.25);
    CHECK(r.contextSwitches == 16);

    // ---- determinism: same input, same result
    SchedResult again = runScheduler(Algo::LRTF, srtf);
    CHECK(timelineIs(again, r.timeline) && again.contextSwitches == r.contextSwitches);

    // ---- validation (shared rules)
    CHECK_THROWS(runScheduler(Algo::LJF, {P(1, 0, 0)}), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::LRTF, {P(1, 0, 3), P(1, 1, 2)}), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::LRTF, {P(1, -2, 3)}), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::LJF, {}), std::invalid_argument);
    {   // quantum is irrelevant for LJF/LRTF (only RR validates it)
        bool ok = true;
        try { runScheduler(Algo::LRTF, {P(1, 0, 3)}, 0); } catch (...) { ok = false; }
        CHECK(ok);
    }

    // ---- Compare all includes LJF and LRTF
    std::vector<SchedResult> all = runAll(srtf, 4);
    CHECK(all.size() == 8);
    CHECK(all[3].algo == Algo::LJF && all[4].algo == Algo::LRTF);
    CHECK_NEAR(all[3].avgWaiting, 10.25);
    CHECK_NEAR(all[4].avgWaiting, 16.5);
    CHECK_NEAR(all[2].avgWaiting, 6.5);                // SRTF unchanged

    // ---- animated replay (threaded engine) gives the core completion times
    for (Algo a : {Algo::LJF, Algo::LRTF}) {
        SchedResult rs = runScheduler(a, srtf);
        SimulationEngine eng(srtf, rs.timeline, EngineOptions{0, false});
        eng.start();
        eng.join();
        bool same = true;
        for (const PCB& p : eng.pcbs())
            for (const ProcMetrics& m : rs.metrics)
                if (m.pid == p.pid) same = same && p.finish == m.completion && p.remaining == 0;
        CHECK(same);
    }

    // ---- integrated run: without resource requests it reproduces the plain schedule
    for (Algo a : {Algo::LJF, Algo::LRTF}) {
        IntegratedConfig cfg;
        cfg.algo = a;
        cfg.totalRes = {1};
        IntegratedResult ir = runIntegrated(srtf, cfg);
        SchedResult rs = runScheduler(a, srtf);
        CHECK(!ir.stalled && ir.memFaults + ir.memHits == 26);
        bool same = ir.metrics.size() == rs.metrics.size();
        for (size_t i = 0; same && i < rs.metrics.size(); ++i)
            same = ir.metrics[i].completion == rs.metrics[i].completion;
        CHECK(same);
    }

    return report("test_ljf_lrtf");
}
