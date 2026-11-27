// heap.cpp - educational heap allocator over a bounded arena (see heap.h).
#include "heap.h"
#include <algorithm>
#include <climits>
#include <set>
#include <sstream>
#include <stdexcept>

static const uint32_t kMagicAlloc = 0xA110CA7Eu;
static const uint32_t kMagicFree = 0xF4EEB10Cu;
static const uint32_t kNone = 0xFFFFFFFFu;

const char* heapFamilyName(HeapFamily f) { return f == HeapFamily::FreeList ? "Free list (first/best/worst fit)" : "Buddy allocator"; }

static bool pow2(int x) { return x > 0 && (x & (x - 1)) == 0; }
static int log2i(int x) { int k = 0; while ((1 << k) < x) ++k; return k; }

void validateHeapConfig(const HeapConfig& c) {
    if (c.alignment != 8 && c.alignment != 16 && c.alignment != 32) throw std::invalid_argument("alignment must be 8, 16 or 32 bytes");
    if (c.capacity < 128 || c.capacity > 8192) throw std::invalid_argument("arena capacity must be 128..8192 bytes");
    if (c.capacity % c.alignment) throw std::invalid_argument("arena capacity must be a multiple of the alignment");
    if (c.family == HeapFamily::Buddy && !pow2(c.capacity)) throw std::invalid_argument("the buddy allocator needs a power-of-two arena (128, 256, ... 8192)");
}

Heap::Heap(const HeapConfig& cfg) : cfg_(cfg) {
    validateHeapConfig(cfg);
    header_ = roundUp(16);
    arena_.assign(cfg.capacity, 0);
    if (cfg.family == HeapFamily::FreeList) {
        makeBlock(0, cfg.capacity, true, 0, kNone);
        freeHead_ = 0;
    } else {
        minOrder_ = log2i(std::max(32, header_ * 2));
        maxOrder_ = log2i(cfg.capacity);
        buddyFree_.assign(maxOrder_ + 1, {});
        makeBlock(0, cfg.capacity, true, 0, kNone);
        pushBuddy(0, maxOrder_);
    }
}

int Heap::minBlock() const { return cfg_.family == HeapFamily::Buddy ? (1 << minOrder_) : header_ + cfg_.alignment; }

uint32_t Heap::get(int off, int field) const {
    int p = off + field * 4;
    if (p < 0 || p + 4 > (int)arena_.size()) return 0;
    return (uint32_t)arena_[p] | ((uint32_t)arena_[p + 1] << 8) | ((uint32_t)arena_[p + 2] << 16) | ((uint32_t)arena_[p + 3] << 24);
}
void Heap::put(int off, int field, uint32_t v) {
    int p = off + field * 4;
    if (p < 0 || p + 4 > (int)arena_.size()) return;          // checked: never outside the arena
    for (int i = 0; i < 4; ++i) arena_[p + i] = (uint8_t)(v >> (8 * i));
}
void Heap::makeBlock(int off, int size, bool isFree, int requested, uint32_t link) {
    put(off, 0, (uint32_t)size);
    put(off, 1, isFree ? 0u : (uint32_t)requested);
    put(off, 2, isFree ? kMagicFree : kMagicAlloc);
    put(off, 3, link);
}

HeapResult Heap::fail(const std::string& why) {
    ++failures_;
    HeapResult r;
    r.message = why;
    return r;
}
HeapResult Heap::guard() const {
    HeapResult r;
    if (corrupted_) { r.message = "the heap is corrupted (see the integrity check): reset it before further operations"; return r; }
    r.ok = true;
    return r;
}

// ------------------------------------------------------------------ free-list family

int Heap::pick(int need) const {
    int best = -1, bestSize = 0;
    for (int off = freeHead_, guardN = 0; off >= 0 && guardN < 10000; ++guardN) {
        int sz = (int)get(off, 0);
        if (sz >= need) {
            if (cfg_.fit == FitAlgo::First) return off;
            bool better = best < 0 || (cfg_.fit == FitAlgo::Best ? sz < bestSize : sz > bestSize);
            if (better) { best = off; bestSize = sz; }
        }
        uint32_t nx = get(off, 3);
        off = nx == kNone ? -1 : (int)nx;
    }
    return best;
}

void Heap::unlinkFree(int off) {
    int prev = -1;
    for (int cur = freeHead_, g = 0; cur >= 0 && g < 10000; ++g) {
        uint32_t nx = get(cur, 3);
        if (cur == off) {
            if (prev < 0) freeHead_ = nx == kNone ? -1 : (int)nx;
            else put(prev, 3, nx);
            return;
        }
        prev = cur;
        cur = nx == kNone ? -1 : (int)nx;
    }
}

// Inserts a free block in address order and coalesces it with adjacent free neighbours.
void Heap::insertFree(int off, std::vector<std::string>& ev) {
    int prev = -1, cur = freeHead_;
    while (cur >= 0 && cur < off) { prev = cur; uint32_t nx = get(cur, 3); cur = nx == kNone ? -1 : (int)nx; }
    put(off, 3, cur < 0 ? kNone : (uint32_t)cur);
    if (prev < 0) freeHead_ = off; else put(prev, 3, (uint32_t)off);
    int size = (int)get(off, 0);
    if (cur >= 0 && off + size == cur) {
        int cs = (int)get(cur, 0);
        ev.push_back("coalesce: free block @" + std::to_string(off) + " (" + std::to_string(size) + " B) + next free block @" +
                     std::to_string(cur) + " (" + std::to_string(cs) + " B) = " + std::to_string(size + cs) + " B");
        size += cs;
        put(off, 0, (uint32_t)size);
        put(off, 3, get(cur, 3));
    }
    if (prev >= 0) {
        int ps = (int)get(prev, 0);
        if (prev + ps == off) {
            ev.push_back("coalesce: previous free block @" + std::to_string(prev) + " (" + std::to_string(ps) + " B) + block @" +
                         std::to_string(off) + " (" + std::to_string(size) + " B) = " + std::to_string(ps + size) + " B");
            put(prev, 0, (uint32_t)(ps + size));
            put(prev, 3, get(off, 3));
        }
    }
}

HeapResult Heap::allocFreeList(int n, std::vector<std::string>& ev) {
    int need = header_ + roundUp(std::max(n, 1));
    int off = need <= cfg_.capacity ? pick(need) : -1;
    if (off < 0) {
        HeapMetrics m = metrics();
        return fail("no free block of " + std::to_string(need) + " B (" + std::to_string(header_) + " B header + " + std::to_string(need - header_) +
                    " B aligned payload): " + std::to_string(m.freeBytes) + " B free in total, largest free block " + std::to_string(m.largestFree) + " B");
    }
    unlinkFree(off);
    int size = (int)get(off, 0);
    std::string how = std::string(fitAlgoName(cfg_.fit)) + " chose the free block @" + std::to_string(off) + " (" + std::to_string(size) + " B)";
    if (size - need >= minBlock()) {
        makeBlock(off + need, size - need, true, 0, kNone);
        insertFree(off + need, ev);
        ev.push_back(how + "; split: " + std::to_string(need) + " B used, remainder @" + std::to_string(off + need) + " (" +
                     std::to_string(size - need) + " B) stays free");
        size = need;
    } else {
        ev.push_back(how + "; remainder " + std::to_string(size - need) + " B < minimum split size " + std::to_string(minBlock()) +
                     " B: the whole block is used (extra bytes = padding)");
    }
    makeBlock(off, size, false, n, 0);
    HeapResult r;
    r.ok = true;
    r.offset = off + header_;
    return r;
}

// ------------------------------------------------------------------ buddy family

int Heap::orderFor(int need) const {
    int k = minOrder_;
    while ((1 << k) < need) ++k;
    return k;
}

void Heap::pushBuddy(int off, int order) {
    auto& l = buddyFree_[order];
    l.insert(std::upper_bound(l.begin(), l.end(), off), off);
    for (size_t i = 0; i < l.size(); ++i) put(l[i], 3, i + 1 < l.size() ? (uint32_t)l[i + 1] : kNone);
}

bool Heap::popBuddy(int off, int order) {
    auto& l = buddyFree_[order];
    auto it = std::find(l.begin(), l.end(), off);
    if (it == l.end()) return false;
    l.erase(it);
    for (size_t i = 0; i < l.size(); ++i) put(l[i], 3, i + 1 < l.size() ? (uint32_t)l[i + 1] : kNone);
    return true;
}

HeapResult Heap::allocBuddy(int n, std::vector<std::string>& ev) {
    int need = header_ + std::max(n, 1);
    if (need > cfg_.capacity) return fail("request of " + std::to_string(n) + " B + header exceeds the arena");
    int k = orderFor(need);
    int j = k;
    while (j <= maxOrder_ && buddyFree_[j].empty()) ++j;
    if (j > maxOrder_) {
        HeapMetrics m = metrics();
        return fail("no free buddy block of order >= " + std::to_string(k) + " (" + std::to_string(1 << k) + " B): " + std::to_string(m.freeBytes) +
                    " B free in total, largest free block " + std::to_string(m.largestFree) + " B");
    }
    int off = buddyFree_[j].front();
    popBuddy(off, j);
    while (j > k) {
        --j;
        int buddy = off + (1 << j);
        makeBlock(buddy, 1 << j, true, 0, kNone);
        pushBuddy(buddy, j);
        ev.push_back("split: block @" + std::to_string(off) + " of " + std::to_string(1 << (j + 1)) + " B -> halves @" + std::to_string(off) +
                     " and @" + std::to_string(buddy) + " (buddies: " + std::to_string(off) + " XOR " + std::to_string(1 << j) + " = " + std::to_string(buddy) + ")");
    }
    makeBlock(off, 1 << k, false, n, 0);
    ev.push_back("allocated a " + std::to_string(1 << k) + " B block (order " + std::to_string(k) + ") for " + std::to_string(n) + " B + " +
                 std::to_string(header_) + " B header: internal fragmentation " + std::to_string((1 << k) - header_ - n) + " B");
    HeapResult r;
    r.ok = true;
    r.offset = off + header_;
    return r;
}

void Heap::freeBuddy(int off, std::vector<std::string>& ev) {
    int order = log2i((int)get(off, 0));
    while (order < maxOrder_) {
        int buddy = off ^ (1 << order);
        if (!popBuddy(buddy, order)) {
            ev.push_back("buddy @" + std::to_string(buddy) + " of block @" + std::to_string(off) + " (" + std::to_string(1 << order) + " B) is not free: no merge");
            break;
        }
        ev.push_back("merge: block @" + std::to_string(off) + " + free buddy @" + std::to_string(buddy) + " -> " + std::to_string(1 << (order + 1)) +
                     " B block @" + std::to_string(std::min(off, buddy)));
        off = std::min(off, buddy);
        ++order;
    }
    makeBlock(off, 1 << order, true, 0, kNone);
    pushBuddy(off, order);
}

// ------------------------------------------------------------------ public operations

HeapResult Heap::allocate(int n, std::vector<std::string>& ev) {
    HeapResult r = cfg_.family == HeapFamily::FreeList ? allocFreeList(n, ev) : allocBuddy(n, ev);
    if (!r.ok) return r;
    int h = nextHandle_++;
    handles_[h] = Alloc{r.offset, n, true};
    put(r.offset - header_, 3, (uint32_t)h);
    ++allocations_;
    r.handle = h;
    return r;
}

void Heap::release(int off, std::vector<std::string>& ev) {
    if (cfg_.family == HeapFamily::FreeList) {
        int size = (int)get(off, 0);
        makeBlock(off, size, true, 0, kNone);
        insertFree(off, ev);
    } else freeBuddy(off, ev);
}

HeapResult Heap::malloc(int n) {
    HeapResult g = guard();
    if (!g.ok) return g;
    if (n < 0) return fail("malloc: negative size");
    std::vector<std::string> ev;
    HeapResult r = allocate(n, ev);
    r.events = ev;
    if (r.ok) r.message = "malloc(" + std::to_string(n) + ") -> handle " + std::to_string(r.handle) + ", payload @" + std::to_string(r.offset) +
                          (n == 0 ? " (malloc(0): a unique minimum block in this model; it must still be freed)" : "");
    else r.message = "malloc(" + std::to_string(n) + ") failed: " + r.message;
    return r;
}

HeapResult Heap::calloc(long long count, long long size) {
    HeapResult g = guard();
    if (!g.ok) return g;
    if (count < 0 || size < 0) return fail("calloc: negative argument");
    if (count != 0 && size > INT_MAX / count)
        return fail("calloc(" + std::to_string(count) + ", " + std::to_string(size) + "): count * size overflows - refused before allocating");
    int n = (int)(count * size);
    std::vector<std::string> ev;
    HeapResult r = allocate(n, ev);
    if (!r.ok) { r.message = "calloc(" + std::to_string(count) + ", " + std::to_string(size) + ") failed: " + r.message; return r; }
    int blockSize = (int)get(r.offset - header_, 0);
    std::fill(arena_.begin() + r.offset, arena_.begin() + (r.offset - header_ + blockSize), 0);
    ev.push_back("payload zeroed (" + std::to_string(blockSize - header_) + " B) - old contents of the reused block are gone");
    r.events = ev;
    r.message = "calloc(" + std::to_string(count) + ", " + std::to_string(size) + ") = " + std::to_string(n) + " zeroed bytes -> handle " +
                std::to_string(r.handle) + ", payload @" + std::to_string(r.offset);
    return r;
}

HeapResult Heap::free(int handle) {
    HeapResult g = guard();
    if (!g.ok) return g;
    if (handle == 0) { HeapResult r; r.ok = true; r.message = "free(0): freeing the null handle does nothing"; return r; }
    auto it = handles_.find(handle);
    if (it == handles_.end()) return fail("free(" + std::to_string(handle) + "): invalid handle (never returned by this heap) - heap unchanged");
    if (!it->second.live) return fail("free(" + std::to_string(handle) + "): DOUBLE FREE detected - the block was already freed (heap unchanged)");
    std::vector<std::string> ev;
    int off = it->second.offset - header_;
    int size = (int)get(off, 0);
    release(off, ev);
    it->second.live = false;
    ++frees_;
    HeapResult r;
    r.ok = true;
    r.events = ev;
    r.message = "free(" + std::to_string(handle) + "): block @" + std::to_string(off) + " (" + std::to_string(size) + " B) returned";
    return r;
}

HeapResult Heap::realloc(int handle, int n) {
    HeapResult g = guard();
    if (!g.ok) return g;
    if (handle == 0) { HeapResult r = malloc(n); r.message = "realloc(0, " + std::to_string(n) + ") = " + r.message; return r; }
    auto it = handles_.find(handle);
    if (it == handles_.end()) return fail("realloc: invalid handle " + std::to_string(handle));
    if (!it->second.live) return fail("realloc: handle " + std::to_string(handle) + " was already freed (use after free refused)");
    if (n < 0) return fail("realloc: negative size");
    if (n == 0) {
        HeapResult r = free(handle);
        r.handle = 0;
        r.message = "realloc(" + std::to_string(handle) + ", 0): freed, returns the null handle (this model's convention)";
        return r;
    }
    std::vector<std::string> ev;
    Alloc& a = it->second;
    int off = a.offset - header_;
    int size = (int)get(off, 0);
    HeapResult r;
    r.ok = true;
    r.handle = handle;
    bool inPlace = false;
    if (cfg_.family == HeapFamily::FreeList) {
        int need = header_ + roundUp(n);
        if (need <= size) {
            if (size - need >= minBlock()) {
                makeBlock(off + need, size - need, true, 0, kNone);
                insertFree(off + need, ev);
                ev.push_back("shrink in place: tail @" + std::to_string(off + need) + " (" + std::to_string(size - need) + " B) freed");
                size = need;
            } else ev.push_back("shrink in place (no split: the remainder would be below the minimum block)");
            inPlace = true;
        } else {
            int nxt = off + size;
            if (nxt < cfg_.capacity && get(nxt, 2) == kMagicFree && size + (int)get(nxt, 0) >= need) {
                int total = size + (int)get(nxt, 0);
                unlinkFree(nxt);
                if (total - need >= minBlock()) {
                    makeBlock(off + need, total - need, true, 0, kNone);
                    insertFree(off + need, ev);
                    size = need;
                } else size = total;
                ev.push_back("grow in place: absorbed the adjacent free block @" + std::to_string(nxt));
                inPlace = true;
            }
        }
        if (inPlace) { makeBlock(off, size, false, n, (uint32_t)handle); a.requested = n; }
    } else {
        int k = orderFor(header_ + n), cur = log2i(size);
        if (k <= cur) {
            while (cur > k) {
                --cur;
                int buddy = off + (1 << cur);
                makeBlock(buddy, 1 << cur, true, 0, kNone);
                pushBuddy(buddy, cur);
                ev.push_back("shrink in place: upper half @" + std::to_string(buddy) + " (" + std::to_string(1 << cur) + " B) freed");
            }
            makeBlock(off, 1 << cur, false, n, (uint32_t)handle);
            a.requested = n;
            inPlace = true;
        }
    }
    if (inPlace) {
        r.offset = a.offset;
        r.events = ev;
        r.message = "realloc(" + std::to_string(handle) + ", " + std::to_string(n) + "): resized in place, payload stays @" + std::to_string(a.offset);
        return r;
    }
    // move: allocate first (the old block is still in use, so it cannot be chosen), copy, free old
    HeapResult nr = cfg_.family == HeapFamily::FreeList ? allocFreeList(n, ev) : allocBuddy(n, ev);
    if (!nr.ok) {
        nr.message = "realloc(" + std::to_string(handle) + ", " + std::to_string(n) + ") failed: " + nr.message + " - the OLD allocation is unchanged";
        return nr;
    }
    int keep = std::min(a.requested, n);
    std::copy(arena_.begin() + a.offset, arena_.begin() + a.offset + keep, arena_.begin() + nr.offset);
    put(nr.offset - header_, 3, (uint32_t)handle);
    int oldOff = a.offset;
    release(off, ev);
    ev.push_back("moved: copied " + std::to_string(keep) + " B from @" + std::to_string(oldOff) + " to @" + std::to_string(nr.offset) + ", old block freed");
    a.offset = nr.offset;
    a.requested = n;
    r.offset = nr.offset;
    r.events = ev;
    r.message = "realloc(" + std::to_string(handle) + ", " + std::to_string(n) + "): moved @" + std::to_string(oldOff) + " -> @" + std::to_string(nr.offset) +
                " (" + std::to_string(keep) + " bytes preserved)";
    return r;
}

HeapResult Heap::write(int handle, int offset, const std::string& bytes) {
    HeapResult g = guard();
    if (!g.ok) return g;
    auto it = handles_.find(handle);
    if (it == handles_.end() || !it->second.live) return fail("write: handle " + std::to_string(handle) + " is not a live allocation");
    const Alloc& a = it->second;
    if (offset < 0 || offset + (long)bytes.size() > a.requested)
        return fail("write: out-of-bounds request rejected - bytes [" + std::to_string(offset) + ", " + std::to_string(offset + (long)bytes.size()) +
                    ") are outside the " + std::to_string(a.requested) + " requested bytes (nothing written)");
    std::copy(bytes.begin(), bytes.end(), arena_.begin() + a.offset + offset);
    HeapResult r;
    r.ok = true;
    r.handle = handle;
    r.offset = a.offset + offset;
    r.message = "wrote " + std::to_string(bytes.size()) + " B at payload offset " + std::to_string(offset) + " (arena @" + std::to_string(a.offset + offset) + ")";
    return r;
}

HeapResult Heap::read(int handle, int offset, int n) const {
    HeapResult r;
    auto it = handles_.find(handle);
    if (it == handles_.end() || !it->second.live) { r.message = "read: handle " + std::to_string(handle) + " is not a live allocation"; return r; }
    const Alloc& a = it->second;
    if (n < 0 || offset < 0 || offset + n > a.requested) {
        r.message = "read: out-of-bounds request rejected - [" + std::to_string(offset) + ", " + std::to_string(offset + n) + ") is outside the " +
                    std::to_string(a.requested) + " requested bytes";
        return r;
    }
    r.ok = true;
    r.handle = handle;
    r.data.assign(arena_.begin() + a.offset + offset, arena_.begin() + a.offset + offset + n);
    r.message = "read " + std::to_string(n) + " B at payload offset " + std::to_string(offset);
    return r;
}

HeapResult Heap::overflow(int handle, int extra) {
    HeapResult g = guard();
    if (!g.ok) return g;
    auto it = handles_.find(handle);
    if (it == handles_.end() || !it->second.live) return fail("overflow demo: handle " + std::to_string(handle) + " is not live");
    if (extra < 1 || extra > 64) return fail("overflow demo: 1..64 extra bytes");
    const Alloc& a = it->second;
    int blockEnd = a.offset - header_ + (int)get(a.offset - header_, 0);
    int start = a.offset + a.requested, inPad = 0, beyond = 0;
    for (int i = 0; i < extra && start + i < cfg_.capacity; ++i) {
        arena_[start + i] = 0xEE;                       // inside the arena vector: a modelled overflow
        if (start + i < blockEnd) ++inPad; else ++beyond;
    }
    std::vector<std::string> issues = check();
    corrupted_ = !issues.empty();
    HeapResult r;
    r.ok = true;
    r.handle = handle;
    r.message = "overflow demo: " + std::to_string(extra) + " B written past the request (" + std::to_string(inPad) + " into this block's padding, " +
                std::to_string(beyond) + " beyond the block). " +
                (corrupted_ ? "Integrity check FAILED: " + issues.front() + " - the heap must be reset."
                            : std::string("Not detected by the header checks (it stayed in padding): such bugs are silent until they hit metadata."));
    return r;
}

// ------------------------------------------------------------------ inspection

std::vector<HeapBlock> Heap::blocks() const {
    std::vector<HeapBlock> v;
    for (int off = 0; off < cfg_.capacity;) {
        int size = (int)get(off, 0);
        if (size <= 0 || off + size > cfg_.capacity) break;
        HeapBlock b;
        b.offset = off;
        b.size = size;
        b.header = header_;
        b.free = get(off, 2) != kMagicAlloc;
        b.requested = b.free ? 0 : (int)get(off, 1);
        b.padding = b.free ? 0 : std::max(0, size - header_ - b.requested);
        uint32_t link = get(off, 3);
        if (b.free) b.nextFree = link == kNone ? -1 : (int)link;
        else b.handle = (int)link;
        b.order = log2i(size);
        v.push_back(b);
        off += size;
    }
    return v;
}

std::vector<int> Heap::freeList(int order) const {
    std::vector<int> v;
    if (cfg_.family == HeapFamily::Buddy) {
        if (order >= 0) return order <= maxOrder_ ? buddyFree_[order] : v;
        for (int k = 0; k <= maxOrder_; ++k) for (int off : buddyFree_[k]) v.push_back(off);
        std::sort(v.begin(), v.end());
        return v;
    }
    for (int off = freeHead_, g = 0; off >= 0 && g < 10000; ++g) {
        v.push_back(off);
        uint32_t nx = get(off, 3);
        off = nx == kNone ? -1 : (int)nx;
    }
    return v;
}

HeapMetrics Heap::metrics() const {
    HeapMetrics m;
    m.capacity = cfg_.capacity;
    for (const HeapBlock& b : blocks()) {
        m.headerBytes += b.header;
        if (b.free) { m.freeBytes += b.size; m.largestFree = std::max(m.largestFree, b.size); }
        else { m.liveRequested += b.requested; m.paddingBytes += b.padding; }
    }
    if (m.largestFree > 0) {
        int raw = m.largestFree - header_;
        m.largestRequest = cfg_.family == HeapFamily::FreeList ? raw / cfg_.alignment * cfg_.alignment : raw;
    }
    for (auto& kv : handles_) if (kv.second.live) ++m.liveAllocations;
    m.allocations = allocations_;
    m.frees = frees_;
    m.failures = failures_;
    m.fragDefined = m.freeBytes > 0;
    m.fragIndicator = m.fragDefined ? 1.0 - double(m.largestFree) / m.freeBytes : 0.0;
    return m;
}

std::vector<std::pair<int, int>> Heap::outstanding() const {
    std::vector<std::pair<int, int>> v;
    for (auto& kv : handles_) if (kv.second.live) v.push_back({kv.first, kv.second.requested});
    return v;
}

int Heap::handleOffset(int h) const { auto it = handles_.find(h); return it != handles_.end() && it->second.live ? it->second.offset : -1; }
int Heap::handleSize(int h) const { auto it = handles_.find(h); return it != handles_.end() && it->second.live ? it->second.requested : -1; }
bool Heap::handleLive(int h) const { return handleOffset(h) >= 0; }

// Structural checks: coverage without gaps or overlap, header magic, alignment, free-list /
// buddy-list agreement, coalescing invariant, handle table agreement, accounting identity.
std::vector<std::string> Heap::check() const {
    std::vector<std::string> bad;
    int covered = 0, prevFree = -1;
    std::set<int> freeSeen, allocSeen;
    for (int off = 0; off < cfg_.capacity;) {
        int size = (int)get(off, 0);
        uint32_t magic = get(off, 2);
        std::string at = "block @" + std::to_string(off);
        if (magic != kMagicAlloc && magic != kMagicFree) { bad.push_back(at + ": header magic corrupted"); break; }
        if (size < minBlock() || off + size > cfg_.capacity) { bad.push_back(at + ": impossible size " + std::to_string(size)); break; }
        if (cfg_.family == HeapFamily::FreeList && size % cfg_.alignment) bad.push_back(at + ": size not a multiple of the alignment");
        if (cfg_.family == HeapFamily::Buddy && (!pow2(size) || off % size)) bad.push_back(at + ": not a valid buddy block");
        if ((off + header_) % cfg_.alignment) bad.push_back(at + ": payload not aligned");
        bool fr = magic == kMagicFree;
        if (fr) {
            freeSeen.insert(off);
            if (cfg_.family == HeapFamily::FreeList && prevFree >= 0 && prevFree == off) bad.push_back(at + ": two adjacent free blocks (missed coalescing)");
            prevFree = off + size;
        } else {
            allocSeen.insert(off);
            prevFree = -1;
            if ((int)get(off, 1) > size - header_) bad.push_back(at + ": requested bytes exceed the block");
        }
        covered += size;
        off += size;
    }
    if (bad.empty() && covered != cfg_.capacity) bad.push_back("blocks cover " + std::to_string(covered) + " of " + std::to_string(cfg_.capacity) + " bytes");
    std::vector<int> fl = freeList();
    std::set<int> listed(fl.begin(), fl.end());
    if (listed.size() != fl.size()) bad.push_back("free list contains a block twice (cycle?)");
    if (bad.empty() && listed != freeSeen) bad.push_back("free list disagrees with the free blocks in the arena");
    if (cfg_.family == HeapFamily::FreeList)
        for (size_t i = 1; i < fl.size(); ++i) if (fl[i] <= fl[i - 1]) { bad.push_back("free list not in address order"); break; }
    if (cfg_.family == HeapFamily::Buddy && bad.empty())
        for (int k = 0; k <= maxOrder_; ++k)
            for (int off : buddyFree_[k]) {
                if ((int)get(off, 0) != (1 << k)) bad.push_back("buddy list order mismatch @" + std::to_string(off));
                if (k < maxOrder_ && std::count(buddyFree_[k].begin(), buddyFree_[k].end(), off ^ (1 << k))) bad.push_back("two free buddies not merged @" + std::to_string(off));
            }
    int live = 0;
    for (auto& kv : handles_) {
        if (!kv.second.live) continue;
        ++live;
        int off = kv.second.offset - header_;
        if (!allocSeen.count(off) || (int)get(off, 3) != kv.first || (int)get(off, 1) != kv.second.requested)
            bad.push_back("handle " + std::to_string(kv.first) + " does not match its block header");
    }
    if (bad.empty() && live != (int)allocSeen.size()) bad.push_back("allocated blocks without a live handle");
    if (bad.empty()) {
        HeapMetrics m = metrics();
        int freeHeaders = (int)freeSeen.size() * header_;
        if (m.headerBytes + m.liveRequested + m.paddingBytes + (m.freeBytes - freeHeaders) != cfg_.capacity) bad.push_back("accounting identity violated");
    }
    return bad;
}

// ------------------------------------------------------------------ sessions

std::vector<std::string> splitHeapScript(const std::string& script) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : script) {
        if (c == ';' || c == '\n') {
            size_t a = cur.find_first_not_of(" \t\r"), b = cur.find_last_not_of(" \t\r");
            if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
            cur.clear();
        } else cur += c;
    }
    size_t a = cur.find_first_not_of(" \t\r"), b = cur.find_last_not_of(" \t\r");
    if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
    return out;
}

static void snapshot(const Heap& h, HeapStep& s) {
    s.blocks = h.blocks();
    s.freeList = h.freeList();
    s.metrics = h.metrics();
    s.arena = h.arena();
    s.integrity = h.check();
}

HeapSession runHeapScript(const HeapConfig& cfg, const std::vector<std::string>& lines) {
    HeapSession ses;
    ses.cfg = cfg;
    Heap h(cfg);
    HeapStep s0;
    s0.op = "(empty heap)";
    s0.result.ok = true;
    s0.result.message = std::string(heapFamilyName(cfg.family)) + ", " + std::to_string(cfg.capacity) + " B arena, alignment " + std::to_string(cfg.alignment) +
                        ", header " + std::to_string(h.headerSize()) + " B";
    snapshot(h, s0);
    ses.steps.push_back(s0);
    std::map<std::string, int> names;
    for (const std::string& line : lines) {
        HeapStep st;
        st.op = line;
        std::string lhs, rhs = line;
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            lhs = line.substr(0, eq);
            rhs = line.substr(eq + 1);
            lhs.erase(0, lhs.find_first_not_of(' '));
            lhs.erase(lhs.find_last_not_of(' ') + 1);
        }
        std::istringstream in(rhs);
        std::string verb;
        in >> verb;
        auto handleOf = [&](const std::string& n, bool& known) {
            known = true;
            if (n == "0" || n == "null") return 0;
            auto it = names.find(n);
            if (it != names.end()) return it->second;
            known = false;
            return -1;
        };
        HeapResult r;
        bool known = true;
        long long x = 0, y = 0;
        std::string v;
        int before = -1;
        if (verb == "malloc" && in >> x && !lhs.empty()) r = h.malloc((int)std::min<long long>(x, INT_MAX));
        else if (verb == "calloc" && in >> x >> y && !lhs.empty()) r = h.calloc(x, y);
        else if (verb == "realloc" && in >> v >> x && !lhs.empty()) {
            int hd = handleOf(v, known);
            before = h.handleOffset(hd);
            if (known) r = h.realloc(hd, (int)std::min<long long>(x, INT_MAX)); else r.message = "unknown name '" + v + "'";
        } else if (verb == "free" && in >> v) {
            int hd = handleOf(v, known);
            if (known) r = h.free(hd); else r.message = "free(" + v + "): unknown name - invalid handle refused";
        } else if (verb == "write" && in >> v >> x) {
            std::string text;
            std::getline(in, text);
            if (!text.empty() && text[0] == ' ') text.erase(0, 1);
            int hd = handleOf(v, known);
            if (known) r = h.write(hd, (int)x, text); else r.message = "write: unknown name '" + v + "'";
        } else if (verb == "read" && in >> v >> x >> y) {
            int hd = handleOf(v, known);
            if (known) r = h.read(hd, (int)x, (int)y); else r.message = "read: unknown name '" + v + "'";
            if (r.ok) r.message += ": \"" + r.data + "\"";
        } else if (verb == "overflow" && in >> v >> x) {
            int hd = handleOf(v, known);
            if (known) r = h.overflow(hd, (int)x); else r.message = "overflow: unknown name '" + v + "'";
        } else r.message = "cannot parse '" + line + "' (examples: a = malloc 100; b = calloc 4 25; a = realloc a 200; free a; write a 0 text; read a 0 4; overflow a 8)";
        if (!lhs.empty() && (verb == "malloc" || verb == "calloc" || verb == "realloc")) {
            if (r.ok) names[lhs] = r.handle;
        }
        if (verb == "realloc" && r.ok && before >= 0 && r.offset >= 0 && r.offset != before) { st.movedFrom = before; st.movedTo = r.offset; }
        st.result = r;
        st.names = names;
        snapshot(h, st);
        ses.steps.push_back(st);
    }
    return ses;
}

std::vector<HeapPreset> heapPresets() {
    auto cfg = [](HeapFamily f, FitAlgo a, int cap, int al) { HeapConfig c; c.family = f; c.fit = a; c.capacity = cap; c.alignment = al; return c; };
    return {
        {"Basics: split, free, coalesce", "Allocations split the single free block; freeing neighbours merges them back into one block.",
         "a = malloc 100; b = malloc 200; c = malloc 50; free b; d = malloc 120; write d 0 hello heap; read d 0 10; free a; free c; free d",
         cfg(HeapFamily::FreeList, FitAlgo::First, 1024, 16)},
        {"First / Best / Worst Fit choice", "Three holes (304, 96, 208 B) and a 320 B tail: a 70-byte request (96 B block) goes to the "
         "first hole (First Fit), the exact 96 B hole (Best Fit) or the 320 B tail (Worst Fit). Switch the fit policy and reload.",
         "h1 = malloc 280; k1 = malloc 16; h2 = malloc 80; k2 = malloc 16; h3 = malloc 180; k3 = malloc 16; free h1; free h2; free h3; x = malloc 70",
         cfg(HeapFamily::FreeList, FitAlgo::Best, 1024, 16)},
        {"Fragmentation: enough free space, no block", "768 B are free in total but the largest free block is 320 B: a 400-byte request "
         "(416 B block) fails - external fragmentation.",
         "a = malloc 200; b = malloc 100; c = malloc 200; d = malloc 100; e = malloc 200; free a; free c; free e; big = malloc 400",
         cfg(HeapFamily::FreeList, FitAlgo::First, 1024, 16)},
        {"realloc: in place, move, failure", "Shrink in place, grow by absorbing the free neighbour, grow by moving (data preserved) and a "
         "failing realloc that leaves the old allocation intact.",
         "a = malloc 64; b = malloc 64; write a 0 important; a = realloc a 32; a = realloc a 48; a = realloc a 300; read a 0 9; "
         "c = realloc b 5000; read b 0 1; free a; free b",
         cfg(HeapFamily::FreeList, FitAlgo::First, 1024, 16)},
        {"calloc zeroing and overflow check", "calloc reuses the freed block and zeroes it (no old secret visible); a count*size overflow is refused.",
         "a = malloc 64; write a 0 SECRET-DATA; free a; z = calloc 4 16; read z 0 11; bad = calloc 1000000 1000000; free z",
         cfg(HeapFamily::FreeList, FitAlgo::First, 512, 16)},
        {"Errors: double free, invalid handle, bounds, leak", "Detected errors never change the heap. 'c' is never freed: it is reported as a "
         "potential leak (an outstanding allocation, not proof of unreachable memory).",
         "a = malloc 32; free a; free a; free b; c = malloc 16; write c 10 0123456789; write c 0 ok",
         cfg(HeapFamily::FreeList, FitAlgo::First, 512, 16)},
        {"Buddy allocator: split and merge", "Power-of-two blocks: each request splits larger blocks in halves; freed buddies (offset XOR size) merge.",
         "a = malloc 40; b = malloc 100; c = malloc 40; d = malloc 200; free a; free c; free b; free d",
         cfg(HeapFamily::Buddy, FitAlgo::First, 1024, 16)},
        {"Corruption inside the model", "Writing 40 bytes past a 20-byte request runs through the padding into the next header: the integrity "
         "check detects the damaged header (no real out-of-bounds access is performed).",
         "a = malloc 20; b = malloc 20; overflow a 8; overflow a 40; c = malloc 10",
         cfg(HeapFamily::FreeList, FitAlgo::First, 256, 16)},
    };
}
