// memory.h - paging and page replacement (OS concept: memory management).
// FIFO, LRU and Optimal replacement over a fixed number of physical frames,
// plus a simple page-table / frame-allocator model.
#pragma once
#include <functional>
#include <string>
#include <vector>

enum class PageAlgo { FIFO, LRU, OPTIMAL };
const char* pageAlgoName(PageAlgo);

struct MemStep {
    int page;                    // page referenced
    std::vector<int> frames;     // frame contents after this reference, -1 = empty
    bool fault;
    int replaced;                // page evicted, or -1
};
struct MemResult {
    PageAlgo algo = PageAlgo::FIFO; int numFrames = 0;
    std::vector<MemStep> steps;
    int faults = 0, hits = 0; double hitRatio = 0;   // hitRatio = hits / steps.size()
};
// Simulates a reference string. Throws std::invalid_argument for numFrames < 1 or negative pages.
MemResult runPageReplacement(PageAlgo, const std::vector<int>& refs, int numFrames);
// Parses "1, 2 3,4" -> {1,2,3,4}. Throws std::invalid_argument on bad tokens or negatives.
std::vector<int> parseRefString(const std::string&);

// A pool of physical frames managed by one replacement algorithm. Pages are
// identified by an integer key (a page number, or pid*1000+page in the integrated run).
class FramePool {
public:
    struct Access { bool fault; int frame; long evicted; };   // evicted = -1 if none
    FramePool(PageAlgo algo, int numFrames);
    // References a page. nextUse(key) must return the position of the next future
    // reference of key (or a huge number if never); it is only called for OPTIMAL.
    Access access(long key, const std::function<long(long)>& nextUse);
    const std::vector<long>& frames() const { return frame_; }   // -1 = empty frame
    // Removes every page that belongs to a predicate (e.g. a terminated process).
    void freeIf(const std::function<bool(long)>& pred);
private:
    int chooseVictim(const std::function<long(long)>& nextUse) const;
    PageAlgo algo_;
    std::vector<long> frame_;     // page key held by each frame
    std::vector<long> loaded_;    // order in which the page was loaded (FIFO / ties)
    std::vector<long> lastUse_;   // order of last reference (LRU)
    long clock_ = 0;              // logical access counter
};

// Simple paging model for the integrated run and the page-table panel
struct PageTableEntry { int frame = -1; bool valid = false; };
struct PageTable { int pid; std::vector<PageTableEntry> entries; };
struct FrameAllocator {          // equal-share allocation of frames among processes
    int totalFrames = 0; std::vector<int> frameOwner;   // pid or -1
    explicit FrameAllocator(int total = 0) : totalFrames(total), frameOwner(total, -1) {}
    // Gives each process totalFrames / pids.size() consecutive frames; returns that number.
    int allocateEqual(const std::vector<int>& pids);
    // Frames owned by one pid, in increasing order.
    std::vector<int> framesOf(int pid) const;
};
// Page table of a process with `pages` pages: its first k pages are resident in
// its k allocated frames (valid), the rest are on disk (invalid).
PageTable buildPageTable(int pid, int pages, const FrameAllocator& fa);
