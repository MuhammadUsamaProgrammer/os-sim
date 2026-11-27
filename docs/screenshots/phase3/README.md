# Phase 3 screenshots

Generated offscreen with `OSSIM_SHOTS=<dir> QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest` (state after the
self-test step named in the file) and `--layout-report 1366x768` (`layout_1366x768_*`, default state of each page).

| File | Shows |
|---|---|
| `storage_explorer.png` | File Explorer after the descriptor / unlink checks |
| `storage_access.png` | Access Control: effective-access matrix, rwx bits, octal conversion |
| `storage_blocks.png` | Block Allocation: shared disk grid, mapping, comparison of the three strategies |
| `storage_recovery.png` | Recovery Lab: partially overwritten file, evidence table, preview |
| `heap_realloc.png`, `heap_buddy.png` | Heap Laboratory: realloc move arrow; buddy tree |
| `sysmon.png` | System Monitor (live), filtered to OS-Sim's own processes (no other host process names are stored) |
| `job_control_pipeline.png` | Job Control: stopped pipeline, pipe buffer, job list, process tree |
| `realtime_rm.png` | Real-Time Scheduling: RM trace of A(2,5) B(4,7) at t = 8 (miss at 7) |
| `mlfq.png` | Multilevel Queues: MLFQ demotions and queue levels |
| `sync_readers_writers.png`, `sync_philosophers_deadlock.png` | the two classical problems in the Synchronization tab |
| `layout_1366x768_*.png` | every new page at 1366 x 768 (System Monitor omitted: it would show host process names) |
