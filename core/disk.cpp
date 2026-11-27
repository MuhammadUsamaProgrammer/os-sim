// disk.cpp - disk scheduling algorithms; every result is a list of head moves (event trace).
#include "disk.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

const char* diskAlgoName(DiskAlgo a) {
    switch (a) {
        case DiskAlgo::FCFS: return "FCFS";
        case DiskAlgo::SSTF: return "SSTF";
        case DiskAlgo::SCAN: return "SCAN";
        case DiskAlgo::CSCAN: return "C-SCAN";
        case DiskAlgo::LOOK: return "LOOK";
        case DiskAlgo::CLOOK: return "C-LOOK";
    }
    return "?";
}

const std::vector<DiskAlgo>& allDiskAlgos() {
    static const std::vector<DiskAlgo> v = {DiskAlgo::FCFS, DiskAlgo::SSTF, DiskAlgo::SCAN,
                                            DiskAlgo::CSCAN, DiskAlgo::LOOK, DiskAlgo::CLOOK};
    return v;
}

const char* diskDirName(DiskDir d) { return d == DiskDir::Higher ? "higher" : "lower"; }

const char* moveKindName(MoveKind k) {
    switch (k) {
        case MoveKind::Service: return "service";
        case MoveKind::Boundary: return "boundary (transit)";
        case MoveKind::Wrap: return "wrap (circular return)";
    }
    return "?";
}

void validateDiskConfig(const DiskConfig& c) {
    auto need = [](bool ok, const std::string& m) { if (!ok) throw std::invalid_argument(m); };
    need(c.minCyl >= 0, "the first cylinder must be >= 0");
    need(c.maxCyl > c.minCyl, "the last cylinder must be greater than the first");
    need(c.maxCyl <= 100000, "at most 100000 cylinders are supported");
    need(c.head >= c.minCyl && c.head <= c.maxCyl, "the initial head position is outside the cylinder range");
    need(c.requests.size() <= 500, "at most 500 requests are supported");
    for (size_t i = 0; i < c.requests.size(); ++i)
        need(c.requests[i] >= c.minCyl && c.requests[i] <= c.maxCyl,
             "request " + std::to_string(i) + " (cylinder " + std::to_string(c.requests[i]) +
                 ") is outside the range " + std::to_string(c.minCyl) + ".." + std::to_string(c.maxCyl));
}

// Pseudocode lines (0-based indexes used by DiskMove::pseudoLine).
std::vector<std::string> diskPseudocode(DiskAlgo a) {
    switch (a) {
        case DiskAlgo::FCFS:
            return {"for each request r in arrival order:", "    move head to r.cylinder",
                    "    total += |r.cylinder - head|; head = r.cylinder"};
        case DiskAlgo::SSTF:
            return {"while requests are pending:", "    r = pending request nearest to head",
                    "      (tie: earliest in the input order)", "    total += |r - head|; head = r; remove r"};
        case DiskAlgo::SCAN:
            return {"service requests at the head position", "move in direction d, servicing every request on the way",
                    "if requests remain behind the head:", "    continue to the end of the disk (boundary)",
                    "    reverse d and service the remaining requests"};
        case DiskAlgo::CSCAN:
            return {"service requests at the head position", "move in direction d, servicing every request on the way",
                    "if requests remain:", "    continue to the end of the disk (boundary)",
                    "    wrap to the other end (distance counted)", "    continue in the SAME direction d"};
        case DiskAlgo::LOOK:
            return {"service requests at the head position", "move in direction d up to the last request on that side",
                    "if requests remain behind the head:", "    reverse at the last request (no trip to the end)",
                    "    service the remaining requests"};
        case DiskAlgo::CLOOK:
            return {"service requests at the head position", "move in direction d up to the last request on that side",
                    "if requests remain:", "    jump to the furthest remaining request (distance counted)",
                    "    continue in the SAME direction d"};
    }
    return {};
}

namespace {
struct Builder {
    DiskResult r;
    int head;
    bool lastUp = true;
    bool haveDir = false;
    void move(int to, MoveKind kind, int id, const std::string& reason, int line) {
        DiskMove m;
        m.from = head;
        m.to = to;
        m.distance = std::abs(to - head);
        m.kind = kind;
        m.requestId = id;
        m.movingUp = to > head ? true : to < head ? false : lastUp;
        // A reversal is a change of travel direction (wraps keep the logical direction).
        m.reversed = haveDir && m.distance > 0 && kind != MoveKind::Wrap && m.movingUp != lastUp &&
                     !(r.moves.size() && r.moves.back().kind == MoveKind::Wrap);
        if (m.distance > 0 && kind != MoveKind::Wrap) { lastUp = m.movingUp; haveDir = true; }
        r.total += m.distance;
        m.cumulative = r.total;
        m.reason = reason;
        m.pseudoLine = line;
        r.moves.push_back(m);
        head = to;
        if (id >= 0) { r.order.push_back(id); r.serviced++; }
    }
};
std::string cyl(int c) { return "cylinder " + std::to_string(c); }
}

// Runs one algorithm on a static batch and records every head move.
DiskResult runDisk(DiskAlgo algo, const DiskConfig& cfg) {
    validateDiskConfig(cfg);
    Builder b;
    b.r.algo = algo;
    b.r.cfg = cfg;
    b.head = cfg.head;
    b.lastUp = cfg.dir == DiskDir::Higher;
    const std::vector<int>& req = cfg.requests;
    std::vector<int> pending;
    for (int i = 0; i < (int)req.size(); ++i) pending.push_back(i);

    if (algo == DiskAlgo::FCFS) {
        for (int id : pending)
            b.move(req[id], MoveKind::Service, id, "FCFS: next request in arrival order is R" + std::to_string(id) +
                   " (" + cyl(req[id]) + ")", 1);
    } else if (algo == DiskAlgo::SSTF) {
        while (!pending.empty()) {
            size_t best = 0;
            for (size_t i = 1; i < pending.size(); ++i) {
                int d = std::abs(req[pending[i]] - b.head), bd = std::abs(req[pending[best]] - b.head);
                if (d < bd) best = i;                         // strict: ties keep the earlier request
            }
            int id = pending[best];
            int d = std::abs(req[id] - b.head);
            std::string why = "SSTF: R" + std::to_string(id) + " (" + cyl(req[id]) + ") is nearest, distance " +
                              std::to_string(d);
            // Mention the runner-up so the decision is explainable.
            int second = -1;
            for (size_t i = 0; i < pending.size(); ++i)
                if (i != best && (second < 0 || std::abs(req[pending[i]] - b.head) < std::abs(req[second] - b.head)))
                    second = pending[i];
            if (second >= 0) {
                int d2 = std::abs(req[second] - b.head);
                why += "; next best R" + std::to_string(second) + " at distance " + std::to_string(d2);
                if (d2 == d) why += " (tie -> earlier request wins)";
            }
            pending.erase(pending.begin() + best);
            b.move(req[id], MoveKind::Service, id, why, d == (second >= 0 ? std::abs(req[second] - b.head) : -1) ? 2 : 1);
        }
    } else {
        bool up = cfg.dir == DiskDir::Higher;
        bool circular = algo == DiskAlgo::CSCAN || algo == DiskAlgo::CLOOK;
        bool toEnd = algo == DiskAlgo::SCAN || algo == DiskAlgo::CSCAN;
        // Requests sorted by cylinder (stable: equal cylinders keep input order).
        auto sortedSide = [&](bool upward) {
            std::vector<int> s;
            for (int id : pending)
                if (upward ? req[id] >= b.head : req[id] <= b.head) s.push_back(id);
            std::stable_sort(s.begin(), s.end(), [&](int x, int y) {
                return upward ? req[x] < req[y] : req[x] > req[y];
            });
            return s;
        };
        auto take = [&](int id) { pending.erase(std::find(pending.begin(), pending.end(), id)); };
        // 1) requests exactly at the head position (distance 0)
        for (int id : std::vector<int>(pending))
            if (req[id] == b.head) { take(id); b.move(b.head, MoveKind::Service, id, "R" + std::to_string(id) + " is at the head position", 0); }
        bool wrapped = false;
        int rounds = 0;
        while (!pending.empty() && rounds++ < 4) {
            // 2) sweep in the current direction
            for (int id : sortedSide(up)) {
                take(id);
                b.move(req[id], MoveKind::Service, id, std::string(diskAlgoName(algo)) + ": moving " +
                       (up ? "up" : "down") + ", next request on the way is R" + std::to_string(id) + " (" +
                       cyl(req[id]) + ")", 1);
            }
            if (pending.empty()) break;
            int end = up ? cfg.maxCyl : cfg.minCyl;
            if (toEnd && b.head != end)
                b.move(end, MoveKind::Boundary, -1, std::string(diskAlgoName(algo)) +
                       ": requests remain behind the head, so it travels to the end of the disk (" + cyl(end) +
                       "); no request is serviced here", 3);
            if (!circular) {
                up = !up;                                     // SCAN / LOOK reverse
                continue;
            }
            // circular: wrap to the other end (C-SCAN) or jump to the furthest request (C-LOOK)
            if (algo == DiskAlgo::CSCAN) {
                int other = up ? cfg.minCyl : cfg.maxCyl;
                b.move(other, MoveKind::Wrap, -1, "C-SCAN: wrap from " + cyl(b.head) + " to the other end " + cyl(other) +
                       " (distance counted, nothing serviced while returning)", 4);
                // requests exactly at the wrap end
                for (int id : std::vector<int>(pending))
                    if (req[id] == b.head) { take(id); b.move(b.head, MoveKind::Service, id, "R" + std::to_string(id) + " is at the wrap end", 5); }
            } else {
                // furthest remaining request in the opposite region (lowest if moving up)
                int far = -1;
                for (int id : pending)
                    if (far < 0 || (up ? req[id] < req[far] : req[id] > req[far])) far = id;
                take(far);
                b.move(req[far], MoveKind::Wrap, far, "C-LOOK: jump back to the furthest remaining request R" +
                       std::to_string(far) + " (" + cyl(req[far]) + "), distance counted", 3);
                for (int id : std::vector<int>(pending))   // duplicates at the same cylinder
                    if (req[id] == b.head) { take(id); b.move(b.head, MoveKind::Service, id, "R" + std::to_string(id) + " is at the same cylinder", 4); }
            }
            wrapped = true;
        }
        (void)wrapped;
    }
    b.r.average = b.r.serviced > 0 ? double(b.r.total) / b.r.serviced : 0.0;
    return b.r;
}

std::vector<DiskResult> runAllDisk(const DiskConfig& cfg) {
    std::vector<DiskResult> v;
    for (DiskAlgo a : allDiskAlgos()) v.push_back(runDisk(a, cfg));
    return v;
}

std::vector<int> randomDiskRequests(int count, int minCyl, int maxCyl, unsigned seed) {
    if (count < 0 || count > 500) throw std::invalid_argument("request count must be between 0 and 500");
    if (maxCyl <= minCyl) throw std::invalid_argument("invalid cylinder range");
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(minCyl, maxCyl);
    std::vector<int> v;
    for (int i = 0; i < count; ++i) v.push_back(d(rng));
    return v;
}

DiskConfig loadDiskFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    DiskConfig c;
    c.requests.clear();
    std::string line;
    int lineNo = 0;
    bool haveReq = false;
    while (std::getline(in, line)) {
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key)) continue;
        auto bad = [&](const std::string& m) {
            throw std::runtime_error(path + " line " + std::to_string(lineNo) + ": " + m);
        };
        if (key == "range") { if (!(ss >> c.minCyl >> c.maxCyl)) bad("expected: range <first> <last>"); }
        else if (key == "head") { if (!(ss >> c.head)) bad("expected: head <cylinder>"); }
        else if (key == "direction") {
            std::string d;
            ss >> d;
            if (d == "higher" || d == "up") c.dir = DiskDir::Higher;
            else if (d == "lower" || d == "down") c.dir = DiskDir::Lower;
            else bad("direction must be higher or lower");
        } else if (key == "requests") {
            std::string tok;
            haveReq = true;
            while (ss >> tok) {
                for (char& ch : tok) if (ch == ',') ch = ' ';
                std::istringstream ts(tok);
                int v;
                while (ts >> v) c.requests.push_back(v);
                if (!ts.eof()) bad("bad request value '" + tok + "'");
            }
        } else bad("unknown key '" + key + "'");
    }
    if (!haveReq) throw std::runtime_error(path + ": no 'requests' line");
    try { validateDiskConfig(c); } catch (const std::exception& e) { throw std::runtime_error(path + ": " + e.what()); }
    return c;
}
