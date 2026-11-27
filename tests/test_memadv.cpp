// test_memadv.cpp - M4 advanced memory (core/memadv.*): allocation preset, coalescing,
// compaction, internal fragmentation, base/limit, paging and segmentation translation,
// demand paging (reusing FramePool), write-back, swapping, thrashing, regressions.
#include <set>
#include "check.h"
#include "memadv.h"

static bool layoutConsistent(const std::vector<MemBlock>& l, int total) {
    int at = 0;
    for (const MemBlock& b : l) {
        if (b.start != at || b.size <= 0) return false;
        at += b.size;
    }
    return at == total;
}

int main() {
    // ---------------- contiguous allocation preset (Silberschatz)
    std::vector<MemBlock> mem = layoutFromHoles({100, 500, 200, 300, 600});
    int total = 0;
    for (const MemBlock& b : mem) total += b.size;
    std::vector<AllocOp> ops = parseAllocScript("P1 212, P2 417, P3 112, P4 426");
    auto holeSize = [&](const AllocResult& r, int step) {
        for (const MemBlock& b : (step == 0 ? r.initial : r.steps[step - 1].layout)) if (b.start == r.steps[step].chosen) return b.size;
        return -1;
    };
    AllocResult ff = runAllocation(FitAlgo::First, mem, ops), bf = runAllocation(FitAlgo::Best, mem, ops),
                wf = runAllocation(FitAlgo::Worst, mem, ops);
    CHECK(holeSize(ff, 0) == 500 && holeSize(ff, 1) == 600 && holeSize(ff, 2) == 288 && !ff.steps[3].ok);
    CHECK(holeSize(bf, 0) == 300 && holeSize(bf, 1) == 500 && holeSize(bf, 2) == 200 && holeSize(bf, 3) == 600 && bf.steps[3].ok);
    CHECK(holeSize(wf, 0) == 600 && holeSize(wf, 1) == 500 && holeSize(wf, 2) == 388 && !wf.steps[3].ok);
    CHECK(ff.steps[3].externalFragFailure && wf.steps[3].externalFragFailure);   // enough free space, no single hole
    CHECK(ff.metrics(4).failures == 1 && ff.metrics(4).externalFailures == 1 && bf.metrics(4).failures == 0);
    CHECK(ff.steps[0].candidates.size() == 3);                                    // 500, 300 and 600 were large enough
    for (const AllocResult* r : {&ff, &bf, &wf})
        for (size_t i = 0; i < r->steps.size(); ++i) {
            CHECK(layoutConsistent(r->steps[i].layout, total));
            AllocMetrics m = r->metrics((int)i + 1);
            CHECK(m.allocated + m.freeBytes + 100 == total);                    // OS region is 100
        }
    // free + coalescing: freeing P1 next to the leftover hole merges them back
    AllocResult co = runAllocation(FitAlgo::First, mem, parseAllocScript("P1 212, free P1"));
    CHECK(co.steps[1].layout.size() == mem.size());
    CHECK(co.metrics(2).largestHole == 600 && co.metrics(2).holes == 5);
    // compaction: one hole with all free space, blocks contiguous after the OS
    AllocResult cp = runAllocation(FitAlgo::First, mem, parseAllocScript("P1 212, P2 417, compact, P4 900"));
    CHECK(cp.steps[2].ok && cp.metrics(3).holes == 1 && cp.metrics(3).freeBytes == cp.metrics(2).freeBytes);
    CHECK(cp.steps[3].ok);                                                         // fits only after compaction
    CHECK(layoutConsistent(cp.steps[3].layout, total));
    // fixed partitions: internal fragmentation (waste inside the partition)
    AllocResult fx = runAllocation(FitAlgo::First, fixedPartitions({100, 200, 300}), parseAllocScript("A 90, B 150, C 290, D 10"), true);
    CHECK(fx.steps[0].ok && fx.steps[1].ok && fx.steps[2].ok && !fx.steps[3].ok);
    CHECK(fx.metrics(3).internalWaste == 10 + 50 + 10 && fx.metrics(3).freeBytes == 0);
    CHECK(!runAllocation(FitAlgo::First, fixedPartitions({100}), parseAllocScript("compact"), true).steps[0].ok);
    // validation
    CHECK_THROWS(parseAllocScript("P1 abc"), std::invalid_argument);
    CHECK_THROWS(parseAllocScript("P1 -5"), std::invalid_argument);
    CHECK_THROWS(runAllocation(FitAlgo::First, mem, parseAllocScript("P1 10, P1 20")), std::invalid_argument);
    CHECK(!runAllocation(FitAlgo::First, mem, parseAllocScript("free ghost")).steps[0].ok);
    // base / limit
    CHECK(baseLimitCheck(4000, 600, 250).ok && baseLimitCheck(4000, 600, 250).physical == 4250);
    CHECK(!baseLimitCheck(4000, 600, 600).ok && !baseLimitCheck(4000, 600, -1).ok);

    // ---------------- paging translation
    PagingConfig pc = pagingPreset();
    XlateResult t = translatePaging(pc, 2500);
    CHECK(t.ok() && t.page == 2 && t.offset == 452 && t.frame == 5 && t.physical == 5572);
    CHECK(t.offsetBits == 10 && t.pageBits == 3 && t.steps.size() == 7);
    CHECK(translatePaging(pc, 0).physical == 1024);                 // page 0 -> frame 1
    CHECK(translatePaging(pc, 1024).ok() && translatePaging(pc, 1024).offset == 0 && translatePaging(pc, 1024).physical == 4096);
    CHECK(translatePaging(pc, 1023).physical == 1024 + 1023);       // last byte of page 0
    CHECK(translatePaging(pc, 4096 + 5).outcome == XlateOutcome::PageFault);   // page 4 valid, not present
    CHECK(translatePaging(pc, 8191).outcome == XlateOutcome::InvalidPage);     // page 7 not part of the process
    CHECK(translatePaging(pc, 8192).outcome == XlateOutcome::OutOfRange);
    CHECK(translatePaging(pc, 1100, true).outcome == XlateOutcome::Protection);   // page 1 read-only
    CHECK(translatePaging(pc, 1100, false).ok());
    PagingConfig bad = pc;
    bad.pageSize = 1000;
    CHECK_THROWS(translatePaging(bad, 0), std::invalid_argument);
    bad = pc;
    bad.table[0].frame = 8;                                          // frames are 0..7
    CHECK_THROWS(translatePaging(bad, 0), std::invalid_argument);
    bad = pc;
    bad.table[3].frame = 5;                                          // duplicate of page 2
    CHECK_THROWS(translatePaging(bad, 0), std::invalid_argument);
    bad.allowShared = true;
    CHECK(translatePaging(bad, 3 * 1024).physical == 5 * 1024);      // deliberate shared mapping
    PagingConfig big;
    big.pageSize = 1 << 20;
    big.logicalSize = 1ULL << 32;
    big.physicalSize = 1ULL << 32;
    big.table.assign(4096, PageEntry{});
    big.table[4095] = {4095, true, true, true};
    XlateResult tb = translatePaging(big, (1ULL << 32) - 1);
    CHECK(tb.ok() && tb.physical == (1ULL << 32) - 1);               // top byte, no overflow

    // ---------------- segmentation
    std::vector<SegmentDesc> segs = segmentPreset();
    XlateResult s1 = translateSegment(segs, 8192, 2, 250);
    CHECK(s1.ok() && s1.physical == 4250);
    CHECK(translateSegment(segs, 8192, 2, 600).outcome == XlateOutcome::Protection);
    CHECK(translateSegment(segs, 8192, 2, 599).physical == 4599);
    CHECK(translateSegment(segs, 8192, 9, 0).outcome == XlateOutcome::SegmentMissing);
    CHECK(translateSegment(segs, 8192, 0, 10, 'w').outcome == XlateOutcome::Protection);   // code is not writable
    std::vector<SegmentDesc> overlap = {{0, "a", 0, 100}, {1, "b", 50, 100}};
    CHECK_THROWS(translateSegment(overlap, 1000, 0, 0), std::invalid_argument);
    CHECK_THROWS(translateSegment(segs, 5000, 0, 0), std::invalid_argument);           // data segment beyond memory

    // ---------------- demand paging: same algorithms as the page-replacement tab
    std::vector<DpRef> refs = parseDpRefs("7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2");
    CHECK(runDemandPaging(PageAlgo::FIFO, refs, 3).faults == 10);
    CHECK(runDemandPaging(PageAlgo::LRU, refs, 3).faults == 9);
    CHECK(runDemandPaging(PageAlgo::OPTIMAL, refs, 3).faults == 7);
    std::vector<int> plain = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    std::vector<DpRef> std20;
    for (int p : plain) std20.push_back({p, false});
    for (PageAlgo a : {PageAlgo::FIFO, PageAlgo::LRU, PageAlgo::OPTIMAL}) {
        DpResult d = runDemandPaging(a, std20, 3);
        MemResult m = runPageReplacement(a, plain, 3);
        CHECK(d.faults == m.faults);
        bool same = true;
        for (size_t i = 0; i < plain.size(); ++i) same = same && d.steps[i].frames == m.steps[i].frames;
        CHECK(same);                                                   // identical frame contents every step
    }
    DpResult wb = runDemandPaging(PageAlgo::FIFO, parseDpRefs("1w, 2, 3, 4, 5"), 3);
    CHECK(wb.writeBacks == 1 && wb.steps[3].victim == 1 && wb.steps[3].victimDirty && !wb.steps[4].victimDirty);
    CHECK(wb.cost(DpCost{}) == 5 * 1 + 5 * 100 + 1 * 100);
    for (const DpStep& s : wb.steps) {
        std::set<int> seen;
        for (int f : s.frames) if (f >= 0) CHECK(seen.insert(f).second);   // a page is never in two frames
    }
    CHECK_THROWS(parseDpRefs("1, x"), std::invalid_argument);
    CHECK_THROWS(runDemandPaging(PageAlgo::LRU, refs, 0), std::invalid_argument);

    // ---------------- swapping
    SwapResult sw = runSwapping(100, {{"A", 40}, {"B", 50}, {"C", 30}}, {"A", "B", "C", "A", "B"});
    CHECK(sw.steps[2].swappedOut == std::vector<std::string>{"A"});     // C needs 30, only 10 free
    CHECK(sw.steps[3].swappedOut == std::vector<std::string>{"B"});     // A back in: B loaded longest ago
    CHECK(sw.swapIns == 5 && sw.swapOuts == 3);
    for (const SwapStep& s : sw.steps) CHECK(s.freeAfter >= 0);
    CHECK_THROWS(runSwapping(100, {{"A", 140}}, {"A"}), std::invalid_argument);

    // ---------------- thrashing
    std::vector<int> cyc = cyclicRefs(4, 5);
    ThrashResult t3 = runThrashing(cyc, 3, PageAlgo::LRU), t4 = runThrashing(cyc, 4, PageAlgo::LRU);
    CHECK(t3.faults == 20 && t3.hits == 0);                            // every reference faults
    CHECK(t4.faults == 4 && t4.hits == 16);                            // only the first four
    CHECK(t3.points.back().rollingRate == 1.0 && t4.points.back().rollingRate == 0.0);
    CHECK(t4.points.back().workingSet == 4 && t3.points.back().occupied == 3);
    CHECK(localityRefs(50, 10, 80, 3) == localityRefs(50, 10, 80, 3));
    std::vector<MultiprogPoint> mp = multiprogrammingCurve(24, 4, 12, 200, 10);
    double peak = 0;
    int peakAt = 0;
    for (const MultiprogPoint& p : mp) if (p.utilisation > peak) { peak = p.utilisation; peakAt = p.degree; }
    CHECK(peakAt > 1 && peakAt < 12);                                 // rises, then collapses
    CHECK(mp.back().utilisation < peak / 2);
    CHECK(mp[0].utilisation < peak);
    std::printf("multiprogramming: peak utilisation %.2f at degree %d, degree 12 -> %.2f\n", peak, peakAt, mp.back().utilisation);
    return report("test_memadv");
}
