// mlq.cpp - Multilevel Queue and Multilevel Feedback Queue scheduling (see mlq.h).
#include "mlq.h"
#include <algorithm>
#include <deque>
#include <set>
#include <stdexcept>

void validateMlqConfig(const MlqConfig& c, const std::vector<PCB>& procs) {
    if (c.queues.empty() || c.queues.size() > 4) throw std::invalid_argument("1 to 4 queues");
    for (size_t i = 0; i < c.queues.size(); ++i)
        if (c.queues[i].policy == QueuePolicy::RR && (c.queues[i].quantum < 1 || c.queues[i].quantum > 50))
            throw std::invalid_argument("queue " + std::to_string(i) + ": quantum must be 1..50");
    if (c.boostInterval < 0 || c.boostInterval > 1000) throw std::invalid_argument("boost interval must be 0..1000");
    validateProcesses(procs, Algo::FCFS, 1);           // burst > 0, arrival >= 0, unique pids (shared rules)
}

static PCB& byIdx(std::vector<PCB>& v, int i) { return v[i]; }

MlqResult runMlq(const MlqConfig& cfg, std::vector<PCB> procs) {
    validateMlqConfig(cfg, procs);
    MlqResult r;
    r.cfg = cfg;
    int nq = (int)cfg.queues.size(), n = (int)procs.size();
    std::vector<std::deque<int>> q(nq);            // indices into procs
    std::vector<int> level(n, 0), slice(n, 0);
    for (PCB& p : procs) { p.remaining = p.burst; p.state = ProcState::New; p.firstRun = -1; p.finish = -1; }
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return procs[a].pid < procs[b].pid; });
    int running = -1, done = 0, lastPid = -2;
    std::vector<Segment> timeline;
    auto name = [&](int i) { return "P" + std::to_string(procs[i].pid); };
    for (int t = 0; done < n && t < 100000; ++t) {
        MlqTick tk;
        tk.t = t;
        // arrivals
        for (int i : order) {
            if (procs[i].arrival != t) continue;
            int lv = cfg.feedback ? 0 : std::max(0, std::min(nq - 1, procs[i].priority));
            level[i] = lv;
            q[lv].push_back(i);
            procs[i].state = ProcState::Ready;
            tk.events.push_back(name(i) + " arrives -> " + cfg.queues[lv].name + (cfg.feedback ? " (new processes start at the top)" : " (fixed queue)"));
        }
        // quantum expiry of the previous runner (after the arrivals)
        if (running >= 0) {
            const MlqQueue& mq = cfg.queues[level[running]];
            if (mq.policy == QueuePolicy::RR && slice[running] >= mq.quantum) {
                int from = level[running];
                if (cfg.feedback && from + 1 < nq) {
                    level[running] = from + 1;
                    ++r.demotions;
                    tk.events.push_back(name(running) + " used its whole quantum (" + std::to_string(mq.quantum) + ") -> demoted to " + cfg.queues[from + 1].name);
                } else tk.events.push_back(name(running) + " quantum expired -> tail of " + cfg.queues[from].name);
                q[level[running]].push_back(running);
                slice[running] = 0;
                procs[running].state = ProcState::Ready;
                running = -1;
            }
        }
        // priority boost
        if (cfg.feedback && cfg.boostInterval > 0 && t > 0 && t % cfg.boostInterval == 0) {
            std::deque<int> all;
            for (int k = 0; k < nq; ++k) for (int i : q[k]) all.push_back(i);
            if (running >= 0) { all.push_back(running); slice[running] = 0; procs[running].state = ProcState::Ready; running = -1; }
            for (int k = 0; k < nq; ++k) q[k].clear();
            for (int i : all) { level[i] = 0; slice[i] = 0; q[0].push_back(i); }
            if (!all.empty()) {
                ++r.boosts;
                tk.events.push_back("priority boost at t=" + std::to_string(t) + ": every process moves to " + cfg.queues[0].name);
            }
        }
        // preemption by a higher queue
        if (running >= 0 && cfg.preemptive) {
            int higher = -1;
            for (int k = 0; k < level[running]; ++k) if (!q[k].empty()) { higher = k; break; }
            if (higher >= 0) {
                ++r.preemptions;
                tk.events.push_back(name(running) + " preempted: " + cfg.queues[higher].name + " has a ready process (" + name(q[higher].front()) + ")");
                q[level[running]].push_back(running);
                slice[running] = 0;
                procs[running].state = ProcState::Ready;
                running = -1;
            }
        }
        // dispatch
        if (running < 0)
            for (int k = 0; k < nq && running < 0; ++k)
                if (!q[k].empty()) { running = q[k].front(); q[k].pop_front(); slice[running] = 0; }
        int pid = -1;
        if (running >= 0) {
            PCB& p = byIdx(procs, running);
            p.state = ProcState::Running;
            if (p.firstRun < 0) p.firstRun = t;
            pid = p.pid;
            tk.pid = pid;
            tk.queue = level[running];
            --p.remaining;
            ++slice[running];
            if (p.remaining == 0) {
                p.finish = t + 1;
                p.state = ProcState::Terminated;
                ++done;
                tk.events.push_back(name(running) + " finishes at t=" + std::to_string(t + 1));
                running = -1;
            }
        }
        if (pid != -1 && pid != lastPid && lastPid != -2) ++r.sched.contextSwitches;   // running pid changes (idle ignored)
        if (pid != -1) lastPid = pid;
        appendTick(timeline, pid, t);
        for (int k = 0; k < nq; ++k) {
            std::vector<int> pids;
            for (int i : q[k]) pids.push_back(procs[i].pid);
            tk.queues.push_back(pids);
        }
        r.ticks.push_back(tk);
    }
    // metrics (definitions as in the scheduler: turnaround = completion - arrival, waiting = turnaround - burst)
    r.sched.algo = Algo::RR;
    r.sched.quantum = 0;
    r.sched.timeline = timeline;
    std::vector<int> byPid = order;
    for (int i : byPid) {
        const PCB& p = procs[i];
        ProcMetrics m{p.pid, p.arrival, p.burst, p.finish, p.finish - p.arrival - p.burst, p.finish - p.arrival, p.firstRun - p.arrival};
        r.sched.metrics.push_back(m);
        r.sched.avgWaiting += m.waiting;
        r.sched.avgTurnaround += m.turnaround;
        r.sched.avgResponse += m.response;
    }
    r.sched.avgWaiting /= n;
    r.sched.avgTurnaround /= n;
    r.sched.avgResponse /= n;
    r.queueOf = level;
    return r;
}

std::vector<MlqPreset> mlqPresets() {
    auto pcb = [](int pid, int arr, int burst, int queue) { PCB p; p.pid = pid; p.arrival = arr; p.burst = burst; p.priority = queue; return p; };
    MlqConfig mlq;
    mlq.feedback = false;
    mlq.queues = {{"Q0 system (RR q=2)", QueuePolicy::RR, 2}, {"Q1 interactive (RR q=4)", QueuePolicy::RR, 4}, {"Q2 batch (FCFS)", QueuePolicy::FCFS, 0}};
    MlqConfig fb;
    fb.feedback = true;
    fb.queues = {{"Q0 (RR q=2)", QueuePolicy::RR, 2}, {"Q1 (RR q=4)", QueuePolicy::RR, 4}, {"Q2 (FCFS)", QueuePolicy::FCFS, 0}};
    MlqConfig boost = fb;
    boost.boostInterval = 12;
    return {
        {"MLQ: system / interactive / batch",
         "Fixed queues (the Queue column): system processes always run before interactive ones, batch only when both are empty.",
         mlq, {pcb(1, 0, 6, 2), pcb(2, 1, 3, 1), pcb(3, 2, 2, 0), pcb(4, 3, 4, 1), pcb(5, 4, 1, 0)}},
        {"MLFQ: long jobs sink, short jobs stay on top",
         "Everybody starts in Q0; CPU-bound P1 uses its whole quanta and is demoted to Q2, the short jobs finish in Q0 / Q1.",
         fb, {pcb(1, 0, 12, 0), pcb(2, 2, 2, 0), pcb(3, 4, 3, 0), pcb(4, 9, 2, 0)}},
        {"MLFQ with priority boost (no starvation)",
         "A stream of short jobs keeps Q0 busy; every 12 ticks the boost lifts the long job P1 back to Q0 so it makes progress.",
         boost, {pcb(1, 0, 10, 0), pcb(2, 1, 2, 0), pcb(3, 3, 2, 0), pcb(4, 5, 2, 0), pcb(5, 7, 2, 0), pcb(6, 9, 2, 0), pcb(7, 11, 2, 0), pcb(8, 13, 2, 0)}},
    };
}
