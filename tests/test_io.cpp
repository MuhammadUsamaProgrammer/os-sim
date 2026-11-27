// test_io.cpp - M5 I/O management (core/iomodel.*): hand-traced sequences per mode,
// exact transfer (every unit once, in order), CPU accounting, presets, back-pressure,
// cycle stealing, common comparison window, validation, determinism.
#include <string>
#include "check.h"
#include "iomodel.h"

// One letter per CPU tick: U useful, P poll, C copy, I ISR, S setup, D DMA-done ISR, - idle, B stall
static std::string cpuString(const IoResult& r) {
    std::string s;
    for (const IoTick& k : r.ticks) {
        switch (k.cpu) {
            case CpuAct::Useful: s += 'U'; break;
            case CpuAct::Poll: s += 'P'; break;
            case CpuAct::Copy: s += 'C'; break;
            case CpuAct::ISR: s += 'I'; break;
            case CpuAct::DMASetup: s += 'S'; break;
            case CpuAct::DMAComplete: s += 'D'; break;
            case CpuAct::Idle: s += '-'; break;
            case CpuAct::BusStall: s += 'B'; break;
        }
    }
    return s;
}

static bool exactTransfer(const IoResult& r) {
    if ((int)r.ramData.size() != r.params.units) return false;
    for (int i = 0; i < r.params.units; ++i) if (r.ramData[i] != i + 1) return false;
    const IoMetrics& m = r.m;
    int sum = m.useful + m.poll + m.copy + m.isr + m.setup + m.dmaComplete + m.idle + m.stall;
    if (sum != (int)r.ticks.size() || m.window != (int)r.ticks.size()) return false;
    for (const IoTick& k : r.ticks) if (k.buffer < 0 || k.buffer > r.params.bufferCap) return false;
    return r.completed;
}

int main() {
    // Hand-traced example: N=2, R=3 (units ready at t=3 and t=6), P=2, C=1, I=1, S=2, E=1, D=1.
    IoParams p;
    p.units = 2;
    p.cpuWork = 100;
    IoResult poll = runIo(IoMode::Polling, p);
    IoResult intr = runIo(IoMode::Interrupt, p);
    IoResult dma = runIo(IoMode::DMA, p);
    std::printf("polling   %s\ninterrupt %s\nDMA       %s\n", cpuString(poll).c_str(), cpuString(intr).c_str(), cpuString(dma).c_str());
    // Polling: poll t0, t2 (nothing), t4 finds unit 1, copy t5, poll t6 finds unit 2, copy t7.
    CHECK(cpuString(poll) == "PUPUPCPC");
    CHECK(poll.m.completion == 8 && poll.m.polls == 4 && poll.m.useful == 2 && poll.m.copy == 2);
    // Interrupt: IRQ at t3 -> ISR t3, copy t4; IRQ at t6 -> ISR t6, copy t7.
    CHECK(cpuString(intr) == "UUUICUIC");
    CHECK(intr.m.completion == 8 && intr.m.interrupts == 2 && intr.m.useful == 4 && intr.m.isr == 2);
    // DMA: setup t0-t1, DMA moves unit 1 at t3 and unit 2 at t6, completion ISR t7.
    CHECK(cpuString(dma) == "SSUUUUUD");
    CHECK(dma.m.completion == 8 && dma.m.interrupts == 1 && dma.m.useful == 5 && dma.m.setup == 2 && dma.m.busDma == 2);
    CHECK(dma.m.copy == 0);                                   // the CPU never copies a unit
    for (const IoResult* r : {&poll, &intr, &dma}) CHECK(exactTransfer(*r));
    // interrupts are raised in the tick the unit becomes ready
    CHECK(intr.ticks[3].irqRaised && intr.ticks[6].irqRaised && !intr.ticks[4].irqRaised);

    // Presets: every one completes with an exact transfer in every mode.
    std::vector<IoPreset> pr = ioPresets();
    CHECK(pr.size() >= 5);
    for (const IoPreset& ps : pr)
        for (IoMode m : {IoMode::Polling, IoMode::Interrupt, IoMode::DMA}) CHECK(exactTransfer(runIo(m, ps.params)));

    // Small transfer (1 unit): DMA's fixed overhead is larger than one ISR + copy.
    IoParams small = pr[1].params;
    IoResult si = runIo(IoMode::Interrupt, small), sd = runIo(IoMode::DMA, small);
    CHECK(sd.m.overhead() > si.m.overhead());
    // Bulk transfer: DMA leaves far more useful work than interrupts or polling (common window).
    std::vector<IoResult> bulk = compareIo(pr[2].params);
    CHECK(bulk.size() == 3);
    CHECK(bulk[0].m.window == bulk[1].m.window && bulk[1].m.window == bulk[2].m.window);
    CHECK(bulk[2].m.useful > bulk[1].m.useful && bulk[2].m.useful > bulk[0].m.useful);
    CHECK(bulk[1].m.copy == 32 * 2 && bulk[1].m.interrupts == 32);
    for (const IoResult& r : bulk) CHECK(exactTransfer(r));
    // Slow device: polling wastes many checks before any data is ready.
    IoResult slow = runIo(IoMode::Polling, pr[3].params);
    int wasted = 0;
    for (const IoTick& k : slow.ticks)
        for (const std::string& e : k.events) if (e.find("wasted") != std::string::npos) ++wasted;
    CHECK(wasted >= 10 && slow.m.polls > wasted);
    // Cycle stealing: DMA bus ticks stall the CPU.
    IoResult steal = runIo(IoMode::DMA, pr[4].params);
    CHECK(steal.m.stall > 0 && steal.m.stall <= steal.m.busDma);
    CHECK(runIo(IoMode::DMA, pr[2].params).m.stall == 0);

    // Back-pressure: fast device, tiny buffer, rare polls -> the device stalls but nothing is lost.
    IoParams bp;
    bp.units = 6;
    bp.deviceInterval = 1;
    bp.bufferCap = 2;
    bp.pollInterval = 10;
    IoResult bpr = runIo(IoMode::Polling, bp);
    bool stalled = false;
    for (const IoTick& k : bpr.ticks) stalled = stalled || k.dev == DevState::Stalled;
    CHECK(stalled && exactTransfer(bpr));

    // No useful work available: the CPU is idle, never "useful".
    IoParams idle = p;
    idle.cpuWork = 0;
    IoResult ir = runIo(IoMode::DMA, idle);
    CHECK(ir.m.useful == 0 && ir.m.idle > 0 && exactTransfer(ir));
    // Limited work: useful ticks never exceed the work available.
    idle.cpuWork = 2;
    CHECK(runIo(IoMode::Interrupt, idle).m.useful == 2);

    // Determinism and validation
    CHECK(cpuString(runIo(IoMode::Interrupt, pr[2].params)) == cpuString(runIo(IoMode::Interrupt, pr[2].params)));
    IoParams bad = p;
    bad.units = 0;
    CHECK_THROWS(runIo(IoMode::DMA, bad), std::invalid_argument);
    bad = p;
    bad.dmaSetup = 0;
    CHECK_THROWS(runIo(IoMode::DMA, bad), std::invalid_argument);
    bad = p;
    bad.pollInterval = 0;
    CHECK_THROWS(runIo(IoMode::Polling, bad), std::invalid_argument);
    CHECK(ioPseudocode(IoMode::DMA).size() == 6);
    return report("test_io");
}
