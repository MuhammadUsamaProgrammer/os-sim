// threads.cpp - tick simulation of the threading models (see threads.h for the rules).
#include "threads.h"
#include <algorithm>
#include <set>
#include <stdexcept>

const char* threadModelName(ThreadModel m) {
    switch (m) {
        case ThreadModel::ManyToOne: return "Many-to-One";
        case ThreadModel::OneToOne: return "One-to-One";
        case ThreadModel::ManyToMany: return "Many-to-Many";
    }
    return "?";
}

const char* tStateName(TState s) {
    switch (s) {
        case TState::New: return "New";
        case TState::Ready: return "Ready";
        case TState::Running: return "Running";
        case TState::Blocked: return "Blocked";
        case TState::Joining: return "Joining";
        case TState::Completed: return "Completed";
    }
    return "?";
}

void validateThreadConfig(const ThreadConfig& c) {
    auto need = [](bool ok, const std::string& m) { if (!ok) throw std::invalid_argument(m); };
    need(!c.threads.empty() && c.threads.size() <= 12, "1 to 12 user threads");
    need(c.cores >= 1 && c.cores <= 8, "1 to 8 logical cores");
    need(c.quantum >= 1 && c.quantum <= 20, "quantum must be 1..20 ticks");
    need(c.model != ThreadModel::ManyToMany || (c.contexts >= 1 && c.contexts <= 8), "Many-to-Many needs 1 to 8 kernel contexts");
    std::set<int> ids;
    for (const UThreadSpec& t : c.threads) {
        need(ids.insert(t.id).second, "duplicate thread id " + std::to_string(t.id));
        need(t.arrival >= 0 && t.arrival <= 200, "arrival must be 0..200");
        need(t.work >= 1 && t.work <= 100, "work must be 1..100 ticks");
        need(t.blockAt < 0 || (t.blockAt >= 1 && t.blockAt < t.work), "block point must be inside the work (1..work-1)");
        need(t.blockAt < 0 || (t.blockFor >= 1 && t.blockFor <= 50), "a blocking call lasts 1..50 ticks");
    }
    for (const UThreadSpec& t : c.threads) {
        if (t.joinTarget < 0) continue;
        need(ids.count(t.joinTarget) && t.joinTarget != t.id, "thread " + std::to_string(t.id) + " joins a thread that does not exist");
    }
    // join cycles would never finish
    for (const UThreadSpec& t : c.threads) {
        int cur = t.joinTarget, steps = 0;
        while (cur >= 0 && steps++ < 20) {
            if (cur == t.id) throw std::invalid_argument("join cycle: threads wait for each other forever");
            int next = -1;
            for (const UThreadSpec& u : c.threads) if (u.id == cur) next = u.joinTarget;
            cur = next;
        }
    }
}

int effectiveContexts(const ThreadConfig& c) {
    switch (c.model) {
        case ThreadModel::ManyToOne: return 1;
        case ThreadModel::OneToOne: return (int)c.threads.size();
        case ThreadModel::ManyToMany: return c.contexts;
    }
    return 1;
}

ThreadResult runThreads(const ThreadConfig& c) {
    validateThreadConfig(c);
    ThreadResult r;
    r.cfg = c;
    const int n = (int)c.threads.size(), K = effectiveContexts(c);
    std::vector<TSnap> th(n);
    std::vector<int> ready;                    // ready queue (thread indices)
    std::vector<int> slice(n, 0), lastCore(n, -1), ctxOf(n, -1);
    std::vector<int> running;                  // threads that ran in the previous tick
    auto idOf = [&](int i) { return c.threads[i].id; };
    auto idx = [&](int id) { for (int i = 0; i < n; ++i) if (c.threads[i].id == id) return i; return -1; };
    for (int t = 0; t < 5000; ++t) {
        ThreadTick k;
        k.t = t;
        // 1. arrivals and 2. ends of blocking calls
        for (int i = 0; i < n; ++i) {
            if (th[i].state == TState::New && c.threads[i].arrival <= t) {
                th[i].state = TState::Ready;
                ready.push_back(i);
                k.events.push_back("T" + std::to_string(idOf(i)) + " created (argument " + std::to_string(c.threads[i].arg) + ") -> Ready");
            }
            if (th[i].state == TState::Blocked && th[i].blockLeft == 0) {
                th[i].state = TState::Ready;
                th[i].reason.clear();
                ready.push_back(i);
                if (c.model != ThreadModel::OneToOne) ctxOf[i] = -1;     // releases its context
                k.events.push_back("T" + std::to_string(idOf(i)) + " blocking call returned -> Ready");
            }
        }
        // 3. available contexts
        std::vector<bool> ctxBlocked(K, false);
        bool processBlocked = false;
        for (int i = 0; i < n; ++i)
            if (th[i].state == TState::Blocked) {
                if (c.model == ThreadModel::ManyToOne) { processBlocked = true; ctxBlocked[0] = true; }
                else if (ctxOf[i] >= 0) ctxBlocked[ctxOf[i]] = true;
            }
        int freeContexts = 0;
        for (int x = 0; x < K; ++x) freeContexts += !ctxBlocked[x];
        int slots = processBlocked ? 0 : std::min(c.cores, freeContexts);
        // 4. round-robin choice: keep running threads within their quantum, then the queue head
        std::vector<int> chosen;
        for (int i : running)
            if (th[i].state == TState::Running && slice[i] < c.quantum && (int)chosen.size() < slots) chosen.push_back(i);
        for (int i : running)                                   // quantum expired or no slot: back of the queue
            if (th[i].state == TState::Running && std::find(chosen.begin(), chosen.end(), i) == chosen.end()) {
                th[i].state = TState::Ready;
                ready.push_back(i);
                if (c.model == ThreadModel::ManyToMany) ctxOf[i] = -1;
            }
        while ((int)chosen.size() < slots && !ready.empty()) {
            int i = ready.front();
            ready.erase(ready.begin());
            chosen.push_back(i);
            slice[i] = 0;
            ++r.m.dispatches;
        }
        // 5. contexts and cores (keep the previous ones when free)
        std::vector<int> ctxThread(K, -1), coreThread(c.cores, -1);
        for (int i : chosen) {
            int ctx = c.model == ThreadModel::ManyToOne ? 0 : c.model == ThreadModel::OneToOne ? i : ctxOf[i];
            if (ctx < 0 || ctx >= K || ctxThread[ctx] >= 0 || ctxBlocked[ctx]) {
                ctx = -1;
                for (int x = 0; x < K && ctx < 0; ++x) if (ctxThread[x] < 0 && !ctxBlocked[x]) ctx = x;
            }
            ctxThread[ctx] = i;
            ctxOf[i] = ctx;
        }
        for (int i : chosen)
            if (lastCore[i] >= 0 && coreThread[lastCore[i]] < 0) coreThread[lastCore[i]] = i;
        for (int i : chosen) {
            if (std::find(coreThread.begin(), coreThread.end(), i) != coreThread.end()) continue;
            for (int x = 0; x < c.cores; ++x)
                if (coreThread[x] < 0) {
                    coreThread[x] = i;
                    if (lastCore[i] >= 0 && lastCore[i] != x) {
                        ++r.m.migrations;
                        k.events.push_back("T" + std::to_string(idOf(i)) + " migrates from core " + std::to_string(lastCore[i]) + " to core " + std::to_string(x));
                    }
                    break;
                }
        }
        // 6. state of everybody during this tick
        for (int i = 0; i < n; ++i) { th[i].core = -1; if (th[i].state != TState::Blocked) th[i].context = c.model == ThreadModel::OneToOne ? i : -1; }
        for (int x = 0; x < c.cores; ++x)
            if (coreThread[x] >= 0) {
                int i = coreThread[x];
                if (th[i].state != TState::Running) k.events.push_back("T" + std::to_string(idOf(i)) + " dispatched on core " + std::to_string(x) +
                                                                     " via kernel context " + std::to_string(ctxOf[i]));
                th[i].state = TState::Running;
                th[i].core = x;
                th[i].context = ctxOf[i];
                th[i].reason.clear();
                lastCore[i] = x;
                ++slice[i];
            }
        int par = 0;
        for (int i = 0; i < n; ++i) {
            TSnap& s = th[i];
            if (s.state == TState::Ready) {
                s.reason = processBlocked ? "process stalled: its only kernel context is blocked (Many-to-One)"
                         : freeContexts == 0 ? "no free kernel context (all are blocked)"
                         : slots == 0 ? "no free core" : "waiting for a core / context";
                if (processBlocked || freeContexts == 0) ++r.m.stalledTicks; else ++r.m.waitingTicks;
            }
            if (s.state == TState::Blocked) ++r.m.blockedTicks;
            if (s.state == TState::Running) ++par;
        }
        r.m.maxParallel = std::max(r.m.maxParallel, par);
        r.m.busyCoreTicks += par;
        k.coreThread = coreThread;
        k.contextThread = ctxThread;
        for (int i = 0; i < n; ++i) if (th[i].state == TState::Blocked && th[i].context >= 0 && th[i].context < K) k.contextThread[th[i].context] = i;
        k.contextBlocked = ctxBlocked;
        k.threads = th;
        r.ticks.push_back(k);
        // 7. execute one tick of work; blocking calls, completion, joins (end of tick)
        running.clear();
        for (int i = 0; i < n; ++i) {
            if (th[i].state == TState::Blocked && th[i].blockLeft > 0) th[i].blockLeft--;
            if (th[i].state != TState::Running) continue;
            th[i].done++;
            const UThreadSpec& sp = c.threads[i];
            if (sp.blockAt >= 0 && th[i].done == sp.blockAt) {
                th[i].state = TState::Blocked;
                th[i].blockLeft = sp.blockFor;
                th[i].reason = "blocking system call (" + std::to_string(sp.blockFor) + " ticks)";
                r.ticks.back().events.push_back("T" + std::to_string(sp.id) + " makes a blocking call" +
                                                (c.model == ThreadModel::ManyToOne ? " -> the whole process blocks" : " -> only this thread blocks"));
            } else if (th[i].done >= sp.work) {
                int tgt = sp.joinTarget >= 0 ? idx(sp.joinTarget) : -1;
                if (tgt >= 0 && th[tgt].state != TState::Completed) {
                    th[i].state = TState::Joining;
                    th[i].reason = "pthread_join(T" + std::to_string(sp.joinTarget) + ")";
                    r.ticks.back().events.push_back("T" + std::to_string(sp.id) + " finished its work and waits in pthread_join(T" + std::to_string(sp.joinTarget) + ")");
                } else {
                    th[i].state = TState::Completed;
                    th[i].finish = t + 1;
                    r.completionOrder.push_back(sp.id);
                    r.ticks.back().events.push_back("T" + std::to_string(sp.id) + " completed at t=" + std::to_string(t + 1));
                }
            } else {
                running.push_back(i);
            }
            if (th[i].state != TState::Running && c.model == ThreadModel::ManyToMany && th[i].state != TState::Blocked) ctxOf[i] = -1;
        }
        // joins complete when their target has completed (possibly a chain)
        for (bool changed = true; changed;) {
            changed = false;
            for (int i = 0; i < n; ++i) {
                int tgt = th[i].state == TState::Joining ? idx(c.threads[i].joinTarget) : -1;
                if (tgt >= 0 && th[tgt].state == TState::Completed) {
                    th[i].state = TState::Completed;
                    th[i].finish = t + 1;
                    th[i].reason.clear();
                    r.completionOrder.push_back(c.threads[i].id);
                    r.ticks.back().events.push_back("pthread_join returns: T" + std::to_string(c.threads[i].id) + " completes after T" +
                                                    std::to_string(c.threads[i].joinTarget));
                    changed = true;
                }
            }
        }
        bool all = true;
        for (const TSnap& s : th) all = all && s.state == TState::Completed;
        r.finalState = th;
        if (all) { r.completed = true; r.m.completion = t + 1; break; }
    }
    for (TSnap& s : r.finalState) { s.core = -1; if (s.state == TState::Completed) s.context = -1; }
    return r;
}

std::vector<ThreadPreset> threadPresets() {
    std::vector<ThreadPreset> v;
    ThreadConfig three;
    three.threads = {{0, "T0", 0, 4, -1, 0, -1, 10}, {1, "T1", 0, 4, -1, 0, -1, 11}, {2, "T2", 0, 4, -1, 0, -1, 12}};
    three.cores = 3;
    three.quantum = 1;
    ThreadConfig m1 = three;
    m1.model = ThreadModel::ManyToOne;
    v.push_back({"Many-to-One, 3 threads x 4 ticks, 3 cores",
                 "One kernel context: only one thread runs at a time although 3 cores exist -> 12 ticks.", m1});
    ThreadConfig o1 = three;
    o1.model = ThreadModel::OneToOne;
    v.push_back({"One-to-One, 3 threads x 4 ticks, 3 cores", "Each thread has its own kernel context: true parallelism -> 4 ticks.", o1});
    ThreadConfig mm = three;
    mm.model = ThreadModel::ManyToMany;
    mm.contexts = 2;
    mm.cores = 2;
    mm.quantum = 1;
    v.push_back({"Many-to-Many, 2 contexts, 2 cores, quantum 1",
                 "Three user threads share two kernel contexts: work-conserving round robin -> 6 ticks.", mm});
    ThreadConfig blk = three;
    blk.threads[0].blockAt = 1;
    blk.threads[0].blockFor = 3;
    blk.model = ThreadModel::ManyToOne;
    v.push_back({"Blocking call, Many-to-One",
                 "T0 makes a blocking call: in the many-to-one model the whole process (all threads) stalls.", blk});
    ThreadConfig blk1 = blk;
    blk1.model = ThreadModel::OneToOne;
    v.push_back({"Blocking call, One-to-One", "Same blocking call: only T0 waits, the other threads keep running.", blk1});
    ThreadConfig join;
    join.model = ThreadModel::OneToOne;
    join.cores = 2;
    join.quantum = 2;
    join.threads = {{0, "main", 0, 1, -1, 0, 2, 0}, {1, "worker A", 0, 3, -1, 0, -1, 5}, {2, "worker B", 0, 5, -1, 0, 1, 7}};
    v.push_back({"Create and join (main joins B, B joins A)",
                 "main finishes its own work early and waits in pthread_join; join order follows the completions.", join});
    return v;
}
