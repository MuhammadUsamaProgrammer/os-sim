# Screenshots for the report

Save each screenshot as a PNG in this folder (`docs/screenshots/`) with the given name.
Start the app with `cd build && ./gui/ossim` and maximise the window.

| # | File name | Steps |
|---|---|---|
| 1 | `01_fcfs_gantt.png` | File > Load sample > processes_fcfs.csv, Algorithm FCFS, **Run**. Shows the Gantt chart P1 0-24, P2 24-27, P3 27-30 and avg WT = 17. |
| 2 | `02_rr_animated.png` | Same sample, Algorithm Round Robin, quantum 4, speed ~300 ms, **Run animated**; take it in the middle of the run (or press **Pause**) with the *Live process states* tab and the event log visible. |
| 3 | `03_compare_all.png` | File > Load sample > processes_srtf.csv, **Compare all**. Table with best values in green + bar chart. |
| 4 | `04_memory_compare.png` | Memory tab, **Standard example** (3 frames, Compare all): FIFO 15 / LRU 12 / Optimal 9 with the red/green frame table. |
| 5 | `05_belady.png` | Memory tab, **Belady example**: 9 faults with 3 frames vs 10 with 4 and the faults-vs-frames chart. |
| 6 | `06_bankers_safe.png` | Deadlock tab, **Load Banker's sample**, **Check safety**: `SAFE: <P1, P3, P4, P0, P2>` with the trace. Optionally also request P1 `1 0 2`. |
| 7 | `07_deadlock_rag.png` | Deadlock tab, **Load deadlock sample**, **Detect deadlock**: P1-P4 red in the tables and in the resource-allocation graph. Then a second shot `07b_recovered.png` after **Recover**. |
| 8 | `08_integrated.png` | File > Load sample > requests_integrated.csv; Integrated run page; untick *Deadlock avoidance*; **Run integrated**: results (P1 killed), then the Deadlock tab showing the P1 -> B -> P2 -> A -> P1 cycle. |
| 9 | `09_sync_race.png` | Synchronization tab, *Race condition*, scenario *Lost update*, press **Step** six times: counter = 1, expected 2, 1 lost update, red violation. |
| 10 | `10_sync_peterson.png` | *Peterson's solution*, *Both want to enter at the same time*, **Step** 9 times: P0 in the critical section, P1 busy-waiting, flags and turn visible. |
| 11 | `11_sync_producer_consumer.png` | *Producer-consumer*, *Fast producer*, **Run** and **Pause** when the producer sits in the queue of `empty` with the buffer full. |
| 12 | `12_sync_comparison.png` | **Comparison** page, **Compare**: table + chart (unsynchronized loses updates, the others do not). |

Shortcut: the self-test can save window images automatically (without window frames):
`cd build && OSSIM_SHOTS=../docs/screenshots QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest`
