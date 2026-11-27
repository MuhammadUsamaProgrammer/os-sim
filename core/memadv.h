// memadv.h - advanced memory management (M4), extending core/memory.h (no duplication):
//   1. contiguous allocation: First / Best / Worst Fit with splitting and coalescing, fixed
//      partitions (internal fragmentation), compaction, base/limit protection;
//   2. paging address translation (page table, valid / present / write bits);
//   3. segmentation (segment table, base + limit, permissions);
//   4. demand paging with dirty bits and write-back, REUSING FramePool (FIFO / LRU / Optimal)
//      from core/memory.h; whole-process swapping scenario;
//   5. thrashing: rolling fault rate, working set, multiprogramming-degree model.
// Every value shown in the GUI is computed here; all functions are deterministic.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "memory.h"

// ---------------------------------------------------------------- 1. contiguous allocation
enum class FitAlgo { First, Best, Worst };
const char* fitAlgoName(FitAlgo);

struct MemBlock {
    int start = 0, size = 0;
    std::string owner;        // "" = free hole, "OS" = reserved
    int requested = 0;        // fixed partitions: bytes actually requested (internal waste = size - requested)
    bool isHole() const { return owner.empty(); }
};

struct AllocOp { enum Kind { Alloc, Free, Compact } kind = Alloc; std::string name; int size = 0; };

struct AllocStep {
    AllocOp op;
    bool ok = true;
    int chosen = -1;                  // start address of the hole / partition chosen (-1 none)
    std::vector<int> candidates;      // start addresses of every hole that was large enough
    std::vector<MemBlock> layout;     // memory after the operation
    std::string text;                 // explanation
    bool externalFragFailure = false; // enough free space in total, but no single hole fits
};

struct AllocMetrics {
    int total = 0, allocated = 0, requested = 0, freeBytes = 0, largestHole = 0, holes = 0;
    int failures = 0, externalFailures = 0, internalWaste = 0;
};

struct AllocResult {
    FitAlgo algo = FitAlgo::First;
    bool fixedPartitions = false;
    std::vector<MemBlock> initial;
    std::vector<AllocStep> steps;
    AllocMetrics metrics(int stepCount) const;   // metrics after the first stepCount operations
};

// Memory of `holes` separated by small allocated blocks (so they never coalesce), after an
// OS region. Used by the Phase 2 preset (holes 100, 500, 200, 300, 600).
std::vector<MemBlock> layoutFromHoles(const std::vector<int>& holes, int osSize = 100, int separator = 50);
// Fixed partitions after an OS region.
std::vector<MemBlock> fixedPartitions(const std::vector<int>& sizes, int osSize = 100);
// Runs the operations. Throws std::invalid_argument on a bad layout or operation.
AllocResult runAllocation(FitAlgo algo, const std::vector<MemBlock>& initial, const std::vector<AllocOp>& ops,
                          bool fixedPartitionMode = false);
// "A 212, B 417, free A, compact" style script -> operations.
std::vector<AllocOp> parseAllocScript(const std::string& text);

// Base / limit check: valid when 0 <= offset < limit; physical = base + offset.
struct BaseLimitResult { bool ok; int64_t physical; std::string message; };
BaseLimitResult baseLimitCheck(int64_t base, int64_t limit, int64_t offset);

// ---------------------------------------------------------------- 2. paging
struct PageEntry { int frame = -1; bool valid = false; bool present = false; bool writable = true; };
struct PagingConfig {
    uint64_t logicalSize = 8192, physicalSize = 8192, pageSize = 1024;
    std::vector<PageEntry> table;    // one entry per logical page
    bool allowShared = false;        // two pages may map to the same frame (labelled shared)
};
// Throws std::invalid_argument (power of two, sizes, table length, frame range, duplicates).
void validatePaging(const PagingConfig& c);

enum class XlateOutcome { Ok, OutOfRange, InvalidPage, PageFault, Protection, SegmentMissing };
const char* xlateOutcomeName(XlateOutcome);
struct XlateStep { std::string title, detail; };
struct XlateResult {
    XlateOutcome outcome = XlateOutcome::Ok;
    uint64_t address = 0, page = 0, offset = 0, frame = 0, physical = 0;
    int pageBits = 0, offsetBits = 0;
    std::vector<XlateStep> steps;    // validate, split, lookup, check, frame, compute, highlight
    std::string message;
    bool ok() const { return outcome == XlateOutcome::Ok; }
};
XlateResult translatePaging(const PagingConfig& c, uint64_t logical, bool write = false);
// The specification's preset: 8 pages of 1024 bytes, page 2 -> frame 5 (others mapped / not present).
PagingConfig pagingPreset();

// ---------------------------------------------------------------- 3. segmentation
struct SegmentDesc { int id = 0; std::string name; uint64_t base = 0, limit = 0; bool read = true, write = true, exec = false; };
void validateSegments(const std::vector<SegmentDesc>& segs, uint64_t physicalSize);
// access: 'r', 'w' or 'x'
XlateResult translateSegment(const std::vector<SegmentDesc>& segs, uint64_t physicalSize, int segment, uint64_t offset, char access = 'r');
std::vector<SegmentDesc> segmentPreset();   // includes segment 2: base 4000, limit 600

// ---------------------------------------------------------------- 4. demand paging and swapping
struct DpRef { int page = 0; bool write = false; };
std::vector<DpRef> parseDpRefs(const std::string& text);   // "7, 0w, 1" (w = write); throws invalid_argument

struct DpStep {
    DpRef ref;
    bool hit = false;
    int frame = -1;                   // frame used by the referenced page
    int victim = -1;                  // evicted page, -1 none
    bool victimDirty = false;         // the victim was modified -> written back to the backing store
    std::vector<int> frames;          // page in each frame after the step (-1 empty)
    std::vector<bool> dirty;          // dirty bit per frame after the step
    std::vector<std::string> actions; // check PT -> (free frame | victim) -> write back -> load -> update PT -> restart
};
struct DpCost { int access = 1, pageIn = 100, writeBack = 100; };   // MODEL parameters (logical cost units)
struct DpResult {
    PageAlgo algo = PageAlgo::FIFO;
    int frames = 0, faults = 0, hits = 0, writeBacks = 0;
    std::vector<DpStep> steps;
    long cost(const DpCost& c) const { return (long)steps.size() * c.access + (long)faults * c.pageIn + (long)writeBacks * c.writeBack; }
};
DpResult runDemandPaging(PageAlgo algo, const std::vector<DpRef>& refs, int frames);

struct SwapProc { std::string name; int size = 0; };
struct SwapStep {
    std::string run;                          // process that wants to run
    std::vector<std::string> swappedOut, swappedIn;
    std::vector<std::string> resident;        // in RAM after the step (load order)
    std::vector<std::string> backing;         // on the backing store after the step
    int freeAfter = 0;
    std::string text;
};
struct SwapResult { int capacity = 0; std::vector<SwapProc> procs; std::vector<SwapStep> steps; int swapIns = 0, swapOuts = 0; };
// Whole-process swapping (illustrative): to run a process it must be entirely in RAM; the
// process loaded longest ago is swapped out until it fits (no host memory is touched).
SwapResult runSwapping(int capacity, const std::vector<SwapProc>& procs, const std::vector<std::string>& runOrder);

// ---------------------------------------------------------------- 5. thrashing
struct ThrashPoint { bool fault = false; double rollingRate = 0; int occupied = 0; int workingSet = 0; };
struct ThrashResult { int frames = 0, faults = 0, hits = 0, window = 0, wsWindow = 0; std::vector<int> refs; std::vector<ThrashPoint> points; };
ThrashResult runThrashing(const std::vector<int>& refs, int frames, PageAlgo algo, int window = 8, int wsWindow = 6);
std::vector<int> cyclicRefs(int pages, int repeats);
// Locality model: with probability locality% the next page is within +-1 of the current one.
std::vector<int> localityRefs(int length, int pages, int locality, unsigned seed);

// Multiprogramming model (logical, labelled): d processes share F frames equally; each loops
// over its own working set of W pages (refsPerProcess references, LRU). A hit costs 1 CPU tick;
// a fault costs 1 CPU tick plus faultTime ticks of the single paging disk, during which THAT
// process waits (other processes may use the CPU). makespan = max(total CPU ticks, total disk
// ticks, the longest single process (its refs + its faults x faultTime)); utilisation = CPU / makespan.
struct MultiprogPoint { int degree = 0, framesPerProc = 0, faults = 0, refs = 0; long cpuTicks = 0, diskTicks = 0; double utilisation = 0; };
std::vector<MultiprogPoint> multiprogrammingCurve(int totalFrames, int workingSet, int maxDegree, int refsPerProcess, int faultTime);
