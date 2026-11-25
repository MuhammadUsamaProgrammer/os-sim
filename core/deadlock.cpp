// deadlock.cpp - Banker's algorithm, deadlock detection, recovery and RAG.
#include "deadlock.h"
#include <functional>
#include <stdexcept>

// Throws std::invalid_argument unless m is rows x cols with non-negative values.
static void checkMatrix(const Matrix& m, size_t rows, size_t cols, const char* name) {
    if (m.size() != rows)
        throw std::invalid_argument(std::string(name) + " must have " + std::to_string(rows) + " rows");
    for (const Vec& r : m) {
        if (r.size() != cols)
            throw std::invalid_argument(std::string(name) + " must have " + std::to_string(cols) + " columns");
        for (int x : r) if (x < 0) throw std::invalid_argument(std::string(name) + " has a negative value");
    }
}
// Same check for a single vector.
static void checkVec(const Vec& v, size_t cols, const char* name) {
    checkMatrix(Matrix{v}, 1, cols, name);
}
// True if a[j] <= b[j] for every resource j.
static bool lessEq(const Vec& a, const Vec& b) {
    for (size_t j = 0; j < a.size(); ++j) if (a[j] > b[j]) return false;
    return true;
}
// a += b, element-wise.
static void addTo(Vec& a, const Vec& b) { for (size_t j = 0; j < a.size(); ++j) a[j] += b[j]; }

std::string vecToString(const Vec& v) {
    std::string s;
    for (size_t j = 0; j < v.size(); ++j) s += (j ? " " : "") + std::to_string(v[j]);
    return s;
}

Matrix computeNeed(const Matrix& alloc, const Matrix& max) {
    if (alloc.empty()) throw std::invalid_argument("no processes");
    size_t m = alloc[0].size();
    checkMatrix(alloc, alloc.size(), m, "Allocation");
    checkMatrix(max, alloc.size(), m, "Max");
    Matrix need = max;
    for (size_t i = 0; i < alloc.size(); ++i)
        for (size_t j = 0; j < m; ++j) {
            need[i][j] = max[i][j] - alloc[i][j];
            if (need[i][j] < 0)
                throw std::invalid_argument("P" + std::to_string(i) + ": Allocation exceeds Max");
        }
    return need;
}

// Core of both the safety and the detection algorithm: repeatedly find an unfinished
// process whose demand <= work, let it finish and release its allocation.
// `done` comes in with already-finished processes marked; `order` receives the finish order.
static void passScan(const Matrix& alloc, const Matrix& demand, Vec work, std::vector<bool>& done,
                     std::vector<int>& order, std::vector<SafetyStep>* trace) {
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < alloc.size(); ++i) {           // one pass over P0..Pn-1
            if (done[i] || !lessEq(demand[i], work)) continue;
            SafetyStep st{(int)i, work, {}};
            addTo(work, alloc[i]);                            // Pi finishes, releases resources
            st.workAfter = work;
            if (trace) trace->push_back(st);
            done[i] = true;
            order.push_back((int)i);
            progress = true;                                  // continue the same pass
        }
    }
}

// Banker's safety algorithm: is there an order in which every process can finish?
SafetyResult bankersSafety(const Matrix& alloc, const Matrix& max, const Vec& avail) {
    SafetyResult r;
    r.need = computeNeed(alloc, max);
    checkVec(avail, alloc[0].size(), "Available");
    std::vector<bool> done(alloc.size(), false);
    passScan(alloc, r.need, avail, done, r.sequence, &r.trace);
    r.safe = r.sequence.size() == alloc.size();
    return r;
}

// Banker's resource-request algorithm for process pid.
RequestResult bankersRequest(int pid, const Vec& request, Matrix alloc, const Matrix& max, Vec avail) {
    RequestResult r;
    Matrix need = computeNeed(alloc, max);
    checkVec(avail, alloc[0].size(), "Available");
    if (pid < 0 || pid >= (int)alloc.size()) throw std::invalid_argument("no such process P" + std::to_string(pid));
    checkVec(request, avail.size(), "Request");
    if (!lessEq(request, need[pid])) {                       // step 1
        r.reason = "request exceeds maximum claim (Need = " + vecToString(need[pid]) + ")";
        return r;
    }
    if (!lessEq(request, avail)) {                           // step 2
        r.reason = "request exceeds available resources (" + vecToString(avail) + "); process must wait";
        return r;
    }
    for (size_t j = 0; j < avail.size(); ++j) {              // step 3: pretend to allocate
        avail[j] -= request[j];
        alloc[pid][j] += request[j];
    }
    r.safetyAfter = bankersSafety(alloc, max, avail);
    if (r.safetyAfter.safe) {
        r.granted = true;
        r.reason = "granted: new state is safe";
    } else {
        r.reason = "request would lead to an unsafe state; rolled back, process must wait";
    }                                                         // roll back = discard the copies
    return r;
}

// Deadlock detection: processes that cannot finish even if everybody else does are deadlocked.
DetectionResult detectDeadlock(const Matrix& alloc, const Matrix& request, const Vec& avail) {
    if (alloc.empty()) throw std::invalid_argument("no processes");
    size_t m = alloc[0].size();
    checkMatrix(alloc, alloc.size(), m, "Allocation");
    checkMatrix(request, alloc.size(), m, "Request");
    checkVec(avail, m, "Available");
    DetectionResult r;
    std::vector<bool> done(alloc.size(), false);
    // A process holding nothing cannot be part of a deadlock (textbook rule): Finish = true.
    for (size_t i = 0; i < alloc.size(); ++i) {
        bool none = true;
        for (int x : alloc[i]) if (x) none = false;
        done[i] = none;
    }
    passScan(alloc, request, avail, done, r.finishOrder, nullptr);
    for (size_t i = 0; i < alloc.size(); ++i) if (!done[i]) r.deadlocked_pids.push_back((int)i);
    r.deadlocked = !r.deadlocked_pids.empty();
    return r;
}

// Recovery: abort one deadlocked victim at a time until the deadlock is gone.
std::vector<RecoveryStep> recoverByTermination(Matrix alloc, Matrix request, Vec avail) {
    std::vector<RecoveryStep> steps;
    DetectionResult d = detectDeadlock(alloc, request, avail);
    while (d.deadlocked) {
        int victim = -1, victimTotal = 0;
        for (int pid : d.deadlocked_pids) {                  // cheapest victim first
            int total = 0;
            for (int x : alloc[pid]) total += x;
            if (victim < 0 || total < victimTotal) { victim = pid; victimTotal = total; }
        }
        RecoveryStep s;
        s.killedPid = victim;
        s.released = alloc[victim];
        addTo(avail, alloc[victim]);                         // its resources return to the pool
        for (size_t j = 0; j < avail.size(); ++j) { alloc[victim][j] = 0; request[victim][j] = 0; }
        d = detectDeadlock(alloc, request, avail);
        s.after = d;
        steps.push_back(s);
    }
    return steps;
}

// Resource-allocation graph with DFS cycle search.
RagGraph buildRag(const Matrix& alloc, const Matrix& request) {
    RagGraph g;
    g.nProcs = (int)alloc.size();
    g.nRes = alloc.empty() ? 0 : (int)alloc[0].size();
    checkMatrix(alloc, g.nProcs, g.nRes, "Allocation");
    checkMatrix(request, g.nProcs, g.nRes, "Request");
    int n = g.nProcs, total = g.nProcs + g.nRes;
    std::vector<std::vector<int>> adj(total);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < g.nRes; ++j) {
            if (alloc[i][j] > 0) { g.edges.push_back({n + j, i, false}); adj[n + j].push_back(i); }  // assignment
            if (request[i][j] > 0) { g.edges.push_back({i, n + j, true}); adj[i].push_back(n + j); } // request
        }
    // DFS with colours: 0 = unvisited, 1 = on the current path, 2 = finished.
    std::vector<int> colour(total, 0), path;
    std::function<bool(int)> dfs = [&](int u) -> bool {
        colour[u] = 1;
        path.push_back(u);
        for (int v : adj[u]) {
            if (colour[v] == 1) {                            // back edge -> cycle found
                size_t k = 0;
                while (path[k] != v) ++k;
                g.cycleNodes.assign(path.begin() + k, path.end());
                return true;
            }
            if (colour[v] == 0 && dfs(v)) return true;
        }
        path.pop_back();
        colour[u] = 2;
        return false;
    };
    for (int u = 0; u < total && g.cycleNodes.empty(); ++u)
        if (colour[u] == 0) dfs(u);
    return g;
}
