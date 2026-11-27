// rtsched.cpp - RM / EDF real-time scheduling simulator (see rtsched.h).
#include "rtsched.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>

const char* rtAlgoName(RtAlgo a) { return a == RtAlgo::RM ? "Rate Monotonic (RM)" : "Earliest Deadline First (EDF)"; }
const char* missPolicyName(MissPolicy p) { return p == MissPolicy::Continue ? "late job continues" : "late job is aborted"; }
const char* rtEventName(RtEventKind k) {
    switch (k) {
        case RtEventKind::Release: return "release";
        case RtEventKind::Dispatch: return "dispatch";
        case RtEventKind::Preempt: return "preempt";
        case RtEventKind::Complete: return "complete";
        case RtEventKind::DeadlineMiss: return "DEADLINE MISS";
        case RtEventKind::Drop: return "abort";
        case RtEventKind::Idle: return "idle";
        case RtEventKind::Incomplete: return "incomplete at horizon";
    }
    return "?";
}

void validateRtConfig(const RtConfig& c) {
    if (c.tasks.empty() || c.tasks.size() > 8) throw std::invalid_argument("1 to 8 tasks");
    if (c.horizon < 1 || c.horizon > 1000) throw std::invalid_argument("horizon must be 1..1000 time units");
    std::set<std::string> names;
    for (const RtTask& t : c.tasks) {
        std::string n = "task '" + t.name + "': ";
        if (t.name.empty()) throw std::invalid_argument("every task needs a name");
        if (!names.insert(t.name).second) throw std::invalid_argument("duplicate task name '" + t.name + "'");
        if (t.C <= 0 || t.T <= 0 || t.D <= 0) throw std::invalid_argument(n + "C, T and D must be positive");
        if (t.offset < 0) throw std::invalid_argument(n + "offset must be >= 0");
        if (t.C > t.D) throw std::invalid_argument(n + "C > D: no job could ever meet its deadline");
        if (t.D > t.T) throw std::invalid_argument(n + "D > T is not supported (constrained deadlines D <= T are required)");
    }
}

double rmUtilizationBound(int n) { return n <= 0 ? 0 : n * (std::pow(2.0, 1.0 / n) - 1.0); }

static std::string fmt(const char* f, double a, double b = 0) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, a, b);
    return buf;
}

RtResult runRt(const RtConfig& cfg) {
    validateRtConfig(cfg);
    RtResult r;
    r.cfg = cfg;
    int n = (int)cfg.tasks.size();
    r.stats.assign(n, RtTaskStats());
    for (const RtTask& t : cfg.tasks) r.utilization += double(t.C) / t.T;
    std::vector<int> active;                     // indices into r.jobs (released, not finished, not dropped)
    std::vector<int> jobCount(n, 0);
    int prev = -1;                               // job that ran in the previous slot
    auto ev = [&](int time, RtEventKind k, int job, const std::string& text) {
        RtEvent e;
        e.time = time;
        e.kind = k;
        e.job = job;
        e.task = job >= 0 ? r.jobs[job].task : -1;
        e.text = text;
        r.events.push_back(e);
    };
    auto jname = [&](int j) { return cfg.tasks[r.jobs[j].task].name + "#" + std::to_string(r.jobs[j].index); };
    auto better = [&](int a, int b) {           // true if job a has higher priority than job b
        const RtJob &A = r.jobs[a], &B = r.jobs[b];
        if (cfg.algo == RtAlgo::RM) {
            int ta = cfg.tasks[A.task].T, tb = cfg.tasks[B.task].T;
            if (ta != tb) return ta < tb;
            if (A.task != B.task) return A.task < B.task;
            return A.release < B.release;
        }
        if (A.deadline != B.deadline) return A.deadline < B.deadline;
        if (A.release != B.release) return A.release < B.release;
        return A.task < B.task;
    };
    for (int t = 0; t <= cfg.horizon; ++t) {
        // (2) deadline checks at instant t (completions of [t-1, t) were applied in the previous slot)
        for (size_t i = 0; i < active.size();) {
            RtJob& j = r.jobs[active[i]];
            if (j.deadline == t && j.remaining > 0 && !j.missed) {
                j.missed = true;
                ++r.misses;
                ++r.stats[j.task].missed;
                if (r.firstMiss < 0) r.firstMiss = t;
                ev(t, RtEventKind::DeadlineMiss, active[i], jname(active[i]) + " misses its deadline at t=" + std::to_string(t) + " with " +
                                                                std::to_string(j.remaining) + " unit(s) left");
                if (cfg.policy == MissPolicy::Abort) {
                    j.dropped = true;
                    ++r.stats[j.task].dropped;
                    ev(t, RtEventKind::Drop, active[i], jname(active[i]) + " is aborted (policy: abort late jobs)");
                    if (prev == active[i]) prev = -1;
                    active.erase(active.begin() + i);
                    continue;
                }
            }
            ++i;
        }
        if (t == cfg.horizon) break;
        // (3) releases at t
        for (int k = 0; k < n; ++k) {
            const RtTask& task = cfg.tasks[k];
            if (t < task.offset || (t - task.offset) % task.T) continue;
            RtJob j;
            j.task = k;
            j.index = jobCount[k]++;
            j.release = t;
            j.deadline = t + task.D;
            j.remaining = task.C;
            r.jobs.push_back(j);
            active.push_back((int)r.jobs.size() - 1);
            ++r.stats[k].released;
            ev(t, RtEventKind::Release, (int)r.jobs.size() - 1, jname((int)r.jobs.size() - 1) + " released, absolute deadline " + std::to_string(j.deadline));
        }
        // (4) dispatch for the slot [t, t+1)
        int run = -1;
        for (int a : active) if (run < 0 || better(a, run)) run = a;
        if (run != prev) {
            if (prev >= 0 && r.jobs[prev].remaining > 0 && !r.jobs[prev].dropped) {
                ++r.preemptions;
                ev(t, RtEventKind::Preempt, prev, jname(prev) + " is preempted by " + jname(run) + " (" +
                                                      (cfg.algo == RtAlgo::RM ? "shorter period" : "earlier deadline") + ")");
            }
            if (run >= 0) {
                ++r.contextSwitches;            // dispatches; the first one is subtracted below
                ev(t, RtEventKind::Dispatch, run, jname(run) + " dispatched" + (cfg.algo == RtAlgo::EDF ? " (deadline " + std::to_string(r.jobs[run].deadline) + ")"
                                                                                                         : " (period " + std::to_string(cfg.tasks[r.jobs[run].task].T) + ")"));
            } else ev(t, RtEventKind::Idle, -1, "CPU idle: no ready job");
        }
        r.slotJob.push_back(run);
        int task = run >= 0 ? r.jobs[run].task : -1;
        if (!r.timeline.empty() && r.timeline.back().task == task && r.timeline.back().job == run && r.timeline.back().end == t) r.timeline.back().end = t + 1;
        else r.timeline.push_back(RtSlice{t, t + 1, task, run});
        if (run < 0) { ++r.idleSlots; prev = -1; continue; }
        RtJob& j = r.jobs[run];
        if (j.start < 0) j.start = t;
        if (--j.remaining == 0) {                // (1) completion at t+1 (before the deadline check at t+1)
            j.finish = t + 1;
            ++r.stats[j.task].completed;
            std::string late = j.finish > j.deadline ? " - LATE by " + std::to_string(j.finish - j.deadline) : j.finish == j.deadline ? " exactly at its deadline (on time)" : "";
            ev(t + 1, RtEventKind::Complete, run, jname(run) + " completes, response time " + std::to_string(j.responseTime()) + late);
            for (size_t i = 0; i < active.size(); ++i) if (active[i] == run) { active.erase(active.begin() + i); break; }
            prev = -1;
        } else prev = run;
    }
    r.contextSwitches = std::max(0, r.contextSwitches - 1);
    for (int a : active) {
        RtJob& j = r.jobs[a];
        if (j.deadline > cfg.horizon) {
            ++r.stats[j.task].incomplete;
            ev(cfg.horizon, RtEventKind::Incomplete, a, jname(a) + " not finished at the horizon (deadline " + std::to_string(j.deadline) + " lies beyond it)");
        }
    }
    for (int k = 0; k < n; ++k) {
        RtTaskStats& s = r.stats[k];
        double sum = 0;
        int cnt = 0;
        for (const RtJob& j : r.jobs) {
            if (j.task != k || j.finish < 0) continue;
            sum += j.responseTime();
            ++cnt;
            s.worstResponse = std::max(s.worstResponse, j.responseTime());
            s.maxLateness = std::max(s.maxLateness, j.finish - j.deadline);
        }
        s.avgResponse = cnt ? sum / cnt : 0;
    }
    return r;
}

RtAnalysis analyseRt(const RtConfig& c) {
    RtAnalysis a;
    int n = (int)c.tasks.size();
    for (const RtTask& t : c.tasks) {
        a.U += double(t.C) / t.T;
        if (t.D != t.T) a.implicitDeadlines = false;
    }
    a.rmBound = rmUtilizationBound(n);
    a.rmGuaranteed = a.implicitDeadlines && a.U <= a.rmBound + 1e-12;
    a.edfFeasible = a.implicitDeadlines && a.U <= 1.0 + 1e-12;
    std::string s = "U = sum(C/T) = " + fmt("%.4f (%.2f %%)", a.U, a.U * 100) + ". ";
    if (!a.implicitDeadlines) {
        s += "Some task has D < T: the classical utilization tests below assume implicit deadlines (D = T) and do not apply; "
             "only the simulated trace is shown (observed behaviour over the horizon, not a proof).";
        a.text = s;
        return a;
    }
    s += "RM bound n(2^(1/n)-1) for n=" + std::to_string(n) + ": " + fmt("%.4f", a.rmBound) + ". ";
    s += a.rmGuaranteed ? "U <= bound: RM is GUARANTEED to meet all deadlines. "
                        : "U > bound: the RM test is inconclusive (the bound is sufficient, not necessary) - the trace shows what happens. ";
    s += a.edfFeasible ? "U <= 1: EDF meets all deadlines (classical result). "
                       : "U > 1: the CPU is overloaded - no algorithm can meet every deadline. ";
    s += "Assumptions: independent periodic preemptible tasks, one CPU, D = T, zero overhead, no blocking.";
    a.text = s;
    return a;
}

std::vector<RtPreset> rtPresets() {
    auto cfg = [](std::vector<RtTask> t, int h) { RtConfig c; c.tasks = t; c.horizon = h; return c; };
    return {
        {"RM misses, EDF does not (A 2/5, B 4/7)",
         "U = 2/5 + 4/7 = 34/35 (97.1 %) > RM bound 0.828: at t=5 A preempts B under RM and B misses its deadline at t=7; EDF runs B "
         "first (deadline 7 < 10) and meets every deadline over the horizon 35.",
         cfg({{"A", 2, 5, 5, 0}, {"B", 4, 7, 7, 0}}, 35)},
        {"Feasible under both (U = 0.70)", "Three tasks below the RM bound (0.780 for n=3): RM is guaranteed, EDF too.",
         cfg({{"T1", 1, 4, 4, 0}, {"T2", 2, 8, 8, 0}, {"T3", 2, 10, 10, 0}}, 40)},
        {"Overload (U = 1.17)", "Utilization above 1: every algorithm misses deadlines; compare which tasks suffer (RM: the long-period task, "
         "EDF: misses spread out - the domino effect).",
         cfg({{"X", 3, 5, 5, 0}, {"Y", 4, 7, 7, 0}}, 35)},
        {"Fictional mission-control workload", "ILLUSTRATIVE, FICTIONAL parameters (not real spacecraft software): navigation every 10, "
         "telemetry every 20, thermal control every 40, communication every 50 time units with staggered offsets. U = 0.75.",
         cfg({{"navigation", 3, 10, 10, 0}, {"telemetry", 4, 20, 20, 2}, {"thermal", 6, 40, 40, 5}, {"comms", 5, 50, 50, 1}}, 100)},
        {"Constrained deadlines (D < T)", "Telemetry must finish 3 units after its release: the utilization tests no longer apply; only the "
         "trace shows the behaviour.",
         cfg({{"sensor", 1, 4, 4, 0}, {"telemetry", 2, 6, 3, 0}, {"logger", 2, 12, 12, 0}}, 24)},
    };
}
