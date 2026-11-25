// test_memory.cpp - acceptance tests of Section 6.2 (page replacement).
#include "check.h"
#include "memory.h"
#include "io_formats.h"
#include <stdexcept>

int main() {
    std::vector<int> std20 = {7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1};
    CHECK(runPageReplacement(PageAlgo::FIFO, std20, 3).faults == 15);
    CHECK(runPageReplacement(PageAlgo::LRU, std20, 3).faults == 12);
    CHECK(runPageReplacement(PageAlgo::OPTIMAL, std20, 3).faults == 9);

    // Hit ratio
    MemResult lru = runPageReplacement(PageAlgo::LRU, std20, 3);
    CHECK(lru.hits == 8);
    CHECK_NEAR(lru.hitRatio, 0.4);
    CHECK(lru.steps.size() == 20);

    // Frame filling order and recorded eviction (FIFO: 7,0,1 then 2 replaces 7)
    MemResult fifo = runPageReplacement(PageAlgo::FIFO, std20, 3);
    CHECK((fifo.steps[0].frames == std::vector<int>{7, -1, -1}));
    CHECK((fifo.steps[2].frames == std::vector<int>{7, 0, 1}));
    CHECK(fifo.steps[3].replaced == 7 && (fifo.steps[3].frames == std::vector<int>{2, 0, 1}));
    CHECK(!fifo.steps[4].fault && fifo.steps[4].replaced == -1);

    // Belady's anomaly with FIFO
    std::vector<int> bel = {1,2,3,4,1,2,5,1,2,3,4,5};
    CHECK(runPageReplacement(PageAlgo::FIFO, bel, 3).faults == 9);
    CHECK(runPageReplacement(PageAlgo::FIFO, bel, 4).faults == 10);

    // Sample files parse to the same strings
    CHECK(parseRefString(readTextFile(samplePath("refstring_standard.txt"))) == std20);
    CHECK(parseRefString(readTextFile(samplePath("refstring_belady.txt"))) == bel);

    // Parsing
    CHECK((parseRefString("1, 2 3,4") == std::vector<int>{1, 2, 3, 4}));
    CHECK_THROWS(parseRefString("1,a"), std::invalid_argument);
    CHECK_THROWS(parseRefString("1,-2"), std::invalid_argument);
    CHECK_THROWS(parseRefString("3x"), std::invalid_argument);
    CHECK(parseRefString("").empty());
    CHECK_THROWS(runPageReplacement(PageAlgo::LRU, {1, 2}, 0), std::invalid_argument);

    // Frame allocator and page table
    FrameAllocator fa(8);
    CHECK(fa.allocateEqual({1, 2, 3}) == 2);
    CHECK((fa.framesOf(2) == std::vector<int>{2, 3}));
    PageTable pt = buildPageTable(2, 4, fa);
    CHECK(pt.entries.size() == 4);
    CHECK(pt.entries[0].valid && pt.entries[0].frame == 2);
    CHECK(pt.entries[1].valid && pt.entries[1].frame == 3);
    CHECK(!pt.entries[2].valid && pt.entries[2].frame == -1);

    return report("test_memory");
}
