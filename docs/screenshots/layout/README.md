# Layout fix screenshots (generated, real renders)

- `before_1366x768_Processes.png`, `before_1366x768_IPC.png`: the old layout. The configuration panel is capped
  at 340 px with no horizontal scroll bar, so its fields, table and buttons are cut off. The originals reported by
  the user were attached to the issue report (they are not stored in the repository).
- `layout_1366x768_<module>.png`, `layout_1920x1080_<module>.png`: every module and Memory sub-tab after the fix.

Regenerate them with:

```bash
cd build && OSSIM_SHOTS=../docs/screenshots/layout QT_QPA_PLATFORM=offscreen ./gui/ossim --layout-report 1366x768,1920x1080
```

The screenshots are offscreen renders (Qt "offscreen" platform), not photographs of a physical monitor.
