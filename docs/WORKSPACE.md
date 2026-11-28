# Module Visibility Manager and workspace customisation

Specification: the "Module Visibility Manager & Workspace Customization" request (after Phase 3).
This is a GUI and workspace feature. It controls which modules appear in the navigation. It does not change any
simulation algorithm, and hidden modules are not disabled.

## 1. Using it

The menu bar is **File | View | Help | Modules**. The Modules menu contains:

| Item | What it does |
|---|---|
| Module tree (search box, Expand All, Collapse All) | Tick or untick any main module or child view. The menu stays open, so you can change several boxes in a row. Parents show a counter (e.g. `7/8`) and are drawn in *italic* when only some children are shown. Children of a hidden parent keep their check mark but are greyed out until the parent is shown again. The line under the tree counts visible views and main modules. |
| Show All Modules | Shows every module and child view. |
| Show Current Module Only | Keeps the selected main module and hides the other main modules. Child choices are kept. |
| Reset to Default | Restores the documented defaults (every module visible). |
| Workspace Presets | Built-in presets, your custom presets, and a link to save, rename or delete presets. |
| Manage Workspace... (Ctrl+Shift+M) | Opens a non-modal dialog with a searchable tree, counters, quick actions and the preset list (Apply, Save Current As..., Rename..., Delete). Changes apply immediately; the dialog has no Apply/Cancel step. |

The Modules menu itself can never be hidden.

## 2. Registered hierarchy (28 entries)

Stable identifiers are registered in `MainWindow::registerModules()`. Display names are the tab texts.

```
[x] Scheduling                       scheduling
    [x] CPU Scheduling               scheduling.cpu
    [x] Multilevel Queues            scheduling.mlq
    [x] Real-Time Scheduling         scheduling.realtime
[x] Processes                        processes
    [x] Process Lifecycle            processes.lifecycle
    [x] System Monitor               processes.monitor
    [x] Job Control                  processes.jobs
[x] Memory                           memory
    [x] Page replacement             memory.replacement
    [x] Allocation & Fragmentation   memory.allocation
    [x] Paging & Address Translation memory.paging
    [x] Segmentation                 memory.segmentation
    [x] Demand Paging                memory.demand
    [x] Swapping                     memory.swapping
    [x] Thrashing                    memory.thrashing
    [x] Heap Laboratory              memory.heap
[x] Deadlock                         deadlock
[x] Synchronization                  synchronization
[x] Threads                          threads
[x] IPC                              ipc
[x] I/O                              io
[x] Disk                             disk
[x] Storage & Files                  storage
    [x] File Explorer                storage.explorer
    [x] Access Control               storage.access
    [x] Block Allocation             storage.blocks
    [x] Recovery Lab                 storage.recovery
```

What is registered and what is not:
- Registered: every tab of the main tab bar, and every tab of the four sub-tab areas.
- Not registered: the small tab bars inside a page, such as *Event log*, *Comparison* or *Live Linux demo*. They are
  parts of one module's page, not separate places to navigate to.
- The registry supports any depth (a child can have children). Adding a module takes one `ModuleManager::add` line.

Default visibility is "visible" for all 28 entries.

## 3. Architecture

| Layer | File | Role |
|---|---|---|
| Model (pure C++17, no Qt) | `core/workspace.h/.cpp` | `Workspace` registry, user preference vs effective visibility, the rules, bulk actions, built-in presets, the `id=1,id=0` text form. Tested by `tests/test_workspace.cpp`. |
| GUI adapter | `gui/ModuleManager.h/.cpp` | Binds each id to its `QTabWidget` and page widget. Applies visibility with `QTabWidget::setTabVisible`, moves the selection, pauses playback, manages custom presets, and saves and loads `QSettings`. |
| Widgets | `gui/WorkspaceUi.h/.cpp` | `ModuleTree`, used both inside the menu (through a `QWidgetAction`) and in the dialog, and `WorkspaceDialog`. |
| Integration | `gui/MainWindow.cpp`, `gui/main.cpp` | Registry, Modules menu, a guard in `showTab()`, layout-report skips hidden tabs, `--workspace-set` and `--workspace-report`. |

Design rules:
- **Pages are never destroyed or re-parented.** The manager calls `setTabVisible` on the page's current index, which
  it looks up by widget pointer and never stores. So hiding and showing keeps every input, result, log and playback
  position, and never duplicates a widget or a signal connection. The self-test toggles modules 40 times and checks
  that each action still writes exactly one log line.
- **Preference vs effective visibility.** Hiding a parent does not change the stored choices of its children. For
  example, Memory with only 5 of 8 children shown comes back with the same 5.
- **The workspace can never be empty.** The last visible main module cannot be hidden. A visible parent cannot lose its
  last visible child; hide the parent itself instead. In the tree, a refused click is undone and a one-line reason
  appears under it. Settings or presets that break a rule are repaired by `Workspace::sanitize()`.
- **Selection.** If the selected tab is hidden, the nearest visible sibling is selected first (in original order,
  trying the right side before the left). The selection never lands on a hidden tab, and Ctrl+Tab skips hidden tabs.
- **Menu actions with a hidden destination** (for example *File > Load sample* for the Deadlock tab while it is
  hidden) still do their work. The workspace is not changed, and the status bar explains that the target is hidden.

## 4. Running simulations (lifecycle policy)

When a module becomes effectively hidden (its own box, a parent's box, Show Current Module Only, or a preset):

| Activity | Behaviour |
|---|---|
| Playback bars (Phase 2/3 modules, Heap Lab, Storage, RT, MLQ, Job Control, ...) | `PlaybackBar::pause()`: paused at the current step, never reset. When shown again it is still paused; the user resumes. |
| Scheduling > CPU Scheduling engine replay (threads) | `SchedulingTab::pauseAnimation()`: the engine threads wait on the pause condition variable, and the button shows *Resume*. |
| Synchronization animation | `SyncTab::pause()`. |
| System Monitor sampling | Stops through the page's own `hideEvent` and restarts when the page is shown (live data, not a simulation). |
| Live Linux demos (owned helper processes) | Keep their existing bounded lifecycle. A visibility change never kills or signals any process. |

Nothing is resumed or restarted automatically. Completed results, event logs, parameters and playback history are
kept.

## 5. Presets

Built-in presets (`builtinPresets()` in `core/workspace.cpp`; preset members are given by module id):

| Preset | Shows |
|---|---|
| Complete OS-Sim | everything |
| CPU & Process Management | Scheduling, Processes, Threads |
| Memory & Storage | Memory, Disk, Storage & Files |
| Synchronization & Deadlocks | Synchronization, Deadlock, Threads, IPC |
| Linux Practical Lab | Processes (lifecycle, System Monitor, Job Control), Threads, IPC, Synchronization, Storage & Files > File Explorer: the pages that have Live Linux demos |

How a preset is read:
- Listing a parent shows all of its children. Listing a child shows that child and its parents, but not its
  siblings.
- Custom presets store the full selection by id.
- Applying any preset changes visibility only. Simulation data is not reset.

## 6. Settings

The workspace is stored in QSettings (`OS-Sim/OS-Sim`), in the same file as the theme and panel sizes. Only keys
under `modules/` are written:

```
modules/version = 1
modules/v1/visible/<id> = true|false
modules/v1/current = <id>                      last selected module, restored at start if still visible
modules/v1/presets/<n>/name, .../state = "memory.heap=0,ipc=0,..."
```

When stored values are not usable:

| Situation | Result |
|---|---|
| Missing key (for example a module added in a later version) | The module gets its documented default. |
| Unknown id (a removed or renamed module) | Ignored. |
| A value that is not a boolean | Replaced by the default; a line is written to the event log as `[workspace] ...`. |
| Unsupported `modules/version` | The defaults are used. |
| A file that is not a settings file | The defaults are used. |
| A state that would hide everything | Repaired by `sanitize()`. |

The theme setting is independent of the workspace. The self-test and `--layout-report` never read or write the
user's workspace.

Restart demonstration (this is also the ctest `gui_workspace_restart`, run with a private `XDG_CONFIG_HOME`):

```bash
QT_QPA_PLATFORM=offscreen ./gui/ossim --workspace-set "memory.heap=0,processes=0,processes.monitor=0"
QT_QPA_PLATFORM=offscreen ./gui/ossim --workspace-report     # new process: prints the saved tree
```

## 7. Tests

- `tests/test_workspace.cpp` (ctest `test_workspace`, 70 checks) covers:
  - the registry and id validation, preference vs effective visibility at three levels;
  - the never-empty rules, Show All, Show Current Only, Reset and apply;
  - sanitizing, the tolerant text parser with corrupted input, and every built-in preset.
- `tests/workspace_restart.cmake` (ctest `gui_workspace_restart`) runs the real GUI binary in several processes:
  - save, restart and read back; other settings in the same file are kept;
  - corrupted values and removed ids, every main module hidden, an unsupported version, and a non-INI file.
- The GUI `--selftest` "modules" section has 88 checks on the real tab widgets:
  - every entry is hidden and restored, and the navigation order is unchanged;
  - child preferences survive a hidden parent, and the 5/8 counter and italic partial state are shown;
  - Disk, Heap, RT and Sync playback is paused (position kept, not resumed); the engine replay is paused and stopped
    cleanly; the System Monitor stops sampling;
  - selection moves to the nearest visible tab, Ctrl+Tab skips hidden tabs, and Load sample into a hidden module
    behaves as described;
  - the last module and the last child cannot be hidden;
  - presets work, including during playback; custom presets can be saved, renamed, loaded and deleted;
  - persistence round trip, and corrupted, outdated or empty settings;
  - 40 toggles create no duplicate tabs, pages or connections;
  - the dialog search, immediate apply, Expand / Collapse, and that the menu fits a 768-pixel screen;
  - dark theme, and the layout report with hidden modules and after Reset (0 problems).

Screenshots: `docs/screenshots/workspace/`.

## 8. Limitations

- Visibility works per navigation entry. The small tab bars inside a page (log, comparison, live demo) are not
  separately hideable.
- Keyboard use inside the open menu depends on how Qt routes keys to a `QWidgetAction`. The fully
  keyboard-accessible route is the Manage Workspace dialog (Ctrl+Shift+M: Tab, arrow keys, Space, typing in the
  search box).
- Windows/macOS: not verified. The code uses only portable Qt APIs.
