# OS-Sim progress

Read this first after a session restart; continue from the first unfinished stage.

| Stage | Title | Status |
|---|---|---|
| 1 | Skeleton | DONE |
| 2 | Scheduling core | todo |
| 3 | Memory core | todo |
| 4 | Deadlock core | todo |
| 5 | Thread engine | todo |
| 6 | GUI: Scheduling tab | todo |
| 7 | GUI: Memory tab | todo |
| 8 | GUI: Deadlock tab + RAG | todo |
| 9 | Integration + export | todo |
| 10 | Polish, docs, final verification | todo |

## Environment
Ubuntu VM, 2 cores, ~6 GB RAM. g++ 15.2, CMake 4.2.3, Qt 6.10.2 (Widgets + Charts found).

## Stage 1: Skeleton
- Built: directory tree, top-level CMake (C++17, -Wall -Wextra, `-DSANITIZE=thread|address`),
  libraries `oscore`, `osengine`, executable `ossim_console`, optional Qt GUI `ossim`, 4 placeholder ctest tests.
- Run: `mkdir -p build && cd build && cmake .. && make -j2 && ctest`
- Known problems: none.

## Not done
(nothing dropped yet)
