// integrated.cpp - one tick loop that combines the three OS modules.
#include "integrated.h"
#include <algorithm>
#include <climits>
#include <map>
#include <random>
#include <stdexcept>

// 70% of the time the next page is the same page or a neighbour (locality of
// reference), otherwise a random page. rng() % n keeps it identical on every platform.
std::vector<int> makeReferenceString(int pid, int pages, int length) {
    std::vector<int> refs;
    if (pages <= 0) return refs;
    std::mt19937 rng((unsigned)pid);
    int cur = (int)(rng() % (unsigned)pages);
    for (int i = 0; i < length; ++i) {
        refs.push_back(cur);
        if (rng() % 100 < 70) cur += (int)(rng() % 3) - 1;     // -1, 0 or +1
        else cur = (int)(rng() % (unsigned)pages);
        cur = std::max(0, std::min(pages - 1, cur));
    }
    return refs;
}

namespace {
std::string P(int pid) { return "P" + std::to_string(pid); }
bool leq(const Vec& a, const Vec& b) {
    for (size_t j = 0; j < a.size(); ++j) if (a[j] > b[j]) return false;
    return true;
}
}  // namespace

IntegratedResult runIntegrated(const std::vector<PCB>& input, const IntegratedConfig& cfg) {
    validateProcesses(input, cfg.algo, cfg.quantum);
    if (cfg.numFrames < 1) throw std::invalid_argument("number of frames must be >= 1");
    if (cfg.detectInterval < 1) throw std::invalid_argument("detection interval must be >= 1");
    size_t m = cfg.totalRes.size();
    std::map<int, int> row;                                    // pid -> row index
    for (size_t i = 0; i < input.size(); ++i) row[input[i].pid] = (int)i;
    size_t n = input.size();

    // Scripted requests per process, sorted by run_tick; Max claim = sum of its requests.
    std::vector<std::vector<ScriptedRequest>> reqs(n);
    IntegratedResult R;
    R.maxClaim.assign(n, Vec(m, 0));
    for (const ScriptedRequest& r : cfg.requests) {
        if (!row.count(r.pid)) throw std::invalid_argument("request for unknown process P" + std::to_string(r.pid));
        if (r.req.size() != m) throw std::invalid_argument("request size does not match the number of resource types");
        if (!leq(r.req, cfg.totalRes)) throw std::invalid_argument("a request of " + P(r.pid) + " exceeds the total resources");
        int i = row[r.pid];
        reqs[i].push_back(r);
        for (size_t j = 0; j < m; ++j) R.maxClaim[i][j] += r.req[j];
    }
    for (auto& v : reqs)
        std::stable_sort(v.begin(), v.end(), [](const ScriptedRequest& a, const ScriptedRequest& b) { return a.runTick < b.runTick; });
    for (size_t i = 0; i < n; ++i)
        if (!leq(R.maxClaim[i], cfg.totalRes))
            throw std::invalid_argument(P(input[i].pid) + ": its requests add up to more than the total resources");

    // ---- simulation state ----
    std::vector<PCB> procs = input;
    for (PCB& p : procs) { p.remaining = p.burst; p.state = ProcState::New; p.firstRun = -1; p.finish = -1; }
    std::vector<int> executed(n, 0), nextReq(n, 0);
    std::vector<bool> killed(n, false);
    Matrix alloc(n, Vec(m, 0)), maxNow = R.maxClaim;
    Vec avail = cfg.totalRes;
    std::vector<std::vector<int>> pageRefs(n);
    for (size_t i = 0; i < n; ++i) pageRefs[i] = makeReferenceString(procs[i].pid, procs[i].pages, procs[i].burst);
    FramePool pool(cfg.pageAlgo, cfg.numFrames);
    SchedState st;
    size_t finished = 0;
    int t = 0;
    auto log = [&](const std::string& s) { R.log.push_back("[t=" + std::to_string(t) + "] " + s); };

    // Releases everything a process holds (resources and frames) when it leaves the system.
    auto releaseAll = [&](int i) {
        for (size_t j = 0; j < m; ++j) { avail[j] += alloc[i][j]; alloc[i][j] = 0; maxNow[i][j] = 0; }
        int pid = procs[i].pid;
        pool.freeIf([pid](long key) { return key / 1000 == pid; });
    };

    // Tries the pending request of process i. Returns true if granted (and allocates it).
    auto tryRequest = [&](int i, bool retry) -> bool {
        const ScriptedRequest& r = reqs[i][nextReq[i]];
        bool granted;
        std::string why;
        if (cfg.avoidance) {                                   // Banker's request algorithm
            RequestResult rr = bankersRequest(i, r.req, alloc, maxNow, avail);
            granted = rr.granted;
            why = rr.reason;
        } else {                                               // no avoidance: grant if available
            granted = leq(r.req, avail);
            why = granted ? "available" : "not enough available resources";
        }
        std::string what = P(procs[i].pid) + (retry ? " RES retry " : " RES request ") + vecToString(r.req);
        if (granted) {
            for (size_t j = 0; j < m; ++j) { alloc[i][j] += r.req[j]; avail[j] -= r.req[j]; }
            nextReq[i]++;
            R.requestsGranted++;
            log(what + " granted, available now " + vecToString(avail));
        } else if (!retry) {
            R.requestsDenied++;
            log(what + " denied (" + why + ")");
        }
        return granted;
    };
    // True if process i must make a request before it can run its next tick.
    auto requestDue = [&](int i) {
        return nextReq[i] < (int)reqs[i].size() && reqs[i][nextReq[i]].runTick <= executed[i];
    };

    // Upper bound on the run length (protects against endless waiting).
    int limit = 100;
    for (const PCB& p : procs) limit += p.arrival + p.burst * 3;

    std::vector<int> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return procs[a].arrival != procs[b].arrival ? procs[a].arrival < procs[b].arrival : procs[a].pid < procs[b].pid;
    });
    size_t nextArrival = 0;

    for (t = 0; finished < n && t < limit; ++t) {
        // 1. Arrivals: New -> Ready
        while (nextArrival < n && procs[order[nextArrival]].arrival <= t) {
            int i = order[nextArrival++];
            makeReady(procs, st, i);
            log(P(procs[i].pid) + " READY arrived");
        }
        // 2. Waiting processes retry their request every tick (Waiting -> Ready when granted)
        for (size_t i = 0; i < n; ++i)
            if (procs[i].state == ProcState::Waiting && tryRequest((int)i, true)) {
                makeReady(procs, st, (int)i);
                log(P(procs[i].pid) + " READY resources granted");
            }
        // 3. Dispatch (same rules as runScheduler). A process that must request
        //    resources first may be blocked -> Waiting, then the dispatcher picks again.
        int prevRunning = st.running;
        int idx = schedulerPick(cfg.algo, procs, st, cfg.quantum);
        while (idx >= 0 && requestDue(idx)) {
            if (tryRequest(idx, false)) continue;                  // granted: another one may be due
            procs[idx].state = ProcState::Waiting;                 // Running -> Waiting (blocked)
            st.running = -1;
            log(P(procs[idx].pid) + " WAITING for " + vecToString(reqs[idx][nextReq[idx]].req));
            idx = schedulerPick(cfg.algo, procs, st, cfg.quantum);
        }
        if (prevRunning >= 0 && prevRunning != idx && procs[prevRunning].state == ProcState::Ready)
            log(P(procs[prevRunning].pid) + " READY preempted remaining=" + std::to_string(procs[prevRunning].remaining));

        // 4. Execute one tick: memory reference + CPU burst
        if (idx >= 0) {
            PCB& p = procs[idx];
            if (p.firstRun < 0) p.firstRun = t;
            if (!pageRefs[idx].empty()) {
                int k = executed[idx];
                int page = pageRefs[idx][k];
                long key = (long)p.pid * 1000 + page;
                // Optimal in a shared pool: "future" = this process's own remaining references.
                auto nextUse = [&](long kk) -> long {
                    int owner = (int)(kk / 1000), pg = (int)(kk % 1000);
                    auto it = row.find(owner);
                    if (it == row.end()) return LONG_MAX;
                    const std::vector<int>& refs = pageRefs[it->second];
                    int from = executed[it->second] + (it->second == idx ? 1 : 0);
                    for (int j = from; j < (int)refs.size(); ++j) if (refs[j] == pg) return j - from;
                    return LONG_MAX;
                };
                FramePool::Access a = pool.access(key, nextUse);
                if (a.fault) {
                    R.memFaults++;
                    std::string ev = a.evicted >= 0 ? " evicted=P" + std::to_string(a.evicted / 1000) + ":" +
                                                          std::to_string(a.evicted % 1000) : "";
                    log("MEM fault " + P(p.pid) + " page=" + std::to_string(page) + " frame=" + std::to_string(a.frame) + ev);
                } else {
                    R.memHits++;
                    log("MEM hit " + P(p.pid) + " page=" + std::to_string(page) + " frame=" + std::to_string(a.frame));
                }
            }
            p.remaining--;
            executed[idx]++;
            st.sliceUsed++;
            appendTick(R.timeline, p.pid, t);
            if (p.remaining == 0) {
                p.state = ProcState::Terminated;
                p.finish = t + 1;
                st.running = -1;
                st.sliceUsed = 0;
                finished++;
                releaseAll(idx);
                log(P(p.pid) + " TERMINATED finish=" + std::to_string(p.finish) + ", resources released");
            } else {
                log(P(p.pid) + " RUNNING remaining=" + std::to_string(p.remaining));
            }
        } else {
            appendTick(R.timeline, -1, t);
            log("CPU idle");
        }

        // 5. Periodic deadlock detection over the blocked processes' pending requests
        bool anyWaiting = false;
        for (const PCB& p : procs) anyWaiting = anyWaiting || p.state == ProcState::Waiting;
        if ((t + 1) % cfg.detectInterval == 0 && anyWaiting) {
            Matrix request(n, Vec(m, 0));
            for (size_t i = 0; i < n; ++i)
                if (procs[i].state == ProcState::Waiting) request[i] = reqs[i][nextReq[i]].req;
            DetectionResult d = detectDeadlock(alloc, request, avail);
            if (d.deadlocked) {
                R.deadlocksDetected++;
                std::string list;
                for (int i : d.deadlocked_pids) list += (list.empty() ? "" : ",") + P(procs[i].pid);
                log("DEADLOCK detected " + list);
                if (R.firstDeadlock.tick < 0)
                    R.firstDeadlock = {t, alloc, maxNow, request, avail, d.deadlocked_pids};
                if (cfg.autoRecover) {
                    for (const RecoveryStep& s : recoverByTermination(alloc, request, avail)) {
                        int i = s.killedPid;
                        procs[i].state = ProcState::Terminated;
                        procs[i].finish = t + 1;
                        killed[i] = true;
                        R.killedPids.push_back(procs[i].pid);
                        finished++;
                        releaseAll(i);
                        log("RECOVERY killed " + P(procs[i].pid) + " released " + vecToString(s.released));
                    }
                } else {
                    log("DEADLOCK not resolved (auto-recover is off): simulation stopped");
                    R.stalled = true;
                    ++t;
                    break;
                }
            }
        }
    }
    R.endTime = t;
    if (finished < n && !R.stalled) { R.stalled = true; log("simulation stopped: tick limit reached"); }

    // Metrics (killed processes are listed but not averaged)
    int done = 0;
    for (size_t i = 0; i < n; ++i) {
        const PCB& p = procs[i];
        ProcMetrics pm{p.pid, p.arrival, p.burst, p.finish, 0, 0, 0};
        if (p.finish >= 0) {
            pm.turnaround = p.finish - p.arrival;
            pm.waiting = pm.turnaround - (killed[i] ? executed[i] : p.burst);
        }
        pm.response = p.firstRun >= 0 ? p.firstRun - p.arrival : -1;
        R.metrics.push_back(pm);
        if (!killed[i] && p.state == ProcState::Terminated) {
            R.avgWaiting += pm.waiting; R.avgTurnaround += pm.turnaround; R.avgResponse += pm.response;
            done++;
        }
    }
    if (done) { R.avgWaiting /= done; R.avgTurnaround /= done; R.avgResponse /= done; }
    std::sort(R.metrics.begin(), R.metrics.end(), [](const ProcMetrics& a, const ProcMetrics& b) { return a.pid < b.pid; });
    int refs = R.memFaults + R.memHits;
    R.hitRatio = refs ? (double)R.memHits / refs : 0.0;
    return R;
}
