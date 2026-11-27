# OS-Sim Phase 3: Integrated OS Subsystems and Live System Tools

Specification: `OS_SIM_PHASE_3_INTEGRATED_MODULES.md` (project root). This document describes what was actually
built, the models and their assumptions, the data sources, what is simulated and what is live, platform status,
reproducible examples and a viva demonstration path.

## 1. Navigation architecture (final)

All nine original parent tabs keep their names and order. **Storage & Files** is the only new parent tab. Three
parents became sub-tab areas (the same pattern the Memory tab already used in Phase 2); the original page is always
the first sub-tab and is unchanged.

| Parent tab | Child views (new in Phase 3 in bold) |
|---|---|
| Scheduling | CPU Scheduling (original), **Multilevel Queues**, **Real-Time Scheduling** |
| Processes | Process Lifecycle (original Phase 2 page), **System Monitor**, **Job Control** |
| Memory | Page replacement, Allocation & Fragmentation, Paging & Address Translation, Segmentation, Demand Paging, Swapping, Thrashing, **Heap Laboratory** |
| Deadlock | unchanged (RAG, Banker's safety / request, detection, recovery already present) |
| Synchronization | unchanged page, algorithm list extended with **Readers-writers** and **Dining philosophers** |
| Threads, IPC, I/O, Disk | unchanged |
| **Storage & Files** (new) | **File Explorer**, **Access Control**, **Block Allocation**, **Recovery Lab** |

Every new view is built with the shared responsive layout (`assembleModule` -> `ConfigPanel` / `PanelSplitter`,
`PageScroll`), so the Phase 2 width fix applies unchanged: the layout report and the splitter-drag self-tests cover
every new page.

Badges: **Simulation** (deterministic model), **Live System** (System Monitor: real data of this computer),
**Live Linux Demo** pages/tabs (real system calls in the helper program on its own objects only).

## 2. Module overview

| View | Core model (pure C++17) | GUI | Live part | Tests |
|---|---|---|---|---|
| File Explorer, Access Control, Block Allocation, Recovery Lab | `core/vfs.*` (one `VirtualFs`) | `gui/StorageTab.*`, `gui/StorageViews.*` | `ossim_helper fsdemo` (private temp dir) | `test_vfs` (214), `test_native` |
| Heap Laboratory | `core/heap.*` | `gui/HeapLab.*` | - | `test_heap` (107) |
| System Monitor | `core/sysmon.*` (parsers, deltas, alerts, history) | `gui/SysMonTab.*` | `engine/sysmon_native.*` (Linux /proc, Windows API), `ossim_helper worker` | `test_sysmon` (59) |
| Job Control | `core/jobctl.*` (reuses `IpcSim` pipe) | `gui/JobControlTab.*` | `ossim_helper jobs` | `test_jobctl` (98), `test_native` |
| Real-Time Scheduling | `core/rtsched.*` | `gui/RtSchedTab.*` | - | `test_rtsched` (79) |
| Multilevel Queues | `core/mlq.*` (reuses scheduler types, `appendTick`, `validateProcesses`) | `gui/MlqTab.*` (reuses `GanttWidget`) | - | `test_mlq` (26) |
| Readers-writers, Dining philosophers | `core/sync.*` (extended `SyncSim`) | existing `SyncTab` / `SyncView` | real semaphore demo of the tab | `test_sync_classic` (473) |

## 3. Storage & Files (one shared virtual filesystem)

`StorageArea` owns ONE `VirtualFs`; all four pages read it and refresh on `changed()`. Operations go through the
model only, so the views cannot disagree (self-test: a file created in the Explorer is selectable in Block Allocation).
The host filesystem is never the simulation medium; virtual paths are never interpreted as host paths. Images are
written only to a file the user picks (`Save image...`), and loaded images are validated completely (version, geometry,
every block / inode / entry index, consistency invariants, directory cycles) before they replace the model.

**Disk layout:** blocks 0-3 are metadata (superblock, free bitmap, inode table x2); data blocks follow. Every block
keeps its bytes and a *generation* number that increases on every allocation.

**Model rules (simplified, documented):**
- inode vs directory entry: a name is an entry in a directory inode; regular files may have several names (hard
  links, not for directories); symbolic links store a path, resolution follows at most 8 links (ELOOP), relative
  targets are resolved from the link's directory, dangling links are allowed.
- permissions: 9 rwx bits, exactly ONE class applies (owner, else group, else other - never OR-ed); every traversed
  directory needs x; listing needs r; create / delete / rename need w+x on the parent directory (the file's own bits do
  not matter for deletion); chmod by owner or root, chown by root only, chgrp by the owner to one of their groups.
  uid 0 is a simplified privileged user (bypasses r/w and directory search; executing a file still needs one x bit).
  Not modelled: ACLs, sticky bit, setuid/setgid, umask. Not POSIX compliant.
- descriptors: per-process fd tables -> shared open-file table entries (offset, access mode, refcount); `dup` shares
  the offset; `close` vs `unlink`: the inode is released only when link count AND open references are 0.
- allocation strategy is fixed per disk (changing it needs a new disk; files are never reinterpreted):
  contiguous (first fit, extend in place or relocate, external fragmentation), linked (4-byte next pointer inside each
  block, logical block k needs k+1 reads), indexed (one index block of blockSize/4 pointers -> maximum file size, two
  reads). Every allocation is all-or-nothing: ENOSPC / EFBIG roll back completely.
- recovery: a deleted regular file leaves a *deleted record* (last name, size, block list with generations, checksum).
  Metadata recovery: a block is intact only if its generation is unchanged since the deletion (a reused-and-freed
  block is NOT intact). Classes: fully recoverable (every block intact AND checksum verified), partially recoverable,
  unrecoverable, unknown (metadata lost / evidence insufficient). Signature carving scans free blocks for the synthetic
  `OSF1` + length + payload + `END!` format and assumes contiguity; a complete signature without metadata is classified
  *unknown*, never "verified". Restore writes a NEW file, never overwrites, and avoids every block that still holds
  intact evidence; with too little space it fails (ENOSPC) and only the preview is shown. The model deliberately keeps
  freed bytes; real filesystems / SSDs (TRIM) may not.

**Reproducible examples:** Block Allocation preset 1 (36 x 128 B, contiguous): `create big 1600` fails with "a run of
13 free blocks ... 27 blocks are free in total but the largest run is 12"; linked and indexed succeed. Recovery
presets: intact (full, verified), partially overwritten (first block reused, 86 of 150 bytes), fully overwritten,
metadata lost + carving (unknown), hard link + open descriptor (inode released only at the last close).

**Sample image:** `samples/virtual_disk_sample.osvfs` (sample filesystem, indexed allocation, one deleted file) can be
loaded with *Load image...*; `test_vfs` loads and checks it.

**Live Linux syscalls (Explorer bottom tab):** `ossim_helper fsdemo` creates its own `mkdtemp` directory and
performs real open / write / lseek / read / fstat / link / readdir / unlink-while-open / close / rmdir, then removes
everything (test_native checks that the directory is gone).

## 4. Memory > Heap Laboratory

Application-level allocation, not the OS-level pages of the Memory area. The arena is a bounded
`std::vector<uint8_t>` (128..8192 bytes); every header is stored in it (size, requested, magic, link/handle, 16 bytes
rounded up to the alignment 8/16/32). Users get handles; the "pointer" shown is the arena-relative payload offset.
- Free-list allocator: address-ordered explicit free list, First / Best / Worst Fit (ties: lowest address),
  splitting only if the remainder >= minimum block (header + one alignment unit; smaller remainders become padding),
  immediate coalescing with both neighbours.
- Buddy allocator (separate family, power-of-two arena): split in halves down to the needed order, buddy =
  offset XOR size, merge while the buddy of the same order is free.
- calloc checks `count * size` overflow before allocating and zeroes the payload; realloc shrinks in place, grows in
  place by absorbing a free neighbour, or moves (copy min(old, new) bytes); a failed realloc keeps the old block.
  Conventions: malloc(0) returns a unique minimum block; free(0) does nothing; realloc(0, n) = malloc(n);
  realloc(h, 0) frees and returns 0.
- Errors detected without changing the heap: invalid handle, double free, use after free, out-of-bounds read/write.
  "Overflow (demo)" writes past the request INSIDE the arena vector: inside the padding it stays undetected, into the
  next header the integrity check reports it and the heap refuses further operations until reset. No real invalid
  C++ access happens.
- Metrics: live requested bytes, header bytes (all blocks), padding, free bytes, largest free block, largest possible
  request, failures; fragmentation indicator `1 - largest_free / total_free` (a chosen indicator, "not defined" when
  nothing is free). Outstanding allocations at the end are *potential* leaks, not proof of unreachable memory.
- Sessions are deterministic scripts: Previous = earlier state, a new operation after Previous starts a new branch,
  Undo removes the last operation, changing family / fit / arena / alignment replays from an empty heap.
- Fragmentation preset: 768 B free in three blocks, largest 320 B, a 400-byte request (416 B block) fails.

## 5. Processes > System Monitor (Live System)

Live data of the computer running OS-Sim; no simulated values are mixed in.
- Linux backend (`engine/sysmon_native.cpp`): `/proc/stat` (total + per-core jiffies), `/proc/meminfo`
  (MemTotal, MemAvailable; old kernels: free+buffers+cached), `/proc/diskstats` (whole disks, 512-byte sectors),
  `/proc/<pid>/stat` (name between the first '(' and the LAST ')', state, ppid, utime+stime, threads, start time, rss)
  and `/proc/<pid>/status` (Name, Uid, Threads, VmRSS). Command lines are not read. Processes that vanish are skipped;
  unreadable ones are marked.
- Windows backend: GetSystemTimes (kernel time includes idle), GlobalMemoryStatusEx, Toolhelp32 snapshot,
  GetProcessTimes, GetProcessMemoryInfo (working set); psapi is linked only on WIN32. **It compiles only on Windows and
  has NOT been verified on a Windows machine.** WSL would report the Linux VM, not the Windows host.
- Arithmetic (`core/sysmon.*`, fixture-tested): total CPU % = busy / all jiffies between two samples; process CPU % =
  delta ticks / (delta seconds x ticks per second) = share of ONE logical core (can exceed 100 %; machine share = / CPUs);
  memory used = total - available; counter resets and zero intervals give "unavailable", the first sample "warming up";
  identity = pid + start time (PID reuse is not treated as a delta); alerts need N consecutive samples above the threshold
  and clear below threshold - 5 % (hysteresis); histories are bounded.
- RSS includes shared pages; RSS values cannot be summed to the used memory (stated in the inspector).
- Sampling runs on a worker `QThread`; snapshots arrive as queued immutable values; sampling pauses while the page is
  hidden; Stop clears the history.
- Process control is limited to the page's OWN demo worker (`ossim_helper worker <s> <MiB> <busy%>`, bounded to 120 s
  and 256 MiB): suspend / resume (SIGSTOP / SIGCONT, POSIX only), cooperative stop (SIGTERM), force kill after a
  confirmation. Signals are sent only while QProcess still owns the running child (its PID cannot have been reused).
  Host processes are read-only. The export contains no live process details.

## 6. Processes > Job Control

Deterministic model (`core/jobctl.*`): session 100 (the shell), jobs with job ids (%1, %2) separate from PIDs, one
process group per job (PGID = first PID), exactly one foreground group owning the terminal. Grammar: `compute N`,
`sleep N`, `readline`, `gen N | upper`, `&`, `jobs`, `fg`, `bg`, `kill -TERM|-KILL|-INT|-STOP|-CONT %n`, `wait`,
`ctrl-z`, `ctrl-c`, `type <text>`, `tick [n]`, `until-prompt`, `help`. While a foreground job runs, only the keyboard
signals, input and time commands are accepted (the shell waits). Bare PIDs and anything else are refused; nothing is
executed on the host.
- Semantics: SIGTSTP (Ctrl-Z) / SIGSTOP stop the group, SIGCONT restores the previous state, a background job is
  *running* (not blocked) unless it reads the terminal (SIGTTIN stops it), SIGINT / SIGTERM / SIGKILL terminate,
  `wait` refuses to wait for a stopped job (it would block forever), completion gives the terminal back.
- The pipeline REUSES the IPC pipe model (`IpcSim`, pipe mode, capacity 3): unused ends are closed at creation, the
  writer blocks on a full pipe, the reader on an empty one (upper is slower: one item every 2 ticks), EOF after gen
  closes its write end; killing a stage closes its ends.
- fg / bg / Ctrl-Z are **simulated** (a QProcess child has no controlling terminal). The **Live Linux demo**
  (`ossim_helper jobs`) shows real process groups on helper-owned children: setpgid, kill(-pgid, SIGSTOP / SIGCONT)
  observed with waitpid(WUNTRACED / WCONTINUED), a producer | consumer pipeline in one group, SIGTERM to a group, all
  children reaped; no tcsetpgrp / PTY.
- Required scenario (test + self-test): `compute 8 &`, stop with `kill -STOP %1` (no progress while stopped), `bg`
  (progress resumes in the background), `fg %1`, `until-prompt` -> Done, the shell has the terminal again.

## 7. Scheduling > Real-Time Scheduling

`core/rtsched.*`: preemptive RM and EDF on one CPU, integer time. Tasks (C, T, D, offset) with 0 < C <= D <= T
(D > T rejected). Event order at instant t: completions of [t-1, t) -> deadline checks (a job finishing exactly at its
deadline is on time) -> releases -> dispatch. Ties: RM shorter period, then lower task index, FIFO within a task; EDF
earlier deadline, then earlier release, then lower index. Miss policy: the late job continues, or it is aborted; a miss
is counted once. Horizon H: slots [0, H); deadlines at H are checked; later deadlines are "incomplete", not misses.
Analysis: U = sum C/T; RM bound n(2^(1/n)-1) is sufficient only; EDF U <= 1 is the classical result for implicit
deadlines; with D < T the tests are marked not applicable. Assumptions shown in the GUI: independent periodic
preemptible tasks, one CPU, D = T, zero overhead, no blocking. A clean trace is an observation, not a proof.

**Specification example (tested):** A(C=2, T=D=5), B(C=4, T=D=7), horizon 35, U = 34/35. RM: A 0-2, B 2-5, A preempts
B at 5, B#0 misses its deadline at t=7 (completes late at 8). EDF: B (deadline 7) runs at 5, no miss over 35.
The mission preset (navigation, telemetry, thermal, comms) uses FICTIONAL parameters and is labelled illustrative; it
does not claim to reproduce any real system or another project. Priority inversion is NOT included: the
synchronization model has no priority scheduler that could be reused accurately.

## 8. Confirmed-gap audit (section 9 of the specification)

| Item | Before Phase 3 | Action |
|---|---|---|
| Multilevel Queue / Multilevel Feedback Queue | absent | added (`core/mlq.*`, Scheduling > Multilevel Queues) |
| Readers-writers | absent | added to the existing SyncSim (first problem, readers' preference) |
| Dining philosophers | absent | added to the existing SyncSim (naive deadlock, asymmetric solution) |
| Deadlock: RAG, Banker's safety/request, detection, recovery | present | unchanged, no duplicate sandbox |
| Deadlock prevention | explained only (no simulator) | documented gap; the dining-philosophers asymmetric order demonstrates resource ordering (breaking circular wait) |
| Page replacement, disk scheduling | present | unchanged; their tests and self-test checks still pass |

**MLQ / MLFQ policy:** queue 0 highest, fixed priority between queues, each queue RR (own quantum) or FCFS. MLQ: fixed
queue = the priority column. MLFQ: new processes enter queue 0, a full quantum demotes one level, preemption by a
higher queue keeps the level (tail, fresh quantum), optional priority boost every S ticks. Order per tick: arrivals ->
expiry of the previous runner -> boost -> preemption -> dispatch. Hand-checked: MLQ preset average waiting 3.4;
MLFQ preset P1 demoted twice, finishes at 19.

**Readers-writers:** rw_mutex = 1, mutex = 1, read_count; first reader waits on rw_mutex, last one signals it; many
readers OR one writer; writers can starve (readers' preference). **Dining philosophers:** chopsticks are binary
semaphores; naive left-first order deadlocks in the scripted scenario (all four Coffman conditions, circular wait);
odd philosophers taking the right chopstick first break the circular wait. Violation rules are problem-specific
(writer with anybody; two neighbours eating).

## 9. Verification (Linux, this checkout)

- `make -j2`: 0 warnings (-Wall -Wextra); `ctest`: 21 of 21 test programs pass (7 new: test_vfs, test_heap,
  test_sysmon, test_jobctl, test_rtsched, test_mlq, test_sync_classic; test_native extended). The unchanged test_sync
  (1068 checks) and every Phase 2 test still pass.
- `QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest`: all checks pass; the Phase 3 sections exercise every new child
  view (presets, parameter changes, state / metric assertions, live sampling and owned-worker control, native demos)
  and the splitter-drag layout test covers the 11 new pages.
- `--layout-report`: 0 inaccessible controls on every page (including the new ones) at 1024x768 .. 2560x1440 and at
  125 / 150 / 200 % scaling (logical sizes), light and dark theme (see PROGRESS.md for the exact matrix).
- Windows: **not verified** (no Windows environment). The Linux build does not establish Windows support.

## 10. Viva demonstration path (about 12 minutes)

1. Storage & Files > File Explorer: as alice open `/home/alice/notes.txt`, show inode / links / blocks; switch to bob
   and try again (EACCES on the search permission of `/home/alice`). Descriptors tab: open, read 5, dup, read (shared
   offset); unlink the file, the inode survives until close.
2. Access Control: `/shared/plan.txt` matrix (alice/carol allowed, bob denied); apply "Public readable" to
   `/home/alice/todo.txt` - bob is still denied (directory search permission).
3. Block Allocation: compare preset 1 (contiguous fails with 27 free blocks), click a linked file to see the pointer
   chain and the access traversal.
4. Recovery Lab: presets intact -> restore; partially overwritten (evidence table: generation changed); metadata lost
   -> carving -> unknown.
5. Memory > Heap Laboratory: fit-policy preset, switch First / Best / Worst; fragmentation preset; buddy preset.
6. Processes > System Monitor: live CPU / memory, filter by "ossim", start the demo worker, suspend / resume it.
7. Processes > Job Control: scenario 1, then type `compute 5 &`, `kill -STOP %1`, `fg %1`, `ctrl-z`; pipeline
   scenario; Live Linux demo.
8. Scheduling > Real-Time Scheduling: preset 1 with RM (miss at 7), switch to EDF; mission preset; RM vs EDF tab.
9. Scheduling > Multilevel Queues: MLFQ preset (demotions), boost preset. Synchronization: dining philosophers naive
   (stuck) vs asymmetric.

## 11. Limitations

- Windows System Monitor backend unverified; per-core CPU and disk rates are not collected on Windows.
- Job control fg/bg is modelled; no PTY / controlling terminal is used in the live demo.
- The virtual filesystem is educational: no journaling, no ACLs / special bits, directory entries live in the inode
  table, logical clock, users fixed (root, alice, bob, carol), images do not store processes / open files.
- Carving assumes contiguous files; generation tracking is a teaching device (real disks have no such counter).
- Heap: no thread safety, no real `malloc` replacement; corruption is only modelled.
- Real-time: one CPU, no blocking / resource sharing, no priority inversion demo, constrained deadlines only.
