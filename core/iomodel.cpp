// iomodel.cpp - logical-tick simulation of polling, interrupt-driven I/O and DMA.
#include "iomodel.h"
#include <algorithm>
#include <stdexcept>

const char* ioModeName(IoMode m) {
    switch (m) {
        case IoMode::Polling: return "Polling";
        case IoMode::Interrupt: return "Interrupt-driven";
        case IoMode::DMA: return "DMA";
    }
    return "?";
}

const char* cpuActName(CpuAct a) {
    switch (a) {
        case CpuAct::Useful: return "Useful work";
        case CpuAct::Poll: return "Poll";
        case CpuAct::Copy: return "Copy";
        case CpuAct::ISR: return "ISR";
        case CpuAct::DMASetup: return "DMA setup";
        case CpuAct::DMAComplete: return "DMA done ISR";
        case CpuAct::Idle: return "Idle";
        case CpuAct::BusStall: return "Bus stall";
    }
    return "?";
}

void validateIoParams(const IoParams& p) {
    auto need = [](bool ok, const std::string& m) { if (!ok) throw std::invalid_argument(m); };
    need(p.units >= 1 && p.units <= 256, "data units must be between 1 and 256");
    need(p.deviceInterval >= 1 && p.deviceInterval <= 50, "device interval must be between 1 and 50 ticks");
    need(p.bufferCap >= 1 && p.bufferCap <= 64, "controller buffer must hold 1 to 64 units");
    need(p.pollInterval >= 1 && p.pollInterval <= 50, "polling interval must be between 1 and 50 ticks");
    need(p.cpuWork >= 0 && p.cpuWork <= 100000, "useful CPU work must be between 0 and 100000 ticks");
    need(p.copyCost >= 1 && p.copyCost <= 20, "copy cost must be between 1 and 20 ticks per unit");
    need(p.isrCost >= 1 && p.isrCost <= 20, "ISR cost must be between 1 and 20 ticks");
    need(p.dmaSetup >= 1 && p.dmaSetup <= 50, "DMA setup must be between 1 and 50 ticks (it is never free)");
    need(p.dmaComplete >= 1 && p.dmaComplete <= 20, "DMA completion ISR must be between 1 and 20 ticks");
    need(p.dmaRate >= 1 && p.dmaRate <= 20, "DMA rate must be between 1 and 20 ticks per unit");
}

std::vector<std::string> ioPseudocode(IoMode m) {
    switch (m) {
        case IoMode::Polling:
            return {"loop:", "    do useful work", "    every P ticks: read the device STATUS register   (poll)",
                    "    if status == READY:", "        for each buffered unit: copy device -> RAM   (CPU copies)",
                    "    until all N units are in RAM"};
        case IoMode::Interrupt:
            return {"loop: do useful work", "device: unit ready -> raise IRQ (pending queue)",
                    "CPU at the instruction boundary: IRQ pending?", "    save context, enter ISR (acknowledge)",
                    "    ISR copies the unit device -> RAM", "    restore context, return to the program"};
        case IoMode::DMA:
            return {"CPU: program DMA (source, destination, count N, direction)", "CPU: continue useful work",
                    "DMA: unit ready -> move it device -> RAM over the bus", "DMA: count--   (CPU is not involved)",
                    "DMA: count == 0 -> raise completion IRQ", "CPU: completion ISR, I/O done"};
    }
    return {};
}

// One mode, tick by tick. See iomodel.h for the exact rules and the order inside a tick.
IoResult runIo(IoMode mode, const IoParams& p, int window) {
    validateIoParams(p);
    IoResult r;
    r.mode = mode;
    r.params = p;
    std::vector<int> buffer;                 // unit ids in the controller buffer (FIFO)
    int produced = 0, nextReady = p.deviceInterval, workLeft = p.cpuWork;
    int pending = 0;                         // interrupt mode: pending IRQs (one per unit)
    int nextPoll = 0;
    // multi-tick CPU activity
    CpuAct act = CpuAct::Idle;
    int actLeft = 0;
    std::vector<int> copyQueue;              // units the CPU still has to copy (polling / ISR)
    int copyingUnit = -1, copyLeft = 0;
    // DMA controller
    bool dmaProgrammed = false, dmaCompleteRaised = false, dmaCompleteHandled = false;
    int dmaUnit = -1, dmaLeftTicks = 0;
    bool done = false;
    const int maxTicks = 200000;
    for (int t = 0; t < maxTicks; ++t) {
        if (done && (window <= 0 || t >= window)) break;
        IoTick k;
        k.t = t;
        // (1) device production
        if (produced < p.units && t >= nextReady) {
            if ((int)buffer.size() < p.bufferCap) {
                ++produced;
                buffer.push_back(produced);
                nextReady = t + p.deviceInterval;
                k.events.push_back("device: unit " + std::to_string(produced) + " ready in the controller buffer");
                if (mode == IoMode::Interrupt) {
                    ++pending;
                    k.irqRaised = true;
                    ++r.m.interrupts;
                    k.events.push_back("device raises an interrupt request (pending " + std::to_string(pending) + ")");
                }
            } else {
                k.events.push_back("device stalled: controller buffer full (back-pressure)");
            }
        }
        k.dev = produced >= p.units ? DevState::Done
              : (t >= nextReady && (int)buffer.size() >= p.bufferCap) ? DevState::Stalled : DevState::Producing;

        // (2) CPU action for this tick
        CpuAct cpu = CpuAct::Idle;
        auto startCopy = [&]() {
            if (copyingUnit < 0 && !copyQueue.empty()) {
                copyingUnit = copyQueue.front();
                copyQueue.erase(copyQueue.begin());
                copyLeft = p.copyCost;
            }
        };
        if (actLeft > 0) {                                       // continue ISR / setup / completion
            cpu = act;
            --actLeft;
        } else if (copyingUnit >= 0 || !copyQueue.empty()) {     // continue / start copying
            startCopy();
            cpu = CpuAct::Copy;
        } else if (mode == IoMode::Polling && !done && t >= nextPoll) {
            cpu = CpuAct::Poll;
            ++r.m.polls;
            nextPoll = t + p.pollInterval;
            if (!buffer.empty()) {
                k.events.push_back("CPU polls: status READY, " + std::to_string(buffer.size()) + " unit(s) buffered -> copy them");
                for (int u : buffer) copyQueue.push_back(u);
            } else {
                k.events.push_back("CPU polls: status BUSY (no data) - this check was wasted");
            }
        } else if (mode == IoMode::Interrupt && pending > 0) {
            --pending;
            int unit = buffer.empty() ? -1 : buffer.front();
            copyQueue.push_back(unit);
            cpu = CpuAct::ISR;
            act = CpuAct::ISR;
            actLeft = p.isrCost - 1;
            k.events.push_back("CPU takes the interrupt: context saved, ISR entered for unit " + std::to_string(unit));
        } else if (mode == IoMode::DMA && !dmaProgrammed) {
            cpu = CpuAct::DMASetup;
            act = CpuAct::DMASetup;
            actLeft = p.dmaSetup - 1;
            dmaProgrammed = true;            // takes effect when the setup ticks are over
            k.events.push_back("CPU programs the DMA controller: source device, destination RAM buffer, count " +
                               std::to_string(p.units));
        } else if (mode == IoMode::DMA && dmaCompleteRaised && !dmaCompleteHandled) {
            cpu = CpuAct::DMAComplete;
            act = CpuAct::DMAComplete;
            actLeft = p.dmaComplete - 1;
            dmaCompleteHandled = true;
            k.events.push_back("CPU takes the DMA completion interrupt");
        } else if (workLeft > 0) {
            cpu = CpuAct::Useful;
        } else {
            cpu = CpuAct::Idle;
        }
        // CPU copy of one unit (uses the bus)
        if (cpu == CpuAct::Copy) {
            // the unit leaves the controller buffer when the copy starts
            auto it = std::find(buffer.begin(), buffer.end(), copyingUnit);
            if (it != buffer.end()) buffer.erase(it);
            k.bus = BusUse::CPU;
            k.unitMoving = copyingUnit;
            ++r.m.busCpu;
            if (--copyLeft == 0) {
                r.ramData.push_back(copyingUnit);
                k.events.push_back("CPU copied unit " + std::to_string(copyingUnit) + " device -> RAM");
                copyingUnit = -1;
            }
        }

        // (3) DMA controller (after the setup ticks are complete)
        bool dmaReady = mode == IoMode::DMA && dmaProgrammed && !(cpu == CpuAct::DMASetup);
        if (dmaReady && (int)r.ramData.size() < p.units) {
            if (dmaUnit < 0 && !buffer.empty()) {
                dmaUnit = buffer.front();
                buffer.erase(buffer.begin());
                dmaLeftTicks = p.dmaRate;
            }
            if (dmaUnit >= 0) {
                k.dmaBusy = true;
                k.unitMoving = dmaUnit;
                bool busCycle = dmaLeftTicks == 1;     // the memory-write cycle of this unit
                if (busCycle) {
                    k.bus = BusUse::DMA;
                    ++r.m.busDma;
                    if (cpu == CpuAct::Useful && p.cycleStealing) {
                        cpu = CpuAct::BusStall;      // the DMA steals this bus cycle
                        k.events.push_back("cycle stealing: the DMA uses the bus, the CPU waits one tick");
                    }
                }
                if (--dmaLeftTicks == 0) {
                    r.ramData.push_back(dmaUnit);
                    k.events.push_back("DMA moved unit " + std::to_string(dmaUnit) + " device -> RAM (no CPU copy)");
                    dmaUnit = -1;
                }
            }
            // (4) completion interrupt after the last unit
            if ((int)r.ramData.size() == p.units && !dmaCompleteRaised) {
                dmaCompleteRaised = true;
                k.irqRaised = true;
                ++r.m.interrupts;
                k.events.push_back("DMA count reached 0: completion interrupt raised");
            }
        }
        if (cpu == CpuAct::Useful) --workLeft;

        // accounting
        k.cpu = cpu;
        switch (cpu) {
            case CpuAct::Useful: ++r.m.useful; break;
            case CpuAct::Poll: ++r.m.poll; break;
            case CpuAct::Copy: ++r.m.copy; break;
            case CpuAct::ISR: ++r.m.isr; break;
            case CpuAct::DMASetup: ++r.m.setup; break;
            case CpuAct::DMAComplete: ++r.m.dmaComplete; break;
            case CpuAct::Idle: ++r.m.idle; break;
            case CpuAct::BusStall: ++r.m.stall; break;
        }
        k.buffer = (int)buffer.size();
        k.ram = (int)r.ramData.size();
        k.pending = mode == IoMode::DMA ? (dmaCompleteRaised && !dmaCompleteHandled ? 1 : 0) : pending;
        k.produced = produced;
        k.dmaLeft = mode == IoMode::DMA ? p.units - (int)r.ramData.size() : 0;
        // pseudocode line
        switch (cpu) {
            case CpuAct::Useful: k.pseudoLine = mode == IoMode::DMA ? (k.dmaBusy ? 2 : 1) : (mode == IoMode::Interrupt && k.irqRaised ? 1 : mode == IoMode::Polling ? 1 : 0); break;
            case CpuAct::Poll: k.pseudoLine = 2; break;
            case CpuAct::Copy: k.pseudoLine = 4; break;
            case CpuAct::ISR: k.pseudoLine = 3; break;
            case CpuAct::DMASetup: k.pseudoLine = 0; break;
            case CpuAct::DMAComplete: k.pseudoLine = 5; break;
            case CpuAct::BusStall: k.pseudoLine = 2; break;
            case CpuAct::Idle: k.pseudoLine = mode == IoMode::DMA && k.dmaBusy ? 3 : 0; break;
        }
        if (k.events.empty())
            k.events.push_back(cpu == CpuAct::Useful ? "CPU runs the user program (useful work)"
                               : cpu == CpuAct::Idle ? "CPU idle (no useful work left)" : std::string("CPU: ") + cpuActName(cpu));
        // completion
        if (!done) {
            bool allInRam = (int)r.ramData.size() == p.units;
            bool cpuDone = actLeft == 0 && copyingUnit < 0 && copyQueue.empty();
            bool fin = mode == IoMode::DMA ? (dmaCompleteHandled && actLeft == 0) : (allInRam && cpuDone && pending == 0);
            if (fin) {
                done = true;
                r.completed = true;
                r.m.completion = t + 1;
                k.events.push_back("I/O operation complete at t=" + std::to_string(t + 1) + " (all " +
                                   std::to_string(p.units) + " units in RAM)");
            }
        }
        r.ticks.push_back(k);
    }
    r.m.transferred = (int)r.ramData.size();
    r.m.window = (int)r.ticks.size();
    return r;
}

std::vector<IoResult> compareIo(const IoParams& p) {
    std::vector<IoResult> first;
    int window = 0;
    for (IoMode m : {IoMode::Polling, IoMode::Interrupt, IoMode::DMA}) {
        first.push_back(runIo(m, p));
        window = std::max(window, first.back().m.completion);
    }
    std::vector<IoResult> v;
    for (IoMode m : {IoMode::Polling, IoMode::Interrupt, IoMode::DMA}) {
        IoResult r = runIo(m, p, window);
        r.m.completion = first[(int)m].m.completion;   // latency of the I/O itself
        v.push_back(r);
    }
    return v;
}

std::vector<IoPreset> ioPresets() {
    std::vector<IoPreset> v;
    IoParams basic;
    v.push_back({"Basic (4 units, device every 3 ticks)", "Small workload to compare the three modes step by step.", basic});
    IoParams small = basic;
    small.units = 1;
    small.dmaSetup = 3;
    small.dmaComplete = 2;
    v.push_back({"Small transfer (1 unit)",
                 "With one unit the DMA setup and completion overhead is larger than one ISR + copy: DMA is NOT always best.", small});
    IoParams bulk = basic;
    bulk.units = 32;
    bulk.deviceInterval = 1;
    bulk.copyCost = 2;
    bulk.bufferCap = 4;
    v.push_back({"Bulk transfer (32 units, fast device)",
                 "Many units: polling and interrupts spend most CPU ticks copying; DMA leaves the CPU free.", bulk});
    IoParams slow = basic;
    slow.units = 3;
    slow.deviceInterval = 10;
    slow.pollInterval = 2;
    v.push_back({"Slow device (ready every 10 ticks)", "Readiness is delayed: polling wastes many status checks.", slow});
    IoParams steal = bulk;
    steal.cycleStealing = true;
    steal.dmaRate = 2;
    v.push_back({"DMA with cycle stealing", "The DMA controller owns the bus while it moves data: CPU useful work stalls during those ticks.", steal});
    IoParams tiny = basic;
    tiny.units = 2;
    tiny.cpuWork = 100;
    v.push_back({"Hand-traced example (2 units)", "Matches the hand-traced sequences in the report and tests.", tiny});
    return v;
}
