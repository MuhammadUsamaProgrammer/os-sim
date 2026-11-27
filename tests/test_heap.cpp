// test_heap.cpp - Heap Laboratory allocator (core/heap.h): alignment, coverage, accounting,
// fit policies, splitting threshold, coalescing, buddy split/merge, calloc, realloc, errors.
#include <cstdio>
#include "check.h"
#include "heap.h"

static HeapConfig cfgOf(HeapFamily f, FitAlgo a, int cap = 1024, int al = 16) {
    HeapConfig c;
    c.family = f; c.fit = a; c.capacity = cap; c.alignment = al;
    return c;
}
static bool ok(const Heap& h) {
    std::vector<std::string> v = h.check();
    for (const std::string& s : v) std::printf("  heap: %s\n", s.c_str());
    return v.empty();
}

static void testFreeList() {
    for (int al : {8, 16, 32}) {
        Heap h(cfgOf(HeapFamily::FreeList, FitAlgo::First, 1024, al));
        HeapResult a = h.malloc(13), b = h.malloc(1), c = h.malloc(100);
        CHECK(a.ok && b.ok && c.ok);
        CHECK(a.offset % al == 0 && b.offset % al == 0 && c.offset % al == 0);     // aligned payloads
        CHECK(a.offset + 13 <= b.offset - h.headerSize());                           // no overlap
        CHECK(ok(h));
        HeapMetrics m = h.metrics();
        CHECK(m.liveRequested == 114 && m.liveAllocations == 3);
        int freeHeaders = 0;
        for (const HeapBlock& x : h.blocks()) if (x.free) freeHeaders += x.header;
        CHECK(m.headerBytes + m.liveRequested + m.paddingBytes + m.freeBytes - freeHeaders == 1024);   // accounting identity
    }
    // split threshold: a remainder smaller than the minimum block is NOT split off (becomes padding)
    Heap t(cfgOf(HeapFamily::FreeList, FitAlgo::First, 256, 16));
    CHECK(t.minBlock() == 32);
    HeapResult big = t.malloc(256 - 16 - 16);           // leaves 16 B < 32 B minimum: whole block used
    CHECK(big.ok && t.blocks().size() == 1 && t.blocks()[0].padding == 16 && t.metrics().freeBytes == 0);
    CHECK(!t.metrics().fragDefined);                    // no free space: the indicator is not defined
    CHECK(t.free(big.handle).ok && t.blocks().size() == 1 && t.blocks()[0].free);
    // coalescing: freeing three adjacent blocks in any order gives back one free block
    Heap c(cfgOf(HeapFamily::FreeList, FitAlgo::First, 1024, 16));
    int x = c.malloc(50).handle, y = c.malloc(50).handle, z = c.malloc(50).handle;
    c.malloc(50);
    CHECK(c.free(x).ok && c.free(z).ok && c.freeList().size() == 3);
    HeapResult fy = c.free(y);
    CHECK(fy.ok && c.freeList().size() == 2 && fy.events.size() == 2);   // merged with next and previous
    CHECK(ok(c));
}

static void testFits() {
    HeapPreset p = heapPresets()[1];
    std::vector<std::string> lines = splitHeapScript(p.script);
    int expected[3] = {16, 352, 720};          // First: hole h1 @0; Best: exact 96 B hole h2 @336; Worst: tail @704
    FitAlgo algos[3] = {FitAlgo::First, FitAlgo::Best, FitAlgo::Worst};
    for (int i = 0; i < 3; ++i) {
        HeapConfig c = p.cfg;
        c.fit = algos[i];
        HeapSession s = runHeapScript(c, lines);
        const HeapStep& last = s.steps.back();
        CHECK(last.result.ok && last.result.offset == expected[i]);
        CHECK(last.integrity.empty());
    }
    // fragmentation preset: enough total free space, no suitable block
    HeapPreset f = heapPresets()[2];
    HeapSession fs = runHeapScript(f.cfg, splitHeapScript(f.script));
    const HeapStep& fl = fs.steps.back();
    CHECK(!fl.result.ok && fl.metrics.freeBytes == 768 && fl.metrics.largestFree == 320);
    CHECK(fl.metrics.freeBytes > 400 + 16 && fl.metrics.fragDefined && fl.metrics.fragIndicator > 0.5);
    CHECK(fl.metrics.failures == 1);
}

static void testCallocRealloc() {
    Heap h(cfgOf(HeapFamily::FreeList, FitAlgo::First, 512, 16));
    HeapResult a = h.malloc(64);
    h.write(a.handle, 0, "SECRET-DATA");
    h.free(a.handle);
    HeapResult z = h.calloc(4, 16);
    CHECK(z.ok && z.offset == a.offset);                                    // the same block is reused...
    HeapResult rz = h.read(z.handle, 0, 11);
    CHECK(rz.ok && rz.data == std::string(11, '\0'));                       // ...but zeroed
    CHECK(!h.calloc(1000000, 1000000).ok);                                  // multiplication overflow refused
    CHECK(!h.calloc(-1, 4).ok);
    CHECK(ok(h));
    // realloc: shrink in place keeps the prefix, grow-in-place, move preserves data, failure keeps the old block
    Heap r(cfgOf(HeapFamily::FreeList, FitAlgo::First, 1024, 16));
    HeapResult p = r.malloc(64), q = r.malloc(64);
    r.write(p.handle, 0, "important");
    HeapResult s1 = r.realloc(p.handle, 32);
    CHECK(s1.ok && s1.offset == p.offset && r.read(p.handle, 0, 9).data == "important");
    HeapResult s2 = r.realloc(p.handle, 48);
    CHECK(s2.ok && s2.offset == p.offset);                                  // absorbed the freed tail
    HeapResult s3 = r.realloc(p.handle, 300);
    CHECK(s3.ok && s3.offset != p.offset && r.read(p.handle, 0, 9).data == "important");
    r.write(q.handle, 0, "Q");
    HeapResult s4 = r.realloc(q.handle, 5000);
    CHECK(!s4.ok && r.handleLive(q.handle) && r.read(q.handle, 0, 1).data == "Q");
    HeapResult s5 = r.realloc(q.handle, 0);
    CHECK(s5.ok && s5.handle == 0 && !r.handleLive(q.handle));              // realloc(h, 0) frees
    HeapResult s6 = r.realloc(0, 10);
    CHECK(s6.ok && s6.handle > 0);                                          // realloc(0, n) = malloc
    CHECK(ok(r));
}

static void testErrors() {
    Heap h(cfgOf(HeapFamily::FreeList, FitAlgo::First, 512, 16));
    HeapResult a = h.malloc(32);
    CHECK(h.free(a.handle).ok);
    std::vector<HeapBlock> before = h.blocks();
    CHECK(!h.free(a.handle).ok);                                            // double free
    CHECK(!h.free(9999).ok);                                                // invalid handle
    CHECK(h.free(0).ok);                                                    // null free = no-op
    CHECK(h.blocks().size() == before.size());
    HeapResult c = h.malloc(16);
    CHECK(!h.write(c.handle, 10, "0123456789").ok && h.write(c.handle, 0, "ok").ok);   // bounds
    CHECK(!h.read(c.handle, 15, 2).ok);
    CHECK(!h.write(a.handle, 0, "x").ok);                                   // use after free refused
    CHECK(h.outstanding().size() == 1 && h.outstanding()[0].first == c.handle && h.outstanding()[0].second == 16);
    CHECK(h.metrics().failures >= 4);
    CHECK(h.malloc(0).ok);                                                  // documented: unique minimum block
    CHECK(!h.malloc(-5).ok && !h.malloc(100000).ok);
    CHECK(ok(h));
    // controlled corruption inside the model
    Heap k(cfgOf(HeapFamily::FreeList, FitAlgo::First, 256, 16));
    HeapResult x = k.malloc(20);
    k.malloc(20);
    HeapResult o1 = k.overflow(x.handle, 8);                                // stays in the padding
    CHECK(o1.ok && !k.corrupted() && k.check().empty());
    HeapResult o2 = k.overflow(x.handle, 40);                               // reaches the next header
    CHECK(o2.ok && k.corrupted() && !k.check().empty());
    CHECK(!k.malloc(10).ok);                                                // refused until reset
    CHECK_THROWS(Heap(cfgOf(HeapFamily::Buddy, FitAlgo::First, 1000, 16)), std::invalid_argument);
    CHECK_THROWS(Heap(cfgOf(HeapFamily::FreeList, FitAlgo::First, 1024, 12)), std::invalid_argument);
}

static void testBuddy() {
    Heap b(cfgOf(HeapFamily::Buddy, FitAlgo::First, 1024, 16));
    CHECK(b.blocks().size() == 1 && b.minBlock() == 32);
    HeapResult a = b.malloc(40);                                            // 56 B -> 64 B block
    CHECK(a.ok && a.offset == 16 && b.blocks()[0].size == 64);
    CHECK(a.events.size() == 5);                                            // 1024->512->256->128->64 splits + summary
    // free lists: one buddy at every order 6..9
    for (int k = 6; k <= 9; ++k) CHECK(b.freeList(k).size() == 1 && b.freeList(k)[0] == (1 << k));
    HeapResult c = b.malloc(40);
    CHECK(c.ok && c.offset == 64 + 16);                                     // the buddy of a
    HeapResult d = b.malloc(200);                                           // 216 -> 256
    CHECK(d.ok && d.offset == 256 + 16);
    CHECK(ok(b));
    for (const HeapBlock& x : b.blocks()) CHECK(x.offset % x.size == 0);    // buddy alignment
    CHECK(b.free(a.handle).ok && b.blocks().size() == 5);                   // buddy c still allocated: no merge
    CHECK(b.free(c.handle).ok);
    CHECK(b.free(d.handle).ok);
    CHECK(b.blocks().size() == 1 && b.blocks()[0].free && b.blocks()[0].size == 1024);   // merged back completely
    CHECK(ok(b));
    // internal fragmentation is padding, not usable payload
    HeapResult e = b.malloc(100);                                           // 116 -> 128 B block
    CHECK(e.ok && b.blocks()[0].padding == 128 - 16 - 100);
    // buddy realloc: shrink frees upper halves, grow moves
    HeapResult g = b.realloc(e.handle, 10);
    CHECK(g.ok && g.offset == e.offset && b.blocks()[0].size == 32);
    HeapResult m = b.realloc(e.handle, 300);
    CHECK(m.ok && ok(b) && b.handleSize(e.handle) == 300);
}

static void testSessions() {
    for (const HeapPreset& p : heapPresets()) {
        HeapSession s = runHeapScript(p.cfg, splitHeapScript(p.script));
        CHECK(s.steps.size() == splitHeapScript(p.script).size() + 1);
        HeapSession s2 = runHeapScript(p.cfg, splitHeapScript(p.script));     // deterministic replay
        CHECK(s2.steps.back().arena == s.steps.back().arena);
    }
    HeapPreset c = heapPresets()[7];
    HeapSession cs = runHeapScript(c.cfg, splitHeapScript(c.script));
    CHECK(cs.steps[3].integrity.empty() && !cs.steps[4].integrity.empty() && !cs.steps[5].result.ok);
    HeapPreset r = heapPresets()[3];
    HeapSession rs = runHeapScript(r.cfg, splitHeapScript(r.script));
    bool moved = false;
    for (const HeapStep& s : rs.steps) if (s.movedFrom >= 0 && s.movedTo >= 0) moved = true;
    CHECK(moved);
    HeapSession bad = runHeapScript(r.cfg, {"x = malloc", "frob a", "free nothing"});
    CHECK(bad.steps.size() == 4 && !bad.steps[1].result.ok && !bad.steps[2].result.ok && !bad.steps[3].result.ok);
    CHECK(splitHeapScript(" a = malloc 4 ;\n free a ; ").size() == 2);
}

int main() {
    testFreeList();
    testFits();
    testCallocRealloc();
    testErrors();
    testBuddy();
    testSessions();
    return report("test_heap");
}
