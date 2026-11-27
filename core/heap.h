// heap.h - Phase 3 "Heap Laboratory": a genuinely working educational allocator over a bounded,
// application-owned byte arena (std::vector<uint8_t>). Pure C++17, deterministic, no Qt.
//
// It is NOT the C++ allocator and never touches memory outside its own arena: every block header
// is really stored inside the arena bytes, every access is a checked arena offset, and users refer
// to allocations through handles (the "pointer" shown is the arena-relative payload offset).
//
// Two allocator families (fixed for a session - changing family means reset):
//   FreeList: aligned headers, address-ordered explicit free list, First / Best / Worst Fit,
//             splitting with a minimum split size, immediate coalescing of adjacent free blocks.
//   Buddy:    power-of-two blocks, split in halves, merge with the buddy (offset XOR size).
//
// Header layout (16 bytes, little endian, rounded up to the alignment):
//   [0] u32 block size (header included)   [4] u32 requested payload bytes (0 = free)
//   [8] u32 magic (allocated / free)        [12] u32 next free block offset (free) or handle (allocated)
// Conventions (documented): malloc(0) returns a unique minimum block that must be freed;
// free(0) (the null handle) does nothing; realloc(0, n) = malloc(n); realloc(h, 0) frees h and
// returns 0; a failed realloc leaves the old allocation unchanged.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "memadv.h"      // FitAlgo / fitAlgoName (same policy names as contiguous allocation)

enum class HeapFamily { FreeList, Buddy };
const char* heapFamilyName(HeapFamily);

struct HeapConfig {
    HeapFamily family = HeapFamily::FreeList;
    FitAlgo fit = FitAlgo::First;
    int capacity = 1024;      // bytes; 128..8192 (power of two for Buddy)
    int alignment = 16;       // 8, 16 or 32
};
void validateHeapConfig(const HeapConfig&);   // throws std::invalid_argument

struct HeapBlock {
    int offset = 0, size = 0;          // whole block, header included
    int header = 0;                     // header bytes
    int requested = 0;                  // payload requested by the user (0 if free)
    int padding = 0;                    // size - header - requested (allocated blocks)
    bool free = true;
    int handle = 0;                     // allocated: handle id
    int nextFree = -1;                  // free: next block in its free list (-1 = end)
    int order = 0;                      // buddy: log2(size)
    int payloadOffset() const { return offset + header; }
};

struct HeapMetrics {
    int capacity = 0;
    int liveRequested = 0;              // payload bytes requested by live allocations
    int headerBytes = 0;                // metadata of every block (allocated and free)
    int paddingBytes = 0;               // alignment / rounding / unsplit remainder inside allocated blocks
    int freeBytes = 0;                  // total size of free blocks (their headers included)
    int largestFree = 0;                // largest free block (header included)
    int largestRequest = 0;             // largest payload a single request could get now
    int liveAllocations = 0, allocations = 0, frees = 0, failures = 0;
    bool fragDefined = false;           // false when nothing is free
    double fragIndicator = 0;           // 1 - largestFree / freeBytes (chosen indicator)
};

struct HeapResult {
    bool ok = false;
    int handle = 0;                     // new / resulting handle (0 = null)
    int offset = -1;                    // payload offset
    std::string message;
    std::string data;                   // read()
    std::vector<std::string> events;    // split / coalesce / move / buddy steps of this call
};

class Heap {
public:
    explicit Heap(const HeapConfig& cfg = HeapConfig());
    const HeapConfig& config() const { return cfg_; }
    int headerSize() const { return header_; }
    int minBlock() const;                   // smallest block (header + one alignment unit / buddy minimum)

    HeapResult malloc(int n);
    HeapResult calloc(long long count, long long size);
    HeapResult realloc(int handle, int n);
    HeapResult free(int handle);
    HeapResult write(int handle, int offset, const std::string& bytes);   // bounds-checked against the request
    HeapResult read(int handle, int offset, int n) const;
    // Controlled corruption INSIDE the model: writes `extra` bytes past the requested size (into
    // padding and possibly the next header). check() then reports it; the heap refuses further
    // operations until reset. No real out-of-bounds C++ access happens.
    HeapResult overflow(int handle, int extra);

    std::vector<HeapBlock> blocks() const;              // address order, decoded from the arena
    std::vector<int> freeList(int order = -1) const;    // block offsets in list order (buddy: per order)
    HeapMetrics metrics() const;
    std::vector<std::string> check() const;             // empty = consistent heap
    std::vector<std::pair<int, int>> outstanding() const;   // live handles and their bytes (potential leaks)
    bool corrupted() const { return corrupted_; }
    const std::vector<uint8_t>& arena() const { return arena_; }
    int handleOffset(int handle) const;                 // payload offset, -1 if not live
    int handleSize(int handle) const;                   // requested bytes, -1 if not live
    bool handleLive(int handle) const;

private:
    uint32_t get(int off, int field) const;
    void put(int off, int field, uint32_t v);
    void makeBlock(int off, int size, bool isFree, int requested, uint32_t link);
    int roundUp(int n) const { return (n + cfg_.alignment - 1) / cfg_.alignment * cfg_.alignment; }
    HeapResult fail(const std::string& why);
    HeapResult guard() const;               // corrupted heap -> refuse
    // free-list family
    int pick(int need) const;
    void unlinkFree(int off);
    void insertFree(int off, std::vector<std::string>& ev);   // address order + coalescing
    HeapResult allocFreeList(int n, std::vector<std::string>& ev);
    // buddy family
    int orderFor(int need) const;
    void pushBuddy(int off, int order);
    bool popBuddy(int off, int order);
    HeapResult allocBuddy(int n, std::vector<std::string>& ev);
    void freeBuddy(int off, std::vector<std::string>& ev);
    HeapResult allocate(int n, std::vector<std::string>& ev);
    void release(int off, std::vector<std::string>& ev);

    HeapConfig cfg_;
    int header_ = 16;
    std::vector<uint8_t> arena_;
    int freeHead_ = -1;                     // free-list family
    std::vector<std::vector<int>> buddyFree_;   // buddy: free blocks per order (address sorted)
    int maxOrder_ = 0, minOrder_ = 5;
    struct Alloc { int offset = 0; int requested = 0; bool live = false; };
    std::map<int, Alloc> handles_;
    int nextHandle_ = 1;
    int allocations_ = 0, frees_ = 0, failures_ = 0;
    bool corrupted_ = false;
};

// ---- deterministic sessions for playback / undo / presets
// Script lines (';' or newline separated):  a = malloc 100 | b = calloc 4 25 | a = realloc a 200 |
// free a | write a 0 hello | read a 0 5 | overflow a 8 | free 0
struct HeapOp { std::string text; };
struct HeapStep {
    std::string op;                     // the script line
    HeapResult result;
    std::vector<HeapBlock> blocks;
    std::vector<int> freeList;
    HeapMetrics metrics;
    std::vector<uint8_t> arena;
    std::map<std::string, int> names;   // variable -> handle
    std::vector<std::string> integrity; // check() after the step
    int movedFrom = -1, movedTo = -1;   // realloc that moved the payload
};
struct HeapSession {
    HeapConfig cfg;
    std::vector<HeapStep> steps;        // steps[0] = empty heap
};
std::vector<std::string> splitHeapScript(const std::string& script);
// Runs the script deterministically; a bad line becomes a failed step (never throws for script errors).
HeapSession runHeapScript(const HeapConfig& cfg, const std::vector<std::string>& lines);

struct HeapPreset { std::string name, description, script; HeapConfig cfg; };
std::vector<HeapPreset> heapPresets();
