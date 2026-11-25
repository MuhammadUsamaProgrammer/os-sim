// memory.cpp - FIFO / LRU / Optimal page replacement and a simple paging model.
#include "memory.h"
#include <climits>
#include <sstream>
#include <stdexcept>

// Display name of a page replacement algorithm.
const char* pageAlgoName(PageAlgo a) {
    switch (a) {
        case PageAlgo::FIFO:    return "FIFO";
        case PageAlgo::LRU:     return "LRU";
        case PageAlgo::OPTIMAL: return "Optimal";
    }
    return "?";
}

FramePool::FramePool(PageAlgo algo, int numFrames)
    : algo_(algo), frame_(numFrames, -1), loaded_(numFrames, 0), lastUse_(numFrames, 0) {
    if (numFrames < 1) throw std::invalid_argument("number of frames must be >= 1");
}

// Chooses the frame to replace when memory is full (the replacement policy).
// FIFO: oldest loaded. LRU: least recently used. OPTIMAL: next use furthest away
// (never used again = infinitely far). Ties -> page loaded earliest.
int FramePool::chooseVictim(const std::function<long(long)>& nextUse) const {
    int victim = 0;
    long bestKey = 0;
    for (int f = 0; f < (int)frame_.size(); ++f) {
        long key;                                  // bigger key = better victim
        if (algo_ == PageAlgo::FIFO)      key = -loaded_[f];
        else if (algo_ == PageAlgo::LRU)  key = -lastUse_[f];
        else                              key = nextUse(frame_[f]);
        bool better = (f == 0) || key > bestKey ||
                      (key == bestKey && loaded_[f] < loaded_[victim]);
        if (better) { victim = f; bestKey = key; }
    }
    return victim;
}

// One memory reference: hit if resident, otherwise page fault -> load into the
// first empty frame, or replace a victim chosen by the algorithm.
FramePool::Access FramePool::access(long key, const std::function<long(long)>& nextUse) {
    ++clock_;
    for (int f = 0; f < (int)frame_.size(); ++f)
        if (frame_[f] == key) { lastUse_[f] = clock_; return {false, f, -1}; }   // hit
    int target = -1;
    for (int f = 0; f < (int)frame_.size() && target < 0; ++f)
        if (frame_[f] == -1) target = f;                 // free frame: fill 0, 1, 2 ...
    long evicted = -1;
    if (target < 0) { target = chooseVictim(nextUse); evicted = frame_[target]; }
    frame_[target] = key; loaded_[target] = clock_; lastUse_[target] = clock_;
    return {true, target, evicted};
}

// Frees all frames whose page matches pred (process exit returns its frames).
void FramePool::freeIf(const std::function<bool(long)>& pred) {
    for (long& k : frame_) if (k != -1 && pred(k)) k = -1;
}

// Runs a whole reference string and records the frame contents after each step.
MemResult runPageReplacement(PageAlgo algo, const std::vector<int>& refs, int numFrames) {
    for (int p : refs) if (p < 0) throw std::invalid_argument("page numbers must be >= 0");
    FramePool pool(algo, numFrames);
    MemResult r;
    r.algo = algo; r.numFrames = numFrames;
    for (size_t i = 0; i < refs.size(); ++i) {
        // Optimal needs the future: the index of the next reference to `key` after i.
        auto nextUse = [&](long key) -> long {
            for (size_t j = i + 1; j < refs.size(); ++j) if (refs[j] == key) return (long)j;
            return LONG_MAX;
        };
        FramePool::Access a = pool.access(refs[i], nextUse);
        MemStep s;
        s.page = refs[i]; s.fault = a.fault; s.replaced = (int)a.evicted;
        for (long k : pool.frames()) s.frames.push_back((int)k);
        r.steps.push_back(s);
        if (a.fault) r.faults++; else r.hits++;
    }
    r.hitRatio = refs.empty() ? 0.0 : (double)r.hits / refs.size();
    return r;
}

// Accepts commas and/or whitespace as separators.
std::vector<int> parseRefString(const std::string& text) {
    std::string s = text;
    for (char& c : s) if (c == ',' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    std::stringstream ss(s);
    std::string tok;
    std::vector<int> out;
    while (ss >> tok) {
        size_t used = 0;
        int v = 0;
        try { v = std::stoi(tok, &used); } catch (const std::exception&) { used = 0; }
        if (used == 0 || used != tok.size())
            throw std::invalid_argument("bad page number '" + tok + "'");
        if (v < 0) throw std::invalid_argument("negative page number " + tok);
        out.push_back(v);
    }
    return out;
}

// Equal allocation: each process gets the same number of consecutive frames.
int FrameAllocator::allocateEqual(const std::vector<int>& pids) {
    frameOwner.assign(totalFrames, -1);
    if (pids.empty()) return 0;
    int share = totalFrames / (int)pids.size();
    for (size_t i = 0; i < pids.size(); ++i)
        for (int k = 0; k < share; ++k) frameOwner[i * share + k] = pids[i];
    return share;
}

// Lists the frames that belong to a pid.
std::vector<int> FrameAllocator::framesOf(int pid) const {
    std::vector<int> v;
    for (int f = 0; f < totalFrames; ++f) if (frameOwner[f] == pid) v.push_back(f);
    return v;
}

// Builds a page table: page i -> i-th owned frame (valid) while frames last.
PageTable buildPageTable(int pid, int pages, const FrameAllocator& fa) {
    PageTable pt;
    pt.pid = pid;
    pt.entries.resize(pages > 0 ? pages : 0);
    std::vector<int> owned = fa.framesOf(pid);
    for (int i = 0; i < (int)pt.entries.size() && i < (int)owned.size(); ++i)
        pt.entries[i] = {owned[i], true};
    return pt;
}
