// SampleCatalog.cpp - the built-in sample catalogue of every module (adapters over the EXISTING
// preset providers: core preset lists, tab presets and the files in samples/), the "Load Sample"
// buttons, the guard that stops / pauses / confirms before a load, the File-menu integration and
// the "samples" section of --selftest. Nothing here defines a workload twice: each adapter calls
// the module's own loader (loadPreset, loadScenario, loadCsv, loadFile, ...).
#include "MainWindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QPointer>
#include <QThread>
#include <QToolButton>
#include <cmath>
#include <cstdio>
#include "DeadlockTab.h"
#include "DiskTab.h"
#include "HeapLab.h"
#include "IoTab.h"
#include "IpcTab.h"
#include "JobControlTab.h"
#include "MemAdvTab.h"
#include "MemoryTab.h"
#include "MlqTab.h"
#include "ModuleKit.h"
#include "ModuleManager.h"
#include "ProcessTab.h"
#include "RtSchedTab.h"
#include "SampleRegistry.h"
#include "SchedulingTab.h"
#include "Style.h"
#include "Theme.h"
#include "StorageTab.h"
#include "SyncTab.h"
#include "SysMonTab.h"
#include "ThreadsTab.h"
#include "disk.h"
#include "io_formats.h"
#include "heap.h"
#include "iomodel.h"
#include "ipc.h"
#include "jobctl.h"
#include "mlq.h"
#include "procmgmt.h"
#include "rtsched.h"
#include "sync.h"
#include "threads.h"
#include "vfs.h"

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString sp(const QString& file) { return ui::samplesDir() + "/" + file; }

// Small builder so that each catalogue entry reads like one line of a table.
struct B {
    SampleDef d;
    B(const QString& module, const QString& key, const QString& name, const QString& desc) {
        d.module = module; d.id = module + "/" + key; d.name = name; d.description = desc;
    }
    B& cat(const QString& c) { d.category = c; return *this; }
    B& mode(const QString& m) { d.mode = m; return *this; }
    B& params(const QString& p) { d.params = p; return *this; }
    B& level(const QString& l) { d.level = l; return *this; }
    B& file(const QString& f) { d.file = f; return *this; }
    B& shared() { d.replacesShared = true; return *this; }
    B& load(std::function<bool()> f) { d.load = std::move(f); return *this; }
    B& run(std::function<bool()> f) { d.run = std::move(f); return *this; }
};

const char* kBasic = "Basic Examples";
const char* kCourse = "Course Examples";
const char* kAlgo = "Algorithm-Specific Examples";
const char* kAdv = "Advanced Scenarios";
const char* kSpecial = "Special Cases";
}

// ================================================================ catalogue

void MainWindow::registerSamples() {
    samples_ = new SampleRegistry(this);
    auto reg = [this](const B& b) {
        SampleDef d = b.d;
        // Load && Run for playback modules: start the page's deterministic playback (never a live demo).
        QWidget* page = modules_->page(d.module);
        if (!d.run && page && page->findChild<PlaybackBar*>() && !d.mode.startsWith("Live")) {
            QPointer<QWidget> pg = page;
            d.run = [pg] { PlaybackBar* bar = pg ? pg->findChild<PlaybackBar*>() : nullptr; if (bar) bar->run(); return bar != nullptr; };
        }
        if (!samples_->add(d)) std::fprintf(stderr, "[samples] rejected duplicate / invalid sample %s\n", qPrintable(d.id));
    };

    // ---------------------------------------------------------------- Scheduling > CPU Scheduling
    const QString cpu = "scheduling.cpu";
    auto cpuLoad = [this](const QString& file, Algo a, int quantum) {
        return [this, file, a, quantum] {
            sched_->clearResults();
            if (!sched_->loadCsv(sp(file))) return false;
            sched_->setAlgorithm(a);
            sched_->setQuantum(quantum);
            return true;
        };
    };
    auto cpuRun = [this] { return sched_->runOnce(); };
    auto cpuSample = [&](const QString& key, const QString& name, const QString& desc, const char* cat, const QString& file,
                         Algo a, int quantum, const QString& params, const QString& level) {
        reg(B(cpu, key, name, desc).cat(cat).mode(algoName(a) + (a == Algo::RR ? QString(" q=%1").arg(quantum) : QString()))
                .params(params).level(level).file(file).load(cpuLoad(file, a, quantum)).run(cpuRun));
    };
    cpuSample("fcfs-convoy", "FCFS: convoy effect (24 / 3 / 3)", "Three processes arrive at t=0; the long P1 runs first and the "
              "short jobs wait behind it. Expected avg WT = 17, avg TAT = 27.", kBasic, "processes_fcfs.csv", Algo::FCFS, 4,
              "P1 0 24, P2 0 3, P3 0 3", "Beginner");
    cpuSample("sjf", "SJF: shortest job first (same input)", "Non-preemptive SJF on the convoy input: order P2, P3, P1, avg WT = 3.",
              kBasic, "processes_fcfs.csv", Algo::SJF, 4, "P1 0 24, P2 0 3, P3 0 3", "Beginner");
    cpuSample("srtf", "SRTF: preemption by a shorter job", "Arrivals at 0, 1, 2, 3: a shorter remaining time preempts the running "
              "process. Expected avg WT = 6.5 (SJF on the same input: 7.75).", kBasic, "processes_srtf.csv", Algo::SRTF, 4,
              "P1 0 8, P2 1 4, P3 2 9, P4 3 5", "Intermediate");
    cpuSample("rr", "Round Robin, quantum 4", "Each process runs at most 4 ticks, then goes to the back of the ready queue. "
              "Expected WT P1 6, P2 4, P3 7, avg 5.67.", kBasic, "processes_fcfs.csv", Algo::RR, 4, "q = 4, P1 0 24, P2 0 3, P3 0 3",
              "Beginner");
    cpuSample("priority-np", "Priority (non-preemptive)", "Lower number = higher priority. Order P2, P5, P1, P3, P4, avg WT = 8.2.",
              kBasic, "processes_priority.csv", Algo::PRIORITY_NP, 4, "5 processes at t=0, priorities 1..5", "Beginner");
    cpuSample("ljf", "LJF: longest job first", "Non-preemptive LJF with different arrivals: P1 (alone at t=0) runs to completion, "
              "then the longest waiting job P3, then P4, then P2. Expected avg WT = 10.25 (SJF: 7.75).", kAlgo,
              "processes_ljf_lrtf.csv", Algo::LJF, 4, "P1 0 8, P2 1 4, P3 2 9, P4 3 5", "Intermediate");
    cpuSample("lrtf", "LRTF: preemption by a longer remaining time", "Preemptive LRTF on the same input: whenever a ready process has "
              "STRICTLY more remaining time it takes the CPU, so the processes alternate and all finish near the end "
              "(16 context switches, avg WT = 16.5; SRTF: 6.5).", kAlgo, "processes_ljf_lrtf.csv", Algo::LRTF, 4,
              "P1 0 8, P2 1 4, P3 2 9, P4 3 5", "Intermediate");
    cpuSample("priority-p", "Priority (preemptive)", "A newly ready process with a higher priority (lower number) takes the CPU at once.",
              kAlgo, "processes_priority.csv", Algo::PRIORITY_P, 4, "5 processes at t=0", "Intermediate");
    cpuSample("ljf-ties", "LJF: simultaneous arrivals, equal bursts", "All arrive at t=0; P2 and P4 both need 6 ticks: the tie goes to "
              "the earlier arrival, then the lower pid (P2 before P4). Order P2, P4, P3, P1, avg WT = 8.5.", kSpecial,
              "processes_ljf_ties.csv", Algo::LJF, 4, "P1 0 3, P2 0 6, P3 0 4, P4 0 6", "Intermediate");
    cpuSample("lrtf-ties", "LRTF: equal remaining times", "The running process keeps the CPU while another one has the SAME remaining "
              "time (no needless context switch); only a strictly longer remaining time preempts it.", kSpecial,
              "processes_ljf_ties.csv", Algo::LRTF, 4, "P1 0 3, P2 0 6, P3 0 4, P4 0 6", "Advanced");
    cpuSample("idle", "CPU idle interval", "Nobody is ready between t=2 and t=5: the Gantt chart shows an idle block and CPU "
              "utilisation drops below 100 %.", kSpecial, "processes_idle_gap.csv", Algo::FCFS, 4, "P1 0 2, P2 5 3, P3 6 1", "Beginner");
    cpuSample("starvation", "Starvation of a short job under LJF", "The 1-tick job P2 arrives at t=1 but every longer job is chosen "
              "first: it waits 26 ticks under LJF (9 under SJF). Under SJF the long jobs are the ones that can starve.", kSpecial,
              "processes_ljf_starvation.csv", Algo::LJF, 4, "P1 0 10, P2 1 1, P3 2 9, P4 3 8", "Intermediate");
    reg(B(cpu, "compare", "Algorithm comparison workload", "Press Compare all: the eight standard algorithms (FCFS, SJF, SRTF, LJF, "
          "LRTF, RR, Priority NP / P) on the same four processes; the best value of each column is green.").cat(kAdv)
            .mode("Compare all").params("P1 0 8, P2 1 4, P3 2 9, P4 3 5, q = 4").level("Intermediate").file("processes_srtf.csv")
            .load(cpuLoad("processes_srtf.csv", Algo::SRTF, 4)).run([this] { return sched_->compareAll(); }));
    auto integrated = [this](bool avoidance) {
        return [this, avoidance] {
            sched_->clearResults();
            if (!sched_->loadCsv(sp("processes_srtf.csv"))) return false;
            sched_->setAlgorithm(Algo::RR);
            sched_->setQuantum(2);
            sched_->setIntegratedOptions(sp("requests_integrated.csv"), avoidance, true, 6, 5);
            return true;
        };
    };
    reg(B(cpu, "integrated-avoid", "Integrated run: Banker's avoidance ON", "Scheduling + paging + scripted resource requests. "
          "With Banker's avoidance no deadlock can occur (some requests wait). Open the Integrated run page and press Run integrated.")
            .cat(kAdv).mode("Integrated (RR q=2, LRU, avoidance)").params("6 frames, detection every 5 ticks").level("Advanced")
            .file("requests_integrated.csv").load(integrated(true)).run([this] { return sched_->runIntegrated(); }));
    reg(B(cpu, "integrated-detect", "Integrated run: detection and recovery", "Same workload with avoidance OFF: a real deadlock "
          "happens, the detection algorithm finds it and recovery terminates a victim.").cat(kAdv)
            .mode("Integrated (RR q=2, LRU, detection)").params("6 frames, detection every 5 ticks, auto-recover").level("Advanced")
            .load(integrated(false)).run([this] { return sched_->runIntegrated(); }));

    // ---------------------------------------------------------------- Scheduling > Multilevel Queues
    {
        std::vector<MlqPreset> v = mlqPresets();
        const char* cats[] = {kBasic, kAlgo, kAdv};
        for (int i = 0; i < (int)v.size(); ++i)
            reg(B("scheduling.mlq", QString("preset-%1").arg(i), q(v[i].name), q(v[i].description)).cat(cats[std::min(i, 2)])
                    .mode(v[i].cfg.feedback ? "MLFQ" : "MLQ")
                    .params(QString("%1 queue(s), %2 processes%3").arg(v[i].cfg.queues.size()).arg(v[i].procs.size())
                                .arg(v[i].cfg.boostInterval ? QString(", boost every %1").arg(v[i].cfg.boostInterval) : QString()))
                    .load([this, i] { return mlq_->loadPreset(i); }));
    }
    // ---------------------------------------------------------------- Scheduling > Real-Time
    {
        std::vector<RtPreset> v = rtPresets();
        for (int i = 0; i < (int)v.size(); ++i) {
            QString n = q(v[i].name);
            const char* c = n.contains("Fictional") ? kCourse : (n.contains("Overload") || n.contains("Constrained")) ? kSpecial
                          : i == 0 ? kAlgo : kBasic;
            reg(B("scheduling.realtime", QString("preset-%1").arg(i), n, q(v[i].description)).cat(c).mode("RM / EDF")
                    .params(QString("%1 task(s), horizon %2").arg(v[i].cfg.tasks.size()).arg(v[i].cfg.horizon))
                    .load([this, i] { return rt_->loadPreset(i); }));
        }
    }

    // ---------------------------------------------------------------- Processes > Lifecycle
    {
        std::vector<LcPreset> v = lifecyclePresets();
        for (int i = 0; i < (int)v.size(); ++i)
            reg(B("processes.lifecycle", QString("preset-%1").arg(i), q(v[i].name), q(v[i].description))
                    .cat(i == 2 ? kSpecial : i == 3 ? kAdv : kBasic).mode(QString("Five-state model, ") + algoName(v[i].policy))
                    .params(QString("%1 process(es), q = %2").arg(v[i].procs.size()).arg(v[i].quantum))
                    .load([this, i] { return proc_->loadPreset(i); }));
        reg(B("processes.lifecycle", "file-reference", "Reference workload (file)", "The reference workload from samples/: "
              "creation, I/O blocking and termination with the Five-state model.").cat(kCourse).mode("Five-state model")
                .file("lifecycle_reference.csv").load([this] { return proc_->loadFile(sp("lifecycle_reference.csv")); }));
        reg(B("processes.lifecycle", "file-tree", "Parent-child process tree (file)", "A process tree with parents, children and a "
              "grandchild (PPID column).").cat(kCourse).mode("Process tree").file("lifecycle_tree.csv")
                .load([this] { return proc_->loadFile(sp("lifecycle_tree.csv")); }));
        for (const char* key : {"fork", "wait", "zombie", "orphan"})
            reg(B("processes.lifecycle", QString("live-%1").arg(key), QString("Live Linux demo: %1").arg(key), ProcessTab::liveDemoText(key) +
                  " Selected only - press Start live demo yourself (application-owned helper processes).")
                    .cat("Live Linux Demos (start manually)").mode("Live Linux Demo").level("Intermediate")
                    .load([this, key] { return proc_->selectLiveDemo(key); }));
    }
    // ---------------------------------------------------------------- Processes > System Monitor
    {
        struct W { const char* key; const char* name; const char* text; int s, mib, busy, cpuThr, memThr, deb; };
        const W ws[] = {
            {"controlled", "Controlled demo workload", "A moderate owned worker (40 % of one core, 32 MiB, 30 s): find it in the "
             "process table with 'Only OS-Sim demo processes'.", 30, 32, 40, 90, 90, 3},
            {"cpu", "CPU-intensive demo", "The worker keeps one core busy (95 %) with little memory: watch the CPU history.", 20, 8, 95, 90, 90, 3},
            {"memory", "Memory-intensive demo", "The worker touches 192 MiB (becomes resident) with low CPU use: watch RSS and memory.",
             20, 192, 5, 90, 90, 3},
            {"alert", "Resource alert demonstration", "A low CPU alert threshold (20 %) with a short debounce (2 samples) and a busy "
             "worker: an informational alert appears after start.", 20, 16, 90, 20, 90, 2},
        };
        for (const W& w : ws)
            reg(B("processes.monitor", w.key, w.name, QString(w.text) + " Inputs only - press Start worker yourself.")
                    .cat("Controlled demo workloads").mode("Live System (read-only) + owned demo worker")
                    .params(QString("%1 s, %2 MiB, %3 % CPU").arg(w.s).arg(w.mib).arg(w.busy))
                    .load([this, w] { sysmon_->applyDemoConfig(w.s, w.mib, w.busy, w.cpuThr, w.memThr, w.deb); return true; }));
    }
    // ---------------------------------------------------------------- Processes > Job Control
    {
        std::vector<JcScenario> v = jobScenarios();
        for (int i = 0; i < (int)v.size(); ++i) {
            const char* c = (i <= 2) ? kBasic : (i == 6) ? kSpecial : kAdv;
            reg(B("processes.jobs", QString("scenario-%1").arg(i), q(v[i].name), q(v[i].description)).cat(c)
                    .mode("Simulated shell").params(QString("%1 command(s)").arg(v[i].commands.size()))
                    .load([this, i] { return jobs_->loadScenario(i); }));
        }
    }

    // ---------------------------------------------------------------- Memory > Page replacement
    {
        const QString m = "memory.replacement";
        auto memLoad = [this](const QString& file, int frames, int mode) {
            return [this, file, frames, mode] {
                std::string text;
                try { text = readTextFile(sp(file).toStdString()); parseRefString(text); }
                catch (const std::exception& e) { ui::warn(this, QString("Could not load %1:\n%2").arg(file, e.what())); return false; }
                mem_->clearResults();
                mem_->setRefString(QString::fromStdString(text).simplified());
                mem_->setFrames(frames);
                mem_->setMode(mode);
                return true;
            };
        };
        auto memRun = [this] { return mem_->run(); };
        const char* names[] = {"FIFO", "LRU", "Optimal"};
        const int faults[] = {15, 12, 9};
        for (int a = 0; a < 3; ++a)
            reg(B(m, QString("standard-%1").arg(QString(names[a]).toLower()), QString("Standard string, %1").arg(names[a]),
                  QString("The 20-reference textbook string with 3 frames: %1 gives %2 page faults.").arg(names[a]).arg(faults[a]))
                    .cat(kCourse).mode(names[a]).params("7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1 - 3 frames").level("Beginner")
                    .file("refstring_standard.txt").load(memLoad("refstring_standard.txt", 3, a)).run(memRun));
        reg(B(m, "standard-compare", "Standard string, compare all", "FIFO 15, LRU 12, Optimal 9 faults side by side with a bar chart.")
                .cat(kAlgo).mode("Compare all").params("3 frames").file("refstring_standard.txt")
                .load(memLoad("refstring_standard.txt", 3, 3)).run(memRun));
        reg(B(m, "belady-3", "Belady's anomaly, 3 frames", "FIFO on 1,2,3,4,1,2,5,1,2,3,4,5 with 3 frames: 9 faults.").cat(kSpecial)
                .mode("FIFO").params("3 frames").file("refstring_belady.txt").load(memLoad("refstring_belady.txt", 3, 0)).run(memRun));
        reg(B(m, "belady-4", "Belady's anomaly, 4 frames", "The same string with 4 frames gives MORE faults (10): more memory, "
              "worse FIFO result.").cat(kSpecial).mode("FIFO").params("4 frames").file("refstring_belady.txt")
                .load(memLoad("refstring_belady.txt", 4, 0)).run(memRun));
    }
    // ---------------------------------------------------------------- Memory > Phase 2 sub-pages
    struct MemPage { const char* id; const char* page; const char* mode; std::function<bool(int)> load; };
    const MemPage pages[] = {
        {"memory.allocation", "allocation", "First / Best / Worst Fit", [this](int i) { return memArea_->loadAllocPreset(i); }},
        {"memory.paging", "paging", "Paging: address translation", [this](int i) { return memArea_->loadPagingPreset(i); }},
        {"memory.segmentation", "segmentation", "Segmentation", [this](int i) { return memArea_->loadSegPreset(i); }},
        {"memory.demand", "demand", "Demand paging (FIFO / LRU / Optimal)", [this](int i) { return memArea_->loadDemandPreset(i); }},
        {"memory.swapping", "swapping", "Swapping (whole processes)", [this](int i) { return memArea_->loadSwapPreset(i); }},
        {"memory.thrashing", "thrashing", "Thrashing / working set", [this](int i) { return memArea_->loadThrashPreset(i); }},
    };
    for (const MemPage& mp : pages) {
        QList<QPair<QString, QString>> list = MemAdvArea::presetList(mp.page);
        for (int i = 0; i < list.size(); ++i) {
            QString n = list[i].first;
            const char* c = i == 0 ? kCourse : (n.startsWith("Edge") || n.contains("Invalid") || n.contains("Outside") || n.contains("No such")
                                                || n.contains("read-only") || n.contains("code segment") || n.contains("Zero")
                                                || n.contains("boundary") || n.contains("Last")) ? kSpecial : kBasic;
            auto load = mp.load;
            reg(B(mp.id, QString("preset-%1").arg(i), n, list[i].second).cat(c).mode(mp.mode).load([load, i] { return load(i); }));
        }
    }
    // ---------------------------------------------------------------- Memory > Heap Laboratory
    {
        std::vector<HeapPreset> v = heapPresets();
        for (int i = 0; i < (int)v.size(); ++i) {
            QString n = q(v[i].name);
            const char* c = i == 0 ? kBasic : (n.startsWith("Errors") || n.startsWith("Corruption")) ? kSpecial
                          : n.startsWith("Buddy") ? kAlgo : kAdv;
            reg(B("memory.heap", QString("preset-%1").arg(i), n, q(v[i].description)).cat(c)
                    .mode(QString("%1%2").arg(heapFamilyName(v[i].cfg.family))
                              .arg(v[i].cfg.family == HeapFamily::FreeList ? QString(", ") + fitAlgoName(v[i].cfg.fit) : QString()))
                    .params(QString("%1 B heap, alignment %2").arg(v[i].cfg.capacity).arg(v[i].cfg.alignment))
                    .load([this, i] { return memArea_->heapLab() ? memArea_->heapLab()->loadPreset(i) : false; }));
        }
    }

    // ---------------------------------------------------------------- Deadlock
    {
        const QString m = "deadlock";
        auto file = [this](const QString& f) { return [this, f] { return dead_->loadFile(sp(f)); }; };
        reg(B(m, "safe", "Safe state (Banker's sample)", "Silberschatz example (A=10, B=5, C=7): Check safety gives SAFE with the "
              "sequence P1, P3, P4, P0, P2.").cat(kCourse).mode("Banker's safety algorithm").params("5 processes, 3 resource types")
                .level("Beginner").file("banker_sample.txt").load(file("banker_sample.txt")).run([this] { return dead_->checkSafety(); }));
        reg(B(m, "request", "Resource request: P1 asks 1 0 2", "The request inputs are filled in: press Request resources. The "
              "request is granted because the state after it is still safe.").cat(kBasic).mode("Banker's request algorithm")
                .params("P1 requests (1, 0, 2)").file("banker_sample.txt")
                .load([this] { if (!dead_->loadFile(sp("banker_sample.txt"))) return false; dead_->prefillRequest(1, "1 0 2"); return true; })
                .run([this] { return dead_->requestResources(1, "1 0 2"); }));
        reg(B(m, "unsafe", "Unsafe state", "The Banker's sample after P1's request was granted and P0's request (0 2 0) "
              "pretend-allocated: no process can finish with Available = 2 1 0, so the state is UNSAFE (not yet a deadlock).")
                .cat(kSpecial).mode("Banker's safety algorithm").params("Available 2 1 0").file("banker_unsafe.txt")
                .load(file("banker_unsafe.txt")).run([this] { return dead_->checkSafety(); }));
        reg(B(m, "detect", "Deadlock detection (P1-P4)", "Detection example (A=7, B=2, C=6) with P2 requesting 0 0 1: Detect deadlock "
              "marks P1, P2, P3 and P4 red.").cat(kCourse).mode("Detection algorithm").params("5 processes, Available 0 0 0")
                .file("deadlock_sample.txt").load(file("deadlock_sample.txt")).run([this] { return dead_->detect(); }));
        reg(B(m, "recover", "Deadlock recovery by termination", "Same deadlocked state: press Detect, then Recover - victims (lowest "
              "total allocation first) are terminated one at a time until the deadlock is gone.").cat(kAdv).mode("Recovery by termination")
                .file("deadlock_sample.txt").load(file("deadlock_sample.txt"))
                .run([this] { return dead_->detect() && dead_->recover(ui::selfTestMode() ? 0 : 700); }));
        reg(B(m, "rag", "Resource Allocation Graph with a cycle", "Two processes, two single-instance resources: P0 -> R1 -> P1 -> R0 "
              "-> P0. With one instance per resource the cycle is a deadlock; the RAG draws it in red.").cat(kBasic)
                .mode("Resource Allocation Graph").params("2 processes, 2 resources, 1 instance each").file("deadlock_rag_cycle.txt")
                .load(file("deadlock_rag_cycle.txt")).run([this] { return dead_->detect(); }));
    }

    // ---------------------------------------------------------------- Synchronization
    for (int a = 0; a <= (int)SyncAlgo::DINING_PHILOSOPHERS; ++a) {
        std::vector<SyncScenario> v = syncScenarios((SyncAlgo)a);
        for (int i = 0; i < (int)v.size(); ++i)
            reg(B("synchronization", QString("algo%1-%2").arg(a).arg(i), q(v[i].name), q(v[i].description)).cat(syncAlgoName((SyncAlgo)a))
                    .mode(syncAlgoName((SyncAlgo)a)).params(QString("%1 process(es)").arg(v[i].cfg.processes))
                    .load([this, a, i] { sync_->setAlgorithm((SyncAlgo)a); return sync_->loadScenario(i); })
                    .run([this] { sync_->run(); return true; }));
    }

    // ---------------------------------------------------------------- Threads, IPC, I/O
    {
        std::vector<ThreadPreset> v = threadPresets();
        for (int i = 0; i < (int)v.size(); ++i)
            reg(B("threads", QString("preset-%1").arg(i), q(v[i].name), q(v[i].description)).cat(i < 3 ? "Threading models" : "Blocking and joining")
                    .mode(threadModelName(v[i].cfg.model))
                    .params(QString("%1 thread(s), %2 core(s)").arg(v[i].cfg.threads.size()).arg(v[i].cfg.cores))
                    .load([this, i] { return threads_->loadPreset(i); }));
    }
    {
        std::vector<IpcPreset> v = ipcPresets();
        for (int i = 0; i < (int)v.size(); ++i) {
            QString mode = ipcModeName(v[i].cfg.mode);
            if (v[i].cfg.mode == IpcMode::MessagePassing) mode += v[i].cfg.direct ? " (direct)" : " (indirect, mailbox)";
            if (v[i].cfg.mode == IpcMode::Pipe) mode += v[i].cfg.namedPipe ? " (named FIFO)" : " (anonymous)";
            reg(B("ipc", QString("preset-%1").arg(i), q(v[i].name), q(v[i].description)).cat(ipcModeName(v[i].cfg.mode)).mode(mode)
                    .params(QString("%1 operation(s)").arg(v[i].ops.size()))
                    .load([this, i] { return ipc_->loadPreset(i); }));
        }
    }
    {
        std::vector<IoPreset> v = ioPresets();
        for (int i = 0; i < (int)v.size(); ++i) {
            QString n = q(v[i].name);
            const char* c = n.startsWith("Hand") ? kCourse : n.startsWith("Basic") ? kBasic
                          : (n.startsWith("Small") || n.startsWith("Slow")) ? kSpecial : kAdv;
            reg(B("io", QString("preset-%1").arg(i), n, q(v[i].description)).cat(c).mode("Polling / Interrupt / DMA")
                    .params(QString("%1 unit(s), device ready every %2 ticks").arg(v[i].params.units).arg(v[i].params.deviceInterval))
                    .load([this, i] { return io_->loadPreset(i); }));
        }
        reg(B("io", "compare", "Algorithm comparison (basic workload)", "The basic workload; the comparison table shows CPU ticks "
              "spent on I/O, useful work and completion time for polling, interrupts and DMA.").cat(kAlgo).mode("Compare all three")
                .load([this] { return io_->loadPreset(0); }));
    }

    // ---------------------------------------------------------------- Disk
    {
        const QString m = "disk";
        QStringList names = DiskTab::presetNames();
        for (int i = 0; i < names.size(); ++i) {
            const char* c = i == 0 ? kCourse : names[i].startsWith("Edge") ? kSpecial : kBasic;
            reg(B(m, QString("preset-%1").arg(i), names[i], names[i].startsWith("Edge") ? "Boundary behaviour of every algorithm: " + names[i].mid(6) + "."
                                                          : "Request queue " + DiskTab::presetParams(i) + ".")
                    .cat(c).mode("any algorithm").params(DiskTab::presetParams(i)).load([this, i] { return disk_->loadPreset(i); }));
        }
        for (DiskAlgo a : allDiskAlgos())
            reg(B(m, QString("textbook-%1").arg(QString(diskAlgoName(a)).toLower()), QString("Textbook queue with %1").arg(diskAlgoName(a)),
                  QString("Head 53 moving to higher cylinders, queue 98 183 37 122 14 124 65 67, serviced by %1.").arg(diskAlgoName(a)))
                    .cat(kAlgo).mode(diskAlgoName(a)).params(DiskTab::presetParams(0))
                    .load([this, a] { if (!disk_->loadPreset(0)) return false; disk_->setAlgorithm(a); return disk_->rebuild(); }));
        reg(B(m, "compare", "Algorithm comparison (all six)", "The textbook queue; Compare all six gives the total head movement of "
              "FCFS 640, SSTF 236, SCAN 331, C-SCAN 382, LOOK 299, C-LOOK 322.").cat(kAdv).mode("Compare all six")
                .params(DiskTab::presetParams(0)).load([this] { return disk_->loadPreset(0); }).run([this] { return disk_->compare(); }));
        reg(B(m, "file-reference", "Reference queue (file)", "The textbook request queue read from samples/disk_reference.txt.").cat(kCourse)
                .mode("any algorithm").file("disk_reference.txt")
                .load([this] { return disk_->loadFile(sp("disk_reference.txt")); }));
        reg(B(m, "file-edge", "Edge cases (file)", "samples/disk_edge_cases.txt: requests at the boundaries and duplicates.")
                .cat(kSpecial).mode("any algorithm").file("disk_edge_cases.txt").load([this] { return disk_->loadFile(sp("disk_edge_cases.txt")); }));
    }

    // ---------------------------------------------------------------- Storage & Files (one shared virtual disk)
    {
        auto sampleDisk = [this](AllocStrategy s) {
            const VirtualFs& fs = storage_->fs();
            return storage_->newDisk(fs.blockCount(), fs.blockSize(), s, true);
        };
        auto ex = [this, sampleDisk](const QString& cwd, const QString& sel, const QString& path, const QString& dest) {
            return [=] {
                if (!sampleDisk(storage_->fs().strategy())) return false;
                storage_->explorer()->setCwd(cwd);
                if (!sel.isEmpty()) storage_->explorer()->select(sel);
                storage_->explorer()->prefill(path, dest);
                return true;
            };
        };
        const QString e = "storage.explorer";
        reg(B(e, "hierarchy", "Sample directory hierarchy", "/home (alice, bob, carol), /shared, /pub, /tmp with files, links and "
              "permissions.").cat(kBasic).mode("Virtual filesystem").shared().load(ex("/", "", "", "")));
        reg(B(e, "create-delete", "File creation and deletion", "The path field holds /tmp/new.txt: press Create, Write, then Delete "
              "and watch the inode and block list.").cat(kBasic).mode("Virtual filesystem").shared()
                .load(ex("/tmp", "", "/tmp/new.txt", "Hello from OS-Sim")));
        reg(B(e, "hard-links", "Hard links", "/home/carol/readme-link is a second NAME for the inode of /pub/readme.txt (link count 2); "
              "deleting one name keeps the data.").cat(kAlgo).mode("Hard link").shared()
                .load(ex("/home/carol", "/home/carol/readme-link", "/pub/readme.txt", "/tmp/readme-hard")));
        reg(B(e, "symlinks", "Symbolic links", "/home/carol/pub points to /pub, /home/carol/old is dangling and /tmp/loopA <-> loopB is "
              "a loop (resolution stops with ELOOP).").cat(kAlgo).mode("Symbolic link").shared()
                .load(ex("/home/carol", "/home/carol/old", "/pub", "/tmp/pub-link")));
        reg(B(e, "descriptors", "File descriptors", "/home/alice/notes.txt is selected: open it in a process, read, seek, dup and close "
              "to see the per-process descriptor table and the open-file table.").cat(kAdv).mode("File descriptors").shared()
                .load(ex("/home/alice", "/home/alice/notes.txt", "/home/alice/notes.txt", "")));
        const QString ac = "storage.access";
        auto acc = [this, sampleDisk](int user, const QString& path, int preset) {
            return [=] {
                if (!sampleDisk(storage_->fs().strategy())) return false;
                storage_->fs().setCurrentUser(user);
                storage_->notifyChanged();
                storage_->access()->setPath(path);
                return preset < 0 ? true : storage_->access()->applyPreset(preset);
            };
        };
        reg(B(ac, "ugo", "Owner, group and others", "/shared/plan.txt (660, group staff): the matrix shows which users may read / "
              "write it - owner and group members yes, others no.").cat(kBasic).mode("rwx permission bits").shared()
                .load(acc(1000, "/shared/plan.txt", -1)));
        reg(B(ac, "rwx", "Read, write and execute bits", "/home/alice/todo.txt is 640: owner rw-, group r--, others ---. Tick bits and "
              "press chmod to change them.").cat(kBasic).mode("rwx permission bits").shared().load(acc(1000, "/home/alice/todo.txt", -1)));
        reg(B(ac, "denied", "Permission denied", "Current user bob looks at alice's private notes (600): every operation is denied "
              "(EACCES) - only the owner (and root) may access it.").cat(kSpecial).mode("Access check").shared()
                .load(acc(1001, "/home/alice/notes.txt", -1)));
        QStringList pn = AccessPage::presetNames();
        for (int i = 0; i < pn.size(); ++i) {
            bool dir = pn[i].contains("directory");
            reg(B(ac, QString("mode-%1").arg(i), pn[i], QString("chmod by alice on %1 to the preset mode; the matrix shows the effect.")
                  .arg(dir ? "/home/alice" : "/home/alice/todo.txt")).cat("Permission presets").mode("chmod").shared()
                    .load(acc(1000, dir ? "/home/alice" : "/home/alice/todo.txt", i)));
        }
        const QString bl = "storage.blocks";
        const AllocStrategy strategies[] = {AllocStrategy::Contiguous, AllocStrategy::Linked, AllocStrategy::Indexed};
        for (AllocStrategy s : strategies)
            reg(B(bl, QString("disk-%1").arg(QString(allocStrategyName(s)).toLower()), QString("%1 allocation (sample disk)").arg(allocStrategyName(s)),
                  QString("A new virtual disk using %1 allocation with the sample files: click a block to see its file.").arg(allocStrategyName(s)))
                    .cat(kBasic).mode(allocStrategyName(s)).shared().load([sampleDisk, s] { return sampleDisk(s); }));
        std::vector<AllocPreset> ap = allocPresets();
        for (int i = 0; i < (int)ap.size(); ++i)
            reg(B(bl, QString("compare-%1").arg(i), q(ap[i].name), q(ap[i].description) + " (comparison on separate disks; the shared disk "
                  "is unchanged)").cat(i == 0 ? "Fragmentation and limits" : i == 3 ? "Fragmentation and limits" : kAdv)
                    .mode("Contiguous / Linked / Indexed").params(QString("%1 blocks x %2 B").arg(ap[i].blockCount).arg(ap[i].blockSize))
                    .load([this, i] { return storage_->blocks()->compare(i); }));
        const QString rc = "storage.recovery";
        std::vector<RecoveryPreset> rp = recoveryPresets();
        for (int i = 0; i < (int)rp.size(); ++i)
            reg(B(rc, QString("preset-%1").arg(i), q(rp[i].name), q(rp[i].description)).cat(i == 3 ? kAdv : i == 4 ? kSpecial : kBasic)
                    .mode(i == 3 ? "Signature carving" : "Metadata recovery").shared()
                    .load([this, i] { return storage_->recovery()->loadPreset(i); }));
    }

    // ---------------------------------------------------------------- guard and feedback
    samples_->setGuard([this](const SampleDef& d) { return sampleGuard(d); });
    connect(samples_, &SampleRegistry::loaded, this, [this](const SampleDef& d) {
        QString where = modules_ ? modules_->name(d.module) : d.module;
        appendLog(QString("Sample loaded: %1 > %2 (%3)").arg(where, d.name, d.mode.isEmpty() ? QString("built-in") : d.mode));
        statusBar()->showMessage(QString("Loaded sample \"%1\" - change the inputs if you like, then start the run").arg(d.name), 8000);
    });
}

// Leaf modules (pages without children) in registration order.
QStringList MainWindow::sampleModules() const {
    QStringList leaves;
    for (const QString& id : modules_->ids()) {
        bool parent = false;
        for (const QString& o : modules_->ids()) if (o.startsWith(id + ".")) parent = true;
        if (!parent) leaves << id;
    }
    return leaves;
}

// One Load Sample button at the top of every module's configuration panel.
void MainWindow::installSampleButtons() {
    for (const QString& id : sampleModules()) {
        QWidget* page = modules_->page(id);
        if (!page || samples_->forModule(id).empty()) continue;
        auto* b = new SampleButton(samples_, id);
        b->setDialogTitle(modules_->name(id));
        if (!installInSampleSlot(page, b)) {
            std::fprintf(stderr, "[samples] module %s has no sample slot\n", qPrintable(id));
            delete b;
        }
    }
}

// Before a sample replaces the inputs: confirm if a simulation is running or shared state would be
// replaced (never in the self-test), then stop / pause what runs on that page.
bool MainWindow::sampleGuard(const SampleDef& d) {
    QWidget* page = modules_->page(d.module);
    bool busy = false;
    if (page) for (PlaybackBar* b : page->findChildren<PlaybackBar*>()) busy = busy || b->playing();
    if (d.module == "scheduling.cpu") busy = busy || sched_->animationRunning();
    if (d.module == "synchronization") busy = busy || sync_->animating();
    if (!ui::selfTestMode() && (busy || d.replacesShared)) {
        QString msg = busy ? QString("A simulation is running in \"%1\".\nStop it and load the sample \"%2\"?").arg(modules_->name(d.module), d.name)
                           : QString("The sample \"%1\" replaces the virtual disk shared by all Storage & Files pages "
                                     "(unsaved changes to it are lost; File > save the image first if needed).\nContinue?").arg(d.name);
        if (QMessageBox::question(this, "Load Sample", msg, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes)
            return false;
    }
    if (page) for (PlaybackBar* b : page->findChildren<PlaybackBar*>()) b->pause();
    if (d.module == "scheduling.cpu") sched_->stopAnimation();
    if (d.module == "synchronization") sync_->pause();
    return true;
}

// Makes a hidden module reachable after asking the user (File menu targets only). Returns true
// if the module can be shown now.
bool MainWindow::revealModule(const QString& id, bool ask) {
    QWidget* page = modules_->page(id);
    if (!page || modules_->reachable(page)) return true;
    if (!ask || ui::selfTestMode()) return false;
    if (QMessageBox::question(this, "Hidden module",
            QString("\"%1\" is hidden in this workspace.\nShow it and switch to it?").arg(modules_->name(id)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return false;
    QStringList parts = id.split('.');
    for (int k = 1; k <= parts.size(); ++k) modules_->setVisible(parts.mid(0, k).join('.'), true);
    return modules_->reachable(page);
}

// File > Load Built-in Sample: same registry load, then navigation (with the hidden-module prompt).
bool MainWindow::loadSampleFromMenu(const QString& id) {
    const SampleDef* d = samples_->find(id);
    if (!d) return false;
    QString module = d->module;
    bool canShow = revealModule(module, true);
    bool ok = samples_->load(id);
    if (ok && canShow) showTab(modules_->page(module));
    else if (ok) statusBar()->showMessage(QString("Loaded \"%1\" into \"%2\", which is hidden in this workspace (Modules menu)")
                                              .arg(d->name, modules_->name(module)), 8000);
    return ok;
}

void MainWindow::fillBuiltinSampleMenu(QMenu* menu) {
    menu->clear();
    menu->setToolTipsVisible(true);
    QStringList leaves = sampleModules();
    for (const QString& top : modules_->ids()) {
        if (top.contains('.')) continue;
        QStringList mine;
        for (const QString& l : leaves) if ((l == top || l.startsWith(top + ".")) && !samples_->forModule(l).empty()) mine << l;
        if (mine.isEmpty()) continue;
        QMenu* tm = menu->addMenu(modules_->name(top));
        tm->setToolTipsVisible(true);
        for (const QString& l : mine) {
            QMenu* lm = (mine.size() == 1 && l == top) ? tm : tm->addMenu(modules_->name(l));
            lm->setToolTipsVisible(true);
            for (const QString& c : samples_->categories(l)) {
                lm->addSection(c);
                for (const SampleDef* d : samples_->forModule(l)) {
                    if (d->category != c) continue;
                    QString id = d->id;
                    QAction* a = lm->addAction(d->name, this, [this, id] { loadSampleFromMenu(id); });
                    a->setToolTip(ui::sampleDetails(*d, false));
                    a->setData(id);
                }
            }
        }
    }
}

// File > Open File...: imports the user's own file. Known sample-name prefixes choose the module;
// otherwise the user picks the importer.
bool MainWindow::importFile(const QString& path, const QString& kindIn) {
    QString name = QFileInfo(path).fileName(), kind = kindIn;
    static const QStringList kinds = {"Processes (CSV) -> Scheduling", "Reference string -> Memory", "Banker / detection matrices -> Deadlock",
                                      "Process lifecycle (CSV) -> Processes", "Disk requests -> Disk", "Virtual disk image -> Storage & Files"};
    if (kind.isEmpty()) {
        for (const char* p : {"processes_", "refstring_", "banker_", "deadlock_", "lifecycle_", "disk_", "requests_"})
            if (name.startsWith(p)) return loadSample(path);
        if (name.endsWith(".osvfs")) kind = kinds[5];
        else if (ui::selfTestMode()) return false;
        else {
            bool ok = false;
            kind = QInputDialog::getItem(this, "Open File", "What does " + name + " contain?", kinds, 0, false, &ok);
            if (!ok) return false;
        }
    }
    bool ok = false;
    QWidget* target = nullptr;
    if (kind == kinds[0]) { ok = sched_->loadCsv(path); target = sched_; }
    else if (kind == kinds[1]) { ok = mem_->loadRefFile(path); target = mem_; }
    else if (kind == kinds[2]) { ok = dead_->loadFile(path); target = dead_; }
    else if (kind == kinds[3]) { ok = proc_->loadFile(path); target = proc_; }
    else if (kind == kinds[4]) { ok = disk_->loadFile(path); target = disk_; }
    else if (kind == kinds[5]) {
        QString err;
        ok = storage_->loadImage(path, &err);
        if (!ok) ui::warn(this, "Could not load the virtual disk image: " + err);
        target = storage_->explorer();
    }
    if (ok && target) {
        if (revealModule(modules_->idOfWidget(target), true)) showTab(target);
        statusBar()->showMessage("Imported " + name, 5000);
    }
    return ok;
}

void MainWindow::openFileDialog() {
    QString f = QFileDialog::getOpenFileName(this, "Open File (import your own data)", QDir::homePath(),
                                             "OS-Sim data (*.csv *.txt *.osvfs);;All files (*)");
    if (!f.isEmpty()) importFile(f);
}

QString MainWindow::sampleReport() const {
    return samples_->report([this](const QString& id) {
        QStringList parts = id.split('.');
        QStringList names;
        for (int k = 1; k <= parts.size(); ++k) names << modules_->name(parts.mid(0, k).join('.'));
        return names.join(" > ");
    });
}

// ================================================================ self-test section "samples"

static int g_sfail = 0;
static void sCheck(bool ok, const QString& what) {
    std::printf("[selftest] %s samples: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) ++g_sfail;
}
template <class F> static bool sWait(F cond, int ms) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) { QApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(2); }
    return cond();
}

// Loads every sample (optionally printing one line each); used by --sample-report --load-all.
int MainWindow::loadAllSamples(bool print) {
    int fail = 0;
    for (const SampleDef& d : samples_->all()) {
        bool ok = samples_->load(d.id);
        QApplication::processEvents();
        if (print) std::printf("[samples] %s %s\n", ok ? "OK  " : "FAIL", qPrintable(d.id));
        fail += !ok;
    }
    return fail;
}

// OSSIM_SHOTS=<dir>: screenshots of the sample controls and of LJF / LRTF runs for the docs.
static void sShot(QWidget* w, const QString& name) {
    QString dir = qEnvironmentVariable("OSSIM_SHOTS");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    sWait([] { return false; }, 300);
    w->grab().save(dir + "/" + name + ".png");
}

void MainWindow::sampleScreenshots() {
    if (qEnvironmentVariable("OSSIM_SHOTS").isEmpty()) return;
    resize(1366, 800);
    samples_->load("scheduling.cpu/ljf");
    showTab(sched_);
    sched_->runOnce();
    sShot(this, "samples_cpu_button_ljf");
    samples_->load("scheduling.cpu/lrtf");
    sched_->runOnce();
    sShot(this, "lrtf_gantt");
    samples_->load("scheduling.cpu/ljf-ties");
    sched_->runOnce();
    sShot(this, "ljf_equal_bursts_gantt");
    samples_->load("scheduling.cpu/compare");
    sched_->compareAll();
    sShot(this, "compare_all_eight");
    for (SampleButton* b : sched_->findChildren<SampleButton*>()) {
        b->rebuildMenu();
        b->menu()->popup(b->button()->mapToGlobal(QPoint(0, b->button()->height())));
        sWait([] { return false; }, 200);
        sShot(b->menu(), "samples_menu_cpu");
        b->menu()->hide();
    }
    {
        SampleDialog dlg(samples_, "disk", "Disk", this);
        dlg.select("disk/textbook-scan");
        dlg.show();
        sShot(&dlg, "samples_dialog_disk");
    }
    for (const char* id : {"disk/textbook-look", "memory.heap/preset-6", "storage.explorer/hard-links", "synchronization/algo8-0", "processes.lifecycle/preset-0"}) {
        samples_->load(id);
        showTab(modules_->page(samples_->find(id)->module));
        sShot(this, QString("samples_button_%1").arg(QString(samples_->find(id)->module).replace('.', '_')));
    }
    setDarkTheme(true);
    samples_->load("scheduling.cpu/lrtf-ties");
    showTab(sched_);
    sched_->runOnce();
    sShot(this, "samples_button_dark_lrtf_ties");
    setDarkTheme(false);
}

int MainWindow::runSampleSelfTest() {
    g_sfail = 0;
    QSize old = size();
    sCheck(QFile::exists(":/samples/processes_fcfs.csv") && QFile::exists(":/samples/processes_ljf_lrtf.csv"),
           "sample files are embedded as Qt resources (:/samples)");
    sCheck(QFile::exists(ui::samplesDir() + "/processes_ljf_lrtf.csv"), "samples folder resolved: " + ui::samplesDir());

    // 1. every leaf module has samples and a visible, prominent Load Sample button
    QStringList leaves = sampleModules();
    int buttons = 0;
    for (const QString& id : leaves) {
        QWidget* page = modules_->page(id);
        SampleButton* b = nullptr;
        for (SampleButton* c : page ? page->findChildren<SampleButton*>() : QList<SampleButton*>()) if (c->module() == id) b = c;
        bool has = !samples_->forModule(id).empty();
        sCheck(has && b, QString("%1: %2 sample(s) and a Load Sample button").arg(id).arg(samples_->forModule(id).size()));
        if (!b) continue;
        ++buttons;
        showTab(page);
        QApplication::processEvents();
        QApplication::processEvents();
        bool visible = b->isVisible() && b->button()->isVisible() && b->button()->text() == "Load Sample"
                       && !b->button()->toolTip().isEmpty() && !b->button()->icon().isNull();
        QRect vis = b->button()->visibleRegion().boundingRect();
        sCheck(visible && vis.width() >= b->button()->width() - 1 && vis.height() >= b->button()->height() - 1,
               id + ": button visible, labelled, with icon and tooltip, not clipped");
        b->rebuildMenu();
        QStringList ids = b->menuSampleIds();
        bool relevant = !ids.isEmpty();
        for (const QString& s : ids) relevant = relevant && samples_->find(s) && samples_->find(s)->module == id;
        sCheck(relevant && ids.size() == (int)samples_->forModule(id).size(), id + ": the menu offers only this module's samples");
    }
    sCheck(buttons == leaves.size(), QString("%1 Load Sample buttons for %2 modules").arg(buttons).arg(leaves.size()));

    // 2. every sample loads (validated, populated); exactly one loaded() per load; no auto-run
    int emitted = 0;
    auto conn = connect(samples_, &SampleRegistry::loaded, this, [&emitted] { ++emitted; });
    int before = samples_->loadCount(), fails = 0;
    for (const SampleDef& d : samples_->all()) {
        int e0 = emitted;
        bool ok = samples_->load(d.id);
        QApplication::processEvents();
        if (!ok || emitted != e0 + 1) { ++fails; std::printf("[selftest] FAIL samples: load %s\n", qPrintable(d.id)); }
    }
    sCheck(fails == 0 && samples_->loadCount() - before == (int)samples_->all().size(),
           QString("all %1 samples load, one notification each").arg(samples_->all().size()));
    disconnect(conn);

    // 3. correct algorithm / mode, user may edit, the run works afterwards
    sCheck(samples_->load("scheduling.cpu/lrtf") && !sched_->lastResult(), "LRTF sample loads without running (stale result cleared)");
    sCheck(sched_->runOnce() && sched_->lastResult()->algo == Algo::LRTF && std::fabs(sched_->lastResult()->avgWaiting - 16.5) < 1e-9
           && sched_->lastResult()->contextSwitches == 16, "LRTF sample runs: avg WT 16.5, 16 context switches");
    sCheck(samples_->load("scheduling.cpu/ljf") && sched_->runOnce() && sched_->lastResult()->algo == Algo::LJF
           && std::fabs(sched_->lastResult()->avgWaiting - 10.25) < 1e-9, "LJF sample runs: avg WT 10.25");
    std::vector<PCB> procs;
    sched_->readProcesses(procs);
    int rows = (int)procs.size();
    samples_->load("scheduling.cpu/ljf");
    samples_->load("scheduling.cpu/ljf");
    sched_->readProcesses(procs);
    sCheck((int)procs.size() == rows && rows == 4, "repeated loading does not duplicate processes");
    procs[1].burst = 12;                                      // the user edits a loaded parameter
    sched_->setProcesses(procs);
    sCheck(sched_->runOnce() && sched_->lastResult()->metrics[1].burst == 12, "loaded parameters can be modified and re-run");
    sCheck(samples_->load("scheduling.cpu/compare") && sched_->compareAll() && sched_->lastComparison().size() == 8,
           "comparison sample: Compare all gives 8 algorithms");
    sCheck(samples_->load("memory.replacement/standard-fifo") && mem_->lastResults().empty() && mem_->run()
           && mem_->lastResults().size() == 1 && mem_->lastResults()[0].faults == 15, "page replacement sample: FIFO 15 faults after Run");
    sCheck(samples_->load("memory.replacement/belady-4") && mem_->run() && mem_->lastResults()[0].faults == 10, "Belady sample: 4 frames -> 10 faults");
    sCheck(samples_->load("deadlock/unsafe") && dead_->checkSafety() && dead_->lastSafety() && !dead_->lastSafety()->safe,
           "unsafe-state sample: Check safety -> UNSAFE");
    sCheck(samples_->load("deadlock/rag") && dead_->detect() && dead_->lastDetection() && dead_->lastDetection()->deadlocked
           && dead_->lastDetection()->deadlocked_pids.size() == 2, "RAG sample: P0 and P1 deadlocked");
    sCheck(samples_->load("disk/textbook-sstf") && disk_->result() && disk_->result()->algo == DiskAlgo::SSTF
           && disk_->result()->total == 236, "disk SSTF sample: total head movement 236");
    sCheck(samples_->load(QString("synchronization/algo%1-0").arg((int)SyncAlgo::DINING_PHILOSOPHERS)) && sync_->algorithm() == SyncAlgo::DINING_PHILOSOPHERS, "dining philosophers sample selects the mechanism");
    // Load & Run through the registry (explicit request)
    sCheck(samples_->load("scheduling.cpu/srtf", true) && sched_->lastResult() && std::fabs(sched_->lastResult()->avgWaiting - 6.5) < 1e-9,
           "Load & Run: SRTF avg WT 6.5");
    sCheck(samples_->load("disk/compare", true) && disk_->comparison().size() == 6, "Load & Run: disk comparison");
    int runFail = 0;
    for (const SampleDef& d : samples_->all()) {
        if (!d.run || d.module == "synchronization") continue;
        if (!samples_->load(d.id, true)) { ++runFail; std::printf("[selftest] FAIL samples: load & run %s\n", qPrintable(d.id)); }
        QApplication::processEvents();
        if (QWidget* pg = modules_->page(d.module)) for (PlaybackBar* b : pg->findChildren<PlaybackBar*>()) b->pause();
    }
    int withRun = 0;
    for (const SampleDef& d : samples_->all()) withRun += d.run ? 1 : 0;
    sCheck(runFail == 0 && withRun > 100, QString("every sample with a run action (%1) executes after loading").arg(withRun));
    // guard: a running animation is stopped before the new inputs are loaded
    samples_->load("scheduling.cpu/fcfs-convoy");
    sched_->runAnimated(200);
    sCheck(sched_->animationRunning() && samples_->load("scheduling.cpu/rr") && !sched_->animationRunning(),
           "loading a sample stops the running animated replay");
    disk_->playback()->run();
    sCheck(samples_->load("disk/preset-1") && !disk_->playback()->playing(), "loading a sample pauses a running playback");

    // 4. dialog: only this module's samples, Load && Run availability
    {
        SampleDialog dlg(samples_, "scheduling.cpu", "CPU Scheduling", this);
        sCheck(dlg.sampleCount() == (int)samples_->forModule("scheduling.cpu").size() && dlg.select("scheduling.cpu/lrtf")
               && dlg.selectedId() == "scheduling.cpu/lrtf", "sample dialog lists the module's samples and selects one");
    }

    // 5. File menu: the file list, the built-in catalogue and Open File... are all present
    QMenu* fileMenu = nullptr;
    for (QAction* a : menuBar()->actions()) if (a->text().remove('&') == "File") fileMenu = a->menu();
    QStringList texts;
    if (fileMenu) for (QAction* a : fileMenu->actions()) texts << a->text();
    sCheck(texts.contains("Load sample") && texts.contains("Load Built-in Sample") && texts.contains("Open File...")
           && texts.contains("Export report..."), "File menu: Load sample, Load Built-in Sample, Open File..., Export report...");
    QMenu built;
    fillBuiltinSampleMenu(&built);
    int menuEntries = 0;
    std::function<void(QMenu*)> count = [&](QMenu* m) {
        for (QAction* a : m->actions()) { if (a->menu()) count(a->menu()); else if (!a->data().toString().isEmpty()) ++menuEntries; }
    };
    count(&built);
    sCheck(menuEntries == (int)samples_->all().size(), QString("File > Load Built-in Sample lists all %1 samples").arg(menuEntries));
    sCheck(loadSample(ui::samplesDir() + "/processes_ljf_lrtf.csv") && loadSample(ui::samplesDir() + "/banker_unsafe.txt")
           && loadSample(ui::samplesDir() + "/deadlock_rag_cycle.txt"), "File > Load sample: the new sample files load");
    sCheck(importFile(ui::samplesDir() + "/processes_ljf_ties.csv") && !importFile(ui::samplesDir() + "/virtual_disk_sample.osvfs", "nonsense"),
           "Open File import path (external files) still works");
    sCheck(loadSampleFromMenu("memory.heap/preset-6") && modules_->currentId() == "memory.heap", "a File-menu sample navigates to its module");

    // 6. Module Visibility Manager: hidden modules keep their samples; loading never changes visibility
    modules_->setVisible("memory.heap", false);
    sCheck(!samples_->forModule("memory.heap").empty() && samples_->load("memory.heap/preset-0") && !modules_->isVisible("memory.heap"),
           "hidden module keeps its samples; loading does not change the workspace");
    sCheck(loadSampleFromMenu("memory.heap/preset-1") && !modules_->isVisible("memory.heap"),
           "File-menu sample for a hidden module loads, the workspace is unchanged (no prompt in the self-test)");
    modules_->setVisible("memory.heap", true);
    showTab(modules_->page("memory.heap"));
    QApplication::processEvents();
    SampleButton* hb = nullptr;
    for (SampleButton* c : modules_->page("memory.heap")->findChildren<SampleButton*>()) hb = c;
    sCheck(hb && hb->isVisible() && hb->currentText().contains(samples_->find("memory.heap/preset-1")->name),
           "restored module shows its Load Sample button and the loaded sample name");
    modules_->resetToDefaults();

    // 7. both themes: the accent button stays readable (WCAG AA >= 4.5)
    bool wasDark = ui::darkTheme();
    for (bool dark : {true, false}) {
        setDarkTheme(dark);
        QApplication::processEvents();
        double worst = 21;
        for (SampleButton* b : findChildren<SampleButton*>()) worst = std::min(worst, ui::contrastRatio(b->foreground(), b->background()));
        sCheck(worst >= 4.5 && findChildren<SampleButton*>().size() == buttons,
               QString("%1 theme: Load Sample contrast %2:1").arg(dark ? "dark" : "light").arg(worst, 0, 'f', 2));
    }
    setDarkTheme(wasDark);

    // 8. small windows: the button is never clipped
    for (QSize sz : {QSize(1024, 768), QSize(900, 650)}) {
        resize(sz);
        int clipped = 0;
        for (const QString& id : leaves) {
            QWidget* page = modules_->page(id);
            showTab(page);
            QApplication::processEvents();
            QApplication::processEvents();
            for (SampleButton* b : page->findChildren<SampleButton*>()) {
                QRect vis = b->button()->visibleRegion().boundingRect();
                if (b->button()->width() < b->button()->minimumSizeHint().width() || vis.width() < b->button()->width() - 1) {
                    ++clipped;
                    std::printf("[selftest] samples: clipped at %dx%d in %s (button %dx%d min %d, visible %dx%d)\n", sz.width(), sz.height(),
                                qPrintable(id), b->button()->width(), b->button()->height(), b->button()->minimumSizeHint().width(),
                                vis.width(), vis.height());
                }
            }
        }
        sCheck(clipped == 0, QString("%1x%2: no Load Sample button is clipped").arg(sz.width()).arg(sz.height()));
    }
    resize(old);
    sampleScreenshots();
    resize(old);
    return g_sfail;
}
