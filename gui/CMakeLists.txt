# Qt GUI is optional: core/engine/tests still build without Qt.
# Charts are drawn with our own QPainter widget (BarChartWidget), so only Widgets is needed.
find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND AND Qt6Widgets_FOUND)
  set(CMAKE_AUTOMOC ON)
  add_executable(ossim
    main.cpp Style.h Theme.h
    MainWindow.h MainWindow.cpp
    SchedulingTab.h SchedulingTab.cpp
    MemoryTab.h MemoryTab.cpp
    DeadlockTab.h DeadlockTab.cpp
    GanttWidget.h GanttWidget.cpp
    RagView.h RagView.cpp
    BarChartWidget.h BarChartWidget.cpp
    EngineBridge.h EngineBridge.cpp
    SyncTab.h SyncTab.cpp
    SyncViews.h SyncViews.cpp
    SyncBridge.h SyncBridge.cpp
    ModuleKit.h ModuleKit.cpp
    DiskTab.h DiskTab.cpp DiskViews.h DiskViews.cpp
    NativeRunner.h NativeRunner.cpp
    ProcessTab.h ProcessTab.cpp LifecycleViews.h LifecycleViews.cpp
    IoTab.h IoTab.cpp IoViews.h IoViews.cpp
    MemAdvTab.h MemAdvTab.cpp MemAdvViews.h MemAdvViews.cpp
    IpcTab.h IpcTab.cpp
    ThreadsTab.h ThreadsTab.cpp
    LayoutAudit.h LayoutAudit.cpp
    PanelLayout.h PanelLayout.cpp
    StorageTab.h StorageTab.cpp StorageViews.h StorageViews.cpp
    HeapLab.h HeapLab.cpp
    SysMonTab.h SysMonTab.cpp
    JobControlTab.h JobControlTab.cpp
    RtSchedTab.h RtSchedTab.cpp)
  target_link_libraries(ossim PRIVATE osengine oscore Qt6::Widgets)
  target_compile_definitions(ossim PRIVATE OSSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                             OSSIM_HELPER="$<TARGET_FILE:ossim_helper>")
  add_dependencies(ossim ossim_helper)
else()
  message(WARNING "Qt6 Widgets not found: GUI (ossim) will not be built. "
                  "Install with: sudo apt install qt6-base-dev")
endif()
