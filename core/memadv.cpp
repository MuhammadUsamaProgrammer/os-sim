// memadv.cpp - advanced memory management models (see memadv.h).
#include "memadv.h"
#include <algorithm>
#include <climits>
#include <deque>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

// ================================================================ 1. contiguous allocation

const char* fitAlgoName(FitAlgo a) {
    switch (a) {
        case FitAlgo::First: return "First Fit";
        case FitAlgo::Best: return "Best Fit";
        case FitAlgo::Worst: return "Worst Fit";
    }
    return "?";
}

std::vector<MemBlock> layoutFromHoles(const std::vector<int>& holes, int osSize, int separator) {
    std::vector<MemBlock> v;
    int at = 0;
    if (osSize > 0) { v.push_back({0, osSize, "OS", 0}); at = osSize; }
    for (size_t i = 0; i < holes.size(); ++i) {
        v.push_back({at, holes[i], "", 0});
        at += holes[i];
        if (i + 1 < holes.size() && separator > 0) {
            std::string n = "used" + std::to_string(i + 1);
            v.push_back({at, separator, n, separator});
            at += separator;
        }
    }
    return v;
}

std::vector<MemBlock> fixedPartitions(const std::vector<int>& sizes, int osSize) {
    std::vector<MemBlock> v;
    int at = 0;
    if (osSize > 0) { v.push_back({0, osSize, "OS", 0}); at = osSize; }
    for (int s : sizes) { v.push_back({at, s, "", 0}); at += s; }
    return v;
}

static void validateLayout(const std::vector<MemBlock>& l) {
    if (l.empty()) throw std::invalid_argument("memory layout is empty");
    int at = l.front().start;
    if (at != 0) throw std::invalid_argument("memory must start at address 0");
    for (const MemBlock& b : l) {
        if (b.size <= 0) throw std::invalid_argument("every block must have a positive size");
        if (b.start != at) throw std::invalid_argument("blocks must be contiguous and non-overlapping");
        at += b.size;
        if (at > 1 << 26) throw std::invalid_argument("memory larger than 64 MiB units is not supported");
    }
}

// Merges adjacent free holes (variable partitions only).
static void coalesce(std::vector<MemBlock>& l) {
    for (size_t i = 0; i + 1 < l.size();) {
        if (l[i].isHole() && l[i + 1].isHole()) { l[i].size += l[i + 1].size; l.erase(l.begin() + i + 1); }
        else ++i;
    }
}

static std::string blockName(const MemBlock& b) { return "[" + std::to_string(b.start) + ".." + std::to_string(b.start + b.size) + ")"; }

AllocResult runAllocation(FitAlgo algo, const std::vector<MemBlock>& initial, const std::vector<AllocOp>& ops, bool fixedMode) {
    validateLayout(initial);
    if (ops.size() > 200) throw std::invalid_argument("at most 200 operations");
    AllocResult r;
    r.algo = algo;
    r.fixedPartitions = fixedMode;
    r.initial = initial;
    std::vector<MemBlock> mem = initial;
    for (const AllocOp& op : ops) {
        AllocStep s;
        s.op = op;
        if (op.kind == AllocOp::Alloc) {
            if (op.size <= 0) throw std::invalid_argument("allocation size must be positive (" + op.name + ")");
            if (op.name.empty() || op.name == "OS") throw std::invalid_argument("allocation needs a process name");
            for (const MemBlock& b : mem)
                if (b.owner == op.name) throw std::invalid_argument("'" + op.name + "' is already allocated");
            // holes that are large enough, in address order (First Fit keeps the original order)
            int best = -1;
            for (int i = 0; i < (int)mem.size(); ++i) {
                if (!mem[i].isHole() || mem[i].size < op.size) continue;
                s.candidates.push_back(mem[i].start);
                if (best < 0) { best = i; if (algo == FitAlgo::First) continue; }
                else if (algo == FitAlgo::Best && mem[i].size < mem[best].size) best = i;      // ties: lowest address
                else if (algo == FitAlgo::Worst && mem[i].size > mem[best].size) best = i;
            }
            if (algo == FitAlgo::First && !s.candidates.empty()) {
                best = -1;
                for (int i = 0; i < (int)mem.size(); ++i) if (mem[i].isHole() && mem[i].size >= op.size) { best = i; break; }
            }
            if (best < 0) {
                s.ok = false;
                int freeTotal = 0;
                for (const MemBlock& b : mem) if (b.isHole()) freeTotal += b.size;
                s.externalFragFailure = freeTotal >= op.size;
                s.text = op.name + " (" + std::to_string(op.size) + ") cannot be placed: no " +
                         (fixedMode ? std::string("free partition") : std::string("hole")) + " is large enough" +
                         (s.externalFragFailure ? " although " + std::to_string(freeTotal) +
                                                      " units are free in total (external fragmentation)" : "");
            } else {
                MemBlock& h = mem[best];
                s.chosen = h.start;
                std::string why = algo == FitAlgo::First ? "the first hole that is large enough"
                                 : algo == FitAlgo::Best ? "the smallest hole that is large enough"
                                                         : "the largest hole";
                if (fixedMode) {
                    h.owner = op.name;
                    h.requested = op.size;
                    s.text = op.name + " (" + std::to_string(op.size) + ") -> partition " + blockName(h) + " of " +
                             std::to_string(h.size) + ": " + std::to_string(h.size - op.size) + " units wasted inside (internal fragmentation)";
                } else {
                    int left = h.size - op.size;
                    s.text = op.name + " (" + std::to_string(op.size) + ") -> hole " + blockName(h) + " of " + std::to_string(h.size) +
                             " (" + why + "); split, leftover hole " + std::to_string(left);
                    MemBlock used{h.start, op.size, op.name, op.size};
                    if (left > 0) { h.start += op.size; h.size = left; mem.insert(mem.begin() + best, used); }
                    else { h = used; }
                }
            }
        } else if (op.kind == AllocOp::Free) {
            int idx = -1;
            for (int i = 0; i < (int)mem.size(); ++i) if (mem[i].owner == op.name) idx = i;
            if (idx < 0 || op.name == "OS") {
                s.ok = false;
                s.text = "free " + op.name + ": no such allocation";
            } else {
                s.chosen = mem[idx].start;
                mem[idx].owner.clear();
                mem[idx].requested = 0;
                size_t before = mem.size();
                if (!fixedMode) coalesce(mem);
                s.text = "free " + op.name + ": its block becomes a hole" +
                         (mem.size() < before ? " and is merged (coalesced) with the adjacent hole(s)" : "");
            }
        } else {   // compaction
            if (fixedMode) {
                s.ok = false;
                s.text = "compaction does not apply to fixed partitions";
            } else {
                std::vector<MemBlock> out;
                int at = 0, moved = 0, total = 0;
                for (const MemBlock& b : mem) total += b.size;
                for (const MemBlock& b : mem) {
                    if (b.isHole()) continue;
                    MemBlock c = b;
                    if (c.start != at) ++moved;
                    c.start = at;                 // relocation: the block's base register changes
                    at += c.size;
                    out.push_back(c);
                }
                if (at < total) out.push_back({at, total - at, "", 0});
                mem = out;
                s.text = "compaction: " + std::to_string(moved) + " block(s) moved down and their base addresses updated "
                         "(needs run-time relocation); all free space is now one hole of " + std::to_string(total - at);
            }
        }
        s.layout = mem;
        r.steps.push_back(s);
    }
    return r;
}

AllocMetrics AllocResult::metrics(int stepCount) const {
    AllocMetrics m;
    const std::vector<MemBlock>& l = stepCount <= 0 || steps.empty() ? initial : steps[std::min<int>(stepCount, (int)steps.size()) - 1].layout;
    for (const MemBlock& b : l) {
        m.total += b.size;
        if (b.isHole()) { m.freeBytes += b.size; m.largestHole = std::max(m.largestHole, b.size); ++m.holes; }
        else if (b.owner != "OS") {
            m.allocated += b.size;
            m.requested += b.requested > 0 ? b.requested : b.size;
            if (fixedPartitions) m.internalWaste += b.size - b.requested;
        }
    }
    for (int i = 0; i < stepCount && i < (int)steps.size(); ++i)
        if (!steps[i].ok && steps[i].op.kind == AllocOp::Alloc) { ++m.failures; m.externalFailures += steps[i].externalFragFailure; }
    return m;
}

std::vector<AllocOp> parseAllocScript(const std::string& text) {
    std::vector<AllocOp> ops;
    std::string s = text;
    for (char& c : s) if (c == ';' || c == '\n') c = ',';
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::istringstream is(item);
        std::string a, b;
        if (!(is >> a)) continue;
        AllocOp op;
        if (a == "compact") { op.kind = AllocOp::Compact; ops.push_back(op); continue; }
        if (a == "free") {
            if (!(is >> b)) throw std::invalid_argument("'free' needs a name");
            op.kind = AllocOp::Free;
            op.name = b;
        } else {
            op.name = a;
            if (!(is >> b)) throw std::invalid_argument("'" + a + "' needs a size, e.g. " + a + " 212");
            try { size_t u; op.size = std::stoi(b, &u); if (u != b.size()) throw 1; }
            catch (...) { throw std::invalid_argument("bad size '" + b + "' for " + a); }
            if (op.size <= 0) throw std::invalid_argument("size must be positive for " + a);
        }
        ops.push_back(op);
    }
    return ops;
}

BaseLimitResult baseLimitCheck(int64_t base, int64_t limit, int64_t offset) {
    if (offset < 0 || offset >= limit)
        return {false, -1, "offset " + std::to_string(offset) + " is outside 0.." + std::to_string(limit - 1) +
                               ": the MMU traps (protection fault), no memory is accessed"};
    return {true, base + offset, "0 <= " + std::to_string(offset) + " < " + std::to_string(limit) + ", physical = base + offset = " +
                                     std::to_string(base) + " + " + std::to_string(offset) + " = " + std::to_string(base + offset)};
}

// ================================================================ 2. paging

const char* xlateOutcomeName(XlateOutcome o) {
    switch (o) {
        case XlateOutcome::Ok: return "translated";
        case XlateOutcome::OutOfRange: return "illegal address (outside the logical address space)";
        case XlateOutcome::InvalidPage: return "invalid page (not part of the process)";
        case XlateOutcome::PageFault: return "page fault (valid page not in memory)";
        case XlateOutcome::Protection: return "protection violation";
        case XlateOutcome::SegmentMissing: return "no such segment";
    }
    return "?";
}

static bool pow2(uint64_t v) { return v && !(v & (v - 1)); }
static int log2u(uint64_t v) { int b = 0; while ((1ULL << b) < v) ++b; return b; }

void validatePaging(const PagingConfig& c) {
    auto need = [](bool ok, const std::string& m) { if (!ok) throw std::invalid_argument(m); };
    need(pow2(c.pageSize) && c.pageSize >= 16 && c.pageSize <= (1ULL << 20), "page size must be a power of two between 16 and 1 MiB");
    need(c.logicalSize >= c.pageSize && c.logicalSize <= (1ULL << 32), "logical address space must be between one page and 4 GiB");
    need(c.physicalSize >= c.pageSize && c.physicalSize <= (1ULL << 32), "physical memory must be between one page and 4 GiB");
    need(c.logicalSize % c.pageSize == 0 && c.physicalSize % c.pageSize == 0, "memory sizes must be whole numbers of pages");
    uint64_t pages = c.logicalSize / c.pageSize, frames = c.physicalSize / c.pageSize;
    need(pages <= 4096, "at most 4096 logical pages can be shown");
    need(c.table.size() == pages, "the page table needs exactly one entry per logical page");
    std::set<int> used;
    for (size_t i = 0; i < c.table.size(); ++i) {
        const PageEntry& e = c.table[i];
        if (!e.valid || !e.present) continue;
        need(e.frame >= 0 && (uint64_t)e.frame < frames, "page " + std::to_string(i) + " maps to frame " + std::to_string(e.frame) +
                                                             " but frames are 0.." + std::to_string(frames - 1));
        need(c.allowShared || used.insert(e.frame).second, "frame " + std::to_string(e.frame) +
                                                              " is mapped twice (enable shared mappings if this is deliberate)");
    }
}

XlateResult translatePaging(const PagingConfig& c, uint64_t a, bool write) {
    validatePaging(c);
    XlateResult r;
    r.address = a;
    r.offsetBits = log2u(c.pageSize);
    r.pageBits = log2u(c.logicalSize / c.pageSize);
    auto step = [&](const std::string& t, const std::string& d) { r.steps.push_back({t, d}); };
    if (a >= c.logicalSize) {
        r.outcome = XlateOutcome::OutOfRange;
        step("1. Validate address", std::to_string(a) + " >= logical size " + std::to_string(c.logicalSize) + " -> trap");
        r.message = "illegal address: " + std::to_string(a) + " is not in 0.." + std::to_string(c.logicalSize - 1);
        return r;
    }
    step("1. Validate address", "0 <= " + std::to_string(a) + " < " + std::to_string(c.logicalSize));
    r.page = a / c.pageSize;
    r.offset = a % c.pageSize;
    step("2. Split address", "page = " + std::to_string(a) + " / " + std::to_string(c.pageSize) + " = " + std::to_string(r.page) +
                                 ", offset = " + std::to_string(a) + " % " + std::to_string(c.pageSize) + " = " + std::to_string(r.offset) +
                                 "  (" + std::to_string(r.pageBits) + " page bits | " + std::to_string(r.offsetBits) + " offset bits)");
    const PageEntry& e = c.table[r.page];
    step("3. Page table lookup", "entry " + std::to_string(r.page) + ": " + (e.valid ? "valid" : "invalid") + ", " +
                                     (e.present ? "present" : "not present") + (e.valid && e.present ? ", frame " + std::to_string(e.frame) : ""));
    if (!e.valid) {
        r.outcome = XlateOutcome::InvalidPage;
        step("4. Check bits", "valid bit = 0 -> illegal access, the process is stopped (not a page fault)");
        r.message = "page " + std::to_string(r.page) + " is not part of the process (invalid)";
        return r;
    }
    if (!e.present) {
        r.outcome = XlateOutcome::PageFault;
        step("4. Check bits", "valid but present bit = 0 -> page fault: the OS loads the page from the backing store and restarts the instruction");
        r.message = "page fault: page " + std::to_string(r.page) + " is valid but not resident";
        return r;
    }
    if (write && !e.writable) {
        r.outcome = XlateOutcome::Protection;
        step("4. Check bits", "write to a read-only page -> protection violation (trap)");
        r.message = "protection violation: page " + std::to_string(r.page) + " is read-only";
        return r;
    }
    step("4. Check bits", std::string("valid = 1, present = 1") + (write ? ", writable = 1" : ""));
    r.frame = (uint64_t)e.frame;
    step("5. Frame", "frame number = " + std::to_string(r.frame));
    uint64_t prod;
    if (__builtin_mul_overflow(r.frame, c.pageSize, &prod) || __builtin_add_overflow(prod, r.offset, &r.physical)) {
        r.outcome = XlateOutcome::OutOfRange;
        r.message = "arithmetic overflow while computing the physical address";
        return r;
    }
    step("6. Physical address", "frame * page size + offset = " + std::to_string(r.frame) + " * " + std::to_string(c.pageSize) +
                                    " + " + std::to_string(r.offset) + " = " + std::to_string(r.physical));
    step("7. Access RAM", "byte " + std::to_string(r.offset) + " inside frame " + std::to_string(r.frame) + " (physical " +
                              std::to_string(r.physical) + ")");
    r.message = "logical " + std::to_string(a) + " -> physical " + std::to_string(r.physical);
    return r;
}

PagingConfig pagingPreset() {
    PagingConfig c;
    c.pageSize = 1024;
    c.logicalSize = 8 * 1024;
    c.physicalSize = 8 * 1024;
    c.table.assign(8, PageEntry{});
    int frames[8] = {1, 4, 5, 7, -1, 2, -1, -1};
    for (int i = 0; i < 8; ++i) {
        c.table[i].valid = i <= 5;                    // pages 6, 7 are not part of the process
        c.table[i].present = frames[i] >= 0;          // page 4 is valid but on disk
        c.table[i].frame = frames[i];
        c.table[i].writable = i != 1;                 // page 1 holds read-only code
    }
    return c;
}

// ================================================================ 3. segmentation

void validateSegments(const std::vector<SegmentDesc>& segs, uint64_t phys) {
    if (phys == 0 || phys > (1ULL << 32)) throw std::invalid_argument("physical memory must be between 1 byte and 4 GiB");
    if (segs.empty() || segs.size() > 32) throw std::invalid_argument("the segment table needs 1 to 32 segments");
    std::set<int> ids;
    for (const SegmentDesc& s : segs) {
        if (!ids.insert(s.id).second) throw std::invalid_argument("duplicate segment number " + std::to_string(s.id));
        if (s.limit == 0) throw std::invalid_argument("segment " + std::to_string(s.id) + " has limit 0");
        if (s.base + s.limit > phys || s.base + s.limit < s.base)
            throw std::invalid_argument("segment " + std::to_string(s.id) + " [" + std::to_string(s.base) + ", " +
                                        std::to_string(s.base + s.limit) + ") does not fit in physical memory of " + std::to_string(phys));
    }
    for (size_t i = 0; i < segs.size(); ++i)
        for (size_t j = i + 1; j < segs.size(); ++j)
            if (segs[i].base < segs[j].base + segs[j].limit && segs[j].base < segs[i].base + segs[i].limit)
                throw std::invalid_argument("segments " + std::to_string(segs[i].id) + " and " + std::to_string(segs[j].id) + " overlap");
}

XlateResult translateSegment(const std::vector<SegmentDesc>& segs, uint64_t phys, int seg, uint64_t offset, char access) {
    validateSegments(segs, phys);
    XlateResult r;
    r.page = (uint64_t)std::max(seg, 0);
    r.offset = offset;
    auto step = [&](const std::string& t, const std::string& d) { r.steps.push_back({t, d}); };
    step("1. Logical address", "(segment " + std::to_string(seg) + ", offset " + std::to_string(offset) + ")");
    const SegmentDesc* s = nullptr;
    for (const SegmentDesc& x : segs) if (x.id == seg) s = &x;
    if (!s) {
        r.outcome = XlateOutcome::SegmentMissing;
        step("2. Segment table lookup", "segment " + std::to_string(seg) + " does not exist -> trap");
        r.message = "segment " + std::to_string(seg) + " does not exist";
        return r;
    }
    step("2. Segment table lookup", "segment " + std::to_string(seg) + " (" + s->name + "): base " + std::to_string(s->base) +
                                        ", limit " + std::to_string(s->limit));
    if (offset >= s->limit) {
        r.outcome = XlateOutcome::Protection;
        step("3. Limit check", "offset " + std::to_string(offset) + " >= limit " + std::to_string(s->limit) +
                                   " -> trap: addressing error (protection fault)");
        r.message = "protection fault: offset " + std::to_string(offset) + " is outside segment " + std::to_string(seg) +
                    " (limit " + std::to_string(s->limit) + ")";
        return r;
    }
    step("3. Limit check", "0 <= " + std::to_string(offset) + " < " + std::to_string(s->limit) + " -> OK");
    bool allowed = access == 'w' ? s->write : access == 'x' ? s->exec : s->read;
    if (!allowed) {
        r.outcome = XlateOutcome::Protection;
        step("4. Permission check", std::string("access '") + access + "' is not allowed on segment " + std::to_string(seg) + " -> trap");
        r.message = std::string("protection violation: '") + access + "' access to segment " + std::to_string(seg);
        return r;
    }
    step("4. Permission check", std::string("access '") + access + "' allowed");
    r.physical = s->base + offset;
    r.frame = s->base;
    step("5. Physical address", "base + offset = " + std::to_string(s->base) + " + " + std::to_string(offset) + " = " + std::to_string(r.physical));
    r.message = "(" + std::to_string(seg) + ", " + std::to_string(offset) + ") -> physical " + std::to_string(r.physical);
    return r;
}

std::vector<SegmentDesc> segmentPreset() {
    return {{0, "code", 1400, 1000, true, false, true},
            {1, "data", 6300, 400, true, true, false},
            {2, "stack", 4000, 600, true, true, false},
            {3, "heap", 3200, 500, true, true, false},
            {4, "shared lib", 4700, 1100, true, false, true}};
}

// ================================================================ 4. demand paging, swapping

std::vector<DpRef> parseDpRefs(const std::string& text) {
    std::string s = text;
    for (char& c : s) if (c == ',' || c == ';' || c == '\n' || c == '\t') c = ' ';
    std::stringstream ss(s);
    std::string tok;
    std::vector<DpRef> v;
    while (ss >> tok) {
        DpRef r;
        if (!tok.empty() && (tok.back() == 'w' || tok.back() == 'W')) { r.write = true; tok.pop_back(); }
        if (!tok.empty() && (tok.back() == 'r' || tok.back() == 'R')) tok.pop_back();
        try { size_t u; r.page = std::stoi(tok, &u); if (u != tok.size()) throw 1; }
        catch (...) { throw std::invalid_argument("bad reference '" + tok + "' (use numbers, add w for a write, e.g. 3w)"); }
        if (r.page < 0 || r.page > 999) throw std::invalid_argument("page numbers must be between 0 and 999");
        v.push_back(r);
    }
    if (v.size() > 500) throw std::invalid_argument("at most 500 references");
    return v;
}

// Uses FramePool (core/memory.h) for the replacement decision, so FIFO / LRU / Optimal are the
// SAME algorithms as in the page-replacement tab; this adds dirty bits and the fault steps.
DpResult runDemandPaging(PageAlgo algo, const std::vector<DpRef>& refs, int frames) {
    if (frames < 1 || frames > 16) throw std::invalid_argument("frames must be between 1 and 16");
    FramePool pool(algo, frames);
    DpResult r;
    r.algo = algo;
    r.frames = frames;
    std::map<int, bool> dirty;                     // resident page -> modified?
    for (size_t i = 0; i < refs.size(); ++i) {
        auto nextUse = [&](long key) -> long {
            for (size_t j = i + 1; j < refs.size(); ++j) if (refs[j].page == key) return (long)j;
            return LONG_MAX;
        };
        std::vector<long> before = pool.frames();
        FramePool::Access a = pool.access(refs[i].page, nextUse);
        DpStep s;
        s.ref = refs[i];
        s.hit = !a.fault;
        s.frame = a.frame;
        std::string pg = "page " + std::to_string(refs[i].page);
        s.actions.push_back("check the page table: " + pg + (a.fault ? " is not present -> page fault (trap to the OS)" : " is present -> hit"));
        if (a.fault) {
            ++r.faults;
            if (a.evicted < 0) {
                s.actions.push_back("free frame " + std::to_string(a.frame) + " available");
            } else {
                s.victim = (int)a.evicted;
                s.victimDirty = dirty[s.victim];
                s.actions.push_back(std::string(pageAlgoName(algo)) + " selects victim page " + std::to_string(s.victim) + " in frame " +
                                    std::to_string(a.frame) + (algo == PageAlgo::OPTIMAL ? " (uses future references: offline baseline)" : ""));
                if (s.victimDirty) { ++r.writeBacks; s.actions.push_back("victim is dirty -> write it back to the backing store"); }
                else s.actions.push_back("victim is clean -> no write-back needed");
                dirty.erase(s.victim);
            }
            s.actions.push_back("load " + pg + " from the backing store into frame " + std::to_string(a.frame));
            s.actions.push_back("update the page table: " + pg + " -> frame " + std::to_string(a.frame) + ", present = 1");
            s.actions.push_back("restart the instruction: the reference now hits");
            dirty[refs[i].page] = false;
        } else {
            ++r.hits;
        }
        if (refs[i].write) { dirty[refs[i].page] = true; s.actions.push_back("write access -> dirty bit of " + pg + " set"); }
        for (long k : pool.frames()) {
            s.frames.push_back((int)k);
            s.dirty.push_back(k >= 0 && dirty.count((int)k) && dirty[(int)k]);
        }
        (void)before;
        r.steps.push_back(s);
    }
    return r;
}

SwapResult runSwapping(int capacity, const std::vector<SwapProc>& procs, const std::vector<std::string>& runOrder) {
    if (capacity <= 0 || capacity > 1 << 20) throw std::invalid_argument("memory capacity must be positive");
    std::map<std::string, int> size;
    for (const SwapProc& p : procs) {
        if (p.size <= 0 || p.size > capacity) throw std::invalid_argument("process " + p.name + " must fit in memory (size 1.." + std::to_string(capacity) + ")");
        if (!size.emplace(p.name, p.size).second) throw std::invalid_argument("duplicate process " + p.name);
    }
    SwapResult r;
    r.capacity = capacity;
    r.procs = procs;
    std::deque<std::string> resident;              // load order (front = loaded longest ago)
    std::set<std::string> backing;
    for (const SwapProc& p : procs) backing.insert(p.name);
    auto used = [&] { int u = 0; for (const std::string& n : resident) u += size[n]; return u; };
    for (const std::string& name : runOrder) {
        if (!size.count(name)) throw std::invalid_argument("unknown process " + name);
        SwapStep s;
        s.run = name;
        if (std::find(resident.begin(), resident.end(), name) != resident.end()) {
            s.text = name + " is already in memory: it runs without any swapping";
        } else {
            while (capacity - used() < size[name]) {
                std::string out = resident.front();
                resident.pop_front();
                backing.insert(out);
                s.swappedOut.push_back(out);
                ++r.swapOuts;
            }
            resident.push_back(name);
            backing.erase(name);
            s.swappedIn.push_back(name);
            ++r.swapIns;
            s.text = (s.swappedOut.empty() ? std::string("enough free memory") : "swap out the whole process(es) loaded longest ago") +
                     ", then swap in " + name + " (" + std::to_string(size[name]) + " units) as a whole";
        }
        s.resident.assign(resident.begin(), resident.end());
        s.backing.assign(backing.begin(), backing.end());
        s.freeAfter = capacity - used();
        r.steps.push_back(s);
    }
    return r;
}

// ================================================================ 5. thrashing

ThrashResult runThrashing(const std::vector<int>& refs, int frames, PageAlgo algo, int window, int wsWindow) {
    if (window < 1 || wsWindow < 1) throw std::invalid_argument("windows must be >= 1");
    MemResult m = runPageReplacement(algo, refs, frames);   // the existing algorithm
    ThrashResult r;
    r.frames = frames;
    r.faults = m.faults;
    r.hits = m.hits;
    r.window = window;
    r.wsWindow = wsWindow;
    r.refs = refs;
    for (size_t i = 0; i < m.steps.size(); ++i) {
        ThrashPoint p;
        p.fault = m.steps[i].fault;
        int f = 0, n = 0;
        for (int j = (int)i; j >= 0 && n < window; --j, ++n) f += m.steps[j].fault;
        p.rollingRate = n ? double(f) / n : 0;
        for (int v : m.steps[i].frames) p.occupied += v >= 0;
        std::set<int> ws;
        for (int j = (int)i; j >= 0 && (int)i - j < wsWindow; --j) ws.insert(refs[j]);
        p.workingSet = (int)ws.size();
        r.points.push_back(p);
    }
    return r;
}

std::vector<int> cyclicRefs(int pages, int repeats) {
    if (pages < 1 || pages > 64 || repeats < 1 || repeats > 100) throw std::invalid_argument("pages 1..64, repeats 1..100");
    std::vector<int> v;
    for (int r = 0; r < repeats; ++r) for (int p = 0; p < pages; ++p) v.push_back(p);
    return v;
}

std::vector<int> localityRefs(int length, int pages, int locality, unsigned seed) {
    if (length < 1 || length > 2000 || pages < 1 || pages > 64 || locality < 0 || locality > 100)
        throw std::invalid_argument("length 1..2000, pages 1..64, locality 0..100");
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99), any(0, pages - 1), step(-1, 1);
    std::vector<int> v;
    int cur = any(rng);
    for (int i = 0; i < length; ++i) {
        if (i > 0) cur = pct(rng) < locality ? std::clamp(cur + step(rng), 0, pages - 1) : any(rng);
        v.push_back(cur);
    }
    return v;
}

std::vector<MultiprogPoint> multiprogrammingCurve(int F, int W, int maxDegree, int refsPerProc, int faultTime) {
    if (F < 1 || W < 1 || maxDegree < 1 || maxDegree > 32 || refsPerProc < 1 || faultTime < 0)
        throw std::invalid_argument("invalid multiprogramming parameters");
    std::vector<MultiprogPoint> v;
    for (int d = 1; d <= maxDegree; ++d) {
        MultiprogPoint p;
        p.degree = d;
        p.framesPerProc = F / d;
        long longest = 0;
        for (int k = 0; k < d; ++k) {
            std::vector<int> refs;
            for (int i = 0; i < refsPerProc; ++i) refs.push_back(i % W);
            int faults = p.framesPerProc < 1 ? refsPerProc : runPageReplacement(PageAlgo::LRU, refs, p.framesPerProc).faults;
            p.faults += faults;
            p.refs += refsPerProc;
            longest = std::max(longest, (long)refsPerProc + (long)faults * faultTime);
        }
        p.cpuTicks = p.refs;
        p.diskTicks = (long)p.faults * faultTime;
        long makespan = std::max({p.cpuTicks, p.diskTicks, longest});
        p.utilisation = makespan > 0 ? double(p.cpuTicks) / makespan : 0;
        v.push_back(p);
    }
    return v;
}
