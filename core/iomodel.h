// iomodel.h - I/O management (M5): polling, interrupt-driven I/O and DMA on the SAME workload.
// Conceptual logical-tick model (not hardware emulation, not cycle accurate).
//
// Timing model (documented in README / REPORT):
//  * Device: the n-th data unit becomes ready R ticks after the previous one (first at tick R),
//    and goes into the controller buffer of capacity B. If the buffer is full the device stalls
//    (back-pressure) and produces the unit at the first tick with free space.
//  * Order inside one tick: (1) device production (+ interrupt request in interrupt mode),
//    (2) CPU action (interrupts are taken at the instruction boundary = the start of a tick),
//    (3) DMA controller transfer, (4) a DMA completion interrupt is raised after its last unit.
//  * CPU state is exactly one of: Useful, Poll, Copy, ISR, DMA setup, DMA completion ISR,
//    Idle, Bus stall (mutually exclusive per tick). Device and DMA work overlap the CPU.
//  * Polling: the CPU checks the status register every P ticks (1 tick per check); if data is
//    ready it copies every buffered unit (C ticks each, CPU uses the bus).
//  * Interrupt: each produced unit raises an IRQ that waits in a pending queue (never lost);
//    the CPU runs the ISR (I ticks) and copies that unit (C ticks).
//  * DMA: the CPU programs the DMA controller (S ticks: source, destination, count, direction);
//    the DMA controller moves each unit in D ticks, of which only the last one is a bus (memory
//    write) cycle; after the last unit it raises one completion interrupt, handled in E ticks.
//    With "cycle stealing" on, the CPU needs the bus for its work, so a useful-work tick that
//    coincides with a DMA bus cycle becomes a bus stall (one stolen cycle per unit).
//  * The CPU has W ticks of useful work available; after that it is Idle.
#pragma once
#include <string>
#include <vector>

enum class IoMode { Polling, Interrupt, DMA };
const char* ioModeName(IoMode);
enum class CpuAct { Useful, Poll, Copy, ISR, DMASetup, DMAComplete, Idle, BusStall };
const char* cpuActName(CpuAct);
enum class DevState { Producing, Stalled, Done };
enum class BusUse { None, CPU, DMA };

struct IoParams {
    int units = 4;            // data units to transfer (N)
    int deviceInterval = 3;   // ticks between ready units (R)
    int bufferCap = 8;        // device controller buffer (B)
    int pollInterval = 2;     // ticks between status checks (P)
    int cpuWork = 1000;       // useful CPU work available (W ticks)
    int copyCost = 1;         // CPU ticks to copy one unit (C)
    int isrCost = 1;          // ISR entry/exit ticks per interrupt (I)
    int dmaSetup = 2;         // ticks to program the DMA controller (S)
    int dmaComplete = 1;      // ticks of the DMA completion ISR (E)
    int dmaRate = 1;          // DMA ticks per unit (D)
    bool cycleStealing = false;
};
// Throws std::invalid_argument with a clear message.
void validateIoParams(const IoParams&);

struct IoTick {
    int t = 0;
    CpuAct cpu = CpuAct::Idle;
    DevState dev = DevState::Producing;
    BusUse bus = BusUse::None;
    bool dmaBusy = false;
    bool irqRaised = false;    // an interrupt request was raised in this tick
    int buffer = 0;            // units waiting in the controller buffer (end of tick)
    int ram = 0;               // units in RAM (end of tick)
    int pending = 0;           // pending interrupts (end of tick)
    int produced = 0;
    int dmaLeft = 0;           // DMA count register (units still to move)
    int unitMoving = -1;       // id of the unit on the bus this tick (-1 none)
    int pseudoLine = 0;
    std::vector<std::string> events;
};

struct IoMetrics {
    int completion = 0;        // logical tick when the I/O operation is complete (latency)
    int window = 0;            // observation window used for the CPU accounting below
    int useful = 0, poll = 0, copy = 0, isr = 0, setup = 0, dmaComplete = 0, idle = 0, stall = 0;
    int polls = 0, interrupts = 0, transferred = 0, busCpu = 0, busDma = 0;
    int overhead() const { return poll + copy + isr + setup + dmaComplete; }
    double usefulPct() const { return window > 0 ? 100.0 * useful / window : 0; }
    double throughput() const { return completion > 0 ? double(transferred) / completion : 0; }
};

struct IoResult {
    IoMode mode = IoMode::Polling;
    IoParams params;
    std::vector<IoTick> ticks;
    std::vector<int> ramData;  // unit ids in the order they reached RAM
    IoMetrics m;
    bool completed = false;
};

// window = 0: stop when the I/O completes; window > 0: keep simulating (CPU work / idle)
// until that many ticks, for a common fixed observation window.
IoResult runIo(IoMode mode, const IoParams& p, int window = 0);
// The three modes on the same workload, accounted over a COMMON window (the slowest completion).
std::vector<IoResult> compareIo(const IoParams& p);
std::vector<std::string> ioPseudocode(IoMode mode);

struct IoPreset { std::string name, description; IoParams params; };
std::vector<IoPreset> ioPresets();
