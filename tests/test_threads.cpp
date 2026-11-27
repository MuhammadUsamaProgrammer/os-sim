// test_threads.cpp - M6 threading models (core/threads.*): 12 / 4 / 6 tick reference cases,
// context / core exclusivity every tick, no duplicated or lost work, blocking behaviour per
// model, joins, migrations, one-core vs multi-core, validation, determinism.
#include <set>
#include "check.h"
#include "threads.h"

static bool invariants(const ThreadResult& r) {
    int K = effectiveContexts(r.cfg);
    int work = 0, ran = 0;
    for (const UThreadSpec& t : r.cfg.threads) work += t.work;
    for (const ThreadTick& k : r.ticks) {
        std::set<int> onCores;
        int running = 0;
        for (int x = 0; x < (int)k.coreThread.size(); ++x) {
            int i = k.coreThread[x];
            if (i < 0) continue;
            if (!onCores.insert(i).second) return false;                  // one thread on two cores
            if (k.threads[i].state != TState::Running || k.threads[i].core != x) return false;
        }
        std::set<int> inCtx;
        for (int x = 0; x < (int)k.contextThread.size(); ++x)
            if (k.contextThread[x] >= 0 && !inCtx.insert(k.contextThread[x]).second) return false;
        for (const TSnap& s : k.threads) running += s.state == TState::Running;
        if (running != (int)onCores.size() || running > r.cfg.cores || running > K) return false;
        if (r.cfg.model == ThreadModel::ManyToOne && running > 1) return false;
        ran += running;
    }
    return ran == work && r.completed;                                   // every work tick done exactly once
}

static ThreadConfig three(ThreadModel m, int contexts, int cores, int quantum) {
    ThreadConfig c;
    c.model = m;
    c.contexts = contexts;
    c.cores = cores;
    c.quantum = quantum;
    c.threads = {{0, "T0", 0, 4, -1, 0, -1, 0}, {1, "T1", 0, 4, -1, 0, -1, 0}, {2, "T2", 0, 4, -1, 0, -1, 0}};
    return c;
}

int main() {
    // Reference: 3 threads x 4 ticks, no blocking.
    ThreadResult m1 = runThreads(three(ThreadModel::ManyToOne, 1, 3, 1));
    ThreadResult o1 = runThreads(three(ThreadModel::OneToOne, 3, 3, 1));
    ThreadResult mm = runThreads(three(ThreadModel::ManyToMany, 2, 2, 1));
    CHECK(m1.m.completion == 12 && m1.m.maxParallel == 1);
    CHECK(o1.m.completion == 4 && o1.m.maxParallel == 3);
    CHECK(mm.m.completion == 6 && mm.m.maxParallel == 2);
    CHECK(invariants(m1) && invariants(o1) && invariants(mm));
    CHECK(runThreads(three(ThreadModel::ManyToOne, 1, 8, 1)).m.completion == 12);     // extra cores do not help
    CHECK(runThreads(three(ThreadModel::ManyToMany, 2, 4, 1)).m.completion == 6);      // limited by 2 contexts
    CHECK(runThreads(three(ThreadModel::ManyToMany, 3, 3, 1)).m.completion == 4);
    // With a large quantum Many-to-Many is not work-conserving across threads: 4 + 4 = 8
    CHECK(runThreads(three(ThreadModel::ManyToMany, 2, 2, 4)).m.completion == 8);
    CHECK_NEAR(o1.m.occupancy(3), 100.0);
    CHECK_NEAR(m1.m.occupancy(3), 100.0 / 3);

    // Concurrency vs parallelism: one core interleaves (12), three cores run in parallel (4).
    ThreadResult one = runThreads(three(ThreadModel::OneToOne, 3, 1, 1));
    CHECK(one.m.completion == 12 && one.m.maxParallel == 1 && invariants(one));

    // Blocking call: T0 blocks after 1 tick for 3 ticks.
    ThreadConfig b = three(ThreadModel::ManyToOne, 1, 3, 1);
    b.threads[0].blockAt = 1;
    b.threads[0].blockFor = 3;
    ThreadResult bm1 = runThreads(b);
    CHECK(bm1.m.completion == 15 && bm1.m.stalledTicks > 0 && invariants(bm1));   // the whole process stalls 3 ticks
    bool stalledAll = false;
    for (const ThreadTick& k : bm1.ticks) {
        int ready = 0, run = 0;
        for (const TSnap& s : k.threads) { ready += s.state == TState::Ready; run += s.state == TState::Running; }
        if (ready == 2 && run == 0) stalledAll = true;
    }
    CHECK(stalledAll);
    b.model = ThreadModel::OneToOne;
    ThreadResult bo1 = runThreads(b);
    CHECK(bo1.m.completion == 7 && bo1.m.stalledTicks == 0 && invariants(bo1));  // others keep running
    b.model = ThreadModel::ManyToMany;
    b.contexts = 1;
    b.cores = 2;
    ThreadResult bmm = runThreads(b);                   // the only context is blocked -> stall like M:1
    CHECK(bmm.m.stalledTicks > 0 && invariants(bmm));
    b.contexts = 2;
    ThreadResult bmm2 = runThreads(b);                  // second context keeps the others going
    CHECK(bmm2.m.completion < bm1.m.completion && invariants(bmm2));

    // Joins: main joins B, B joins A -> completion order A, B, main.
    std::vector<ThreadPreset> pr = threadPresets();
    CHECK(pr.size() == 6);
    ThreadResult j = runThreads(pr[5].cfg);
    CHECK((j.completionOrder == std::vector<int>{1, 2, 0}));
    CHECK(invariants(j));
    int mainFinish = j.finalState[0].finish, bFinish = j.finalState[2].finish, aFinish = j.finalState[1].finish;
    CHECK(aFinish == 3 && bFinish == 6 && mainFinish == 6);     // hand-derived: A 3 ticks; B runs ticks 1..5
    bool joinedAfter = false;
    for (const ThreadTick& k : j.ticks)
        for (const std::string& e : k.events) if (e.find("pthread_join returns: T0") != std::string::npos) joinedAfter = true;
    CHECK(joinedAfter && bFinish > 0);
    bool joining = false;
    for (const ThreadTick& k : j.ticks) joining = joining || k.threads[0].state == TState::Joining;
    CHECK(joining);

    // Every preset completes and respects the invariants.
    for (const ThreadPreset& p : pr) CHECK(invariants(runThreads(p.cfg)));

    // Arrivals: a late thread does not run before it exists.
    ThreadConfig late = three(ThreadModel::OneToOne, 3, 3, 1);
    late.threads[2].arrival = 5;
    ThreadResult lr = runThreads(late);
    CHECK(lr.ticks[4].threads[2].state == TState::New && lr.m.completion == 9 && invariants(lr));

    // Migrations happen with more threads than cores under round robin.
    CHECK(runThreads(three(ThreadModel::OneToOne, 3, 2, 1)).m.migrations > 0);

    // Determinism and validation
    ThreadResult d1 = runThreads(pr[2].cfg), d2 = runThreads(pr[2].cfg);
    CHECK(d1.completionOrder == d2.completionOrder && d1.m.completion == d2.m.completion);
    ThreadConfig bad = three(ThreadModel::OneToOne, 3, 0, 1);
    CHECK_THROWS(runThreads(bad), std::invalid_argument);
    bad = three(ThreadModel::OneToOne, 3, 2, 1);
    bad.threads[0].joinTarget = 1;
    bad.threads[1].joinTarget = 0;
    CHECK_THROWS(runThreads(bad), std::invalid_argument);                // join cycle
    bad = three(ThreadModel::OneToOne, 3, 2, 1);
    bad.threads[0].blockAt = 4;                                           // must be inside the work
    bad.threads[0].blockFor = 2;
    CHECK_THROWS(runThreads(bad), std::invalid_argument);
    bad = three(ThreadModel::OneToOne, 3, 2, 1);
    bad.threads[1].id = 0;
    CHECK_THROWS(runThreads(bad), std::invalid_argument);
    return report("test_threads");
}
