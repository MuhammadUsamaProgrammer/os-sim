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
    RtSchedTab.h RtSchedTab.cpp
    MlqTab.h MlqTab.cpp
    ModuleManager.h ModuleManager.cpp
    WorkspaceUi.h WorkspaceUi.cpp
    SampleRegistry.h SampleRegistry.cpp SampleCatalog.cpp)
  # Built-in samples are embedded (:/samples/...) so they work from any working directory.
  file(GLOB OSSIM_SAMPLE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/samples/*)
  qt_add_resources(ossim "samples" PREFIX "/" BASE ${CMAKE_SOURCE_DIR} FILES ${OSSIM_SAMPLE_FILES})
  target_link_libraries(ossim PRIVATE osengine oscore Qt6::Widgets)
  target_compile_definitions(ossim PRIVATE OSSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                             OSSIM_HELPER="$<TARGET_FILE:ossim_helper>")
  add_dependencies(ossim ossim_helper)
  # Modules menu: workspace persistence across real restarts (private XDG_CONFIG_HOME)
  add_test(NAME gui_workspace_restart
           COMMAND ${CMAKE_COMMAND} -DOSSIM=$<TARGET_FILE:ossim> -DCFG=${CMAKE_BINARY_DIR}/workspace-test-config
                   -P ${CMAKE_SOURCE_DIR}/tests/workspace_restart.cmake)
  # Built-in samples from another working directory, using only the embedded copy.
  add_test(NAME gui_samples_anywhere COMMAND ossim --sample-report --load-all WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/gui)
  set_tests_properties(gui_samples_anywhere PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OSSIM_FORCE_EMBEDDED_SAMPLES=1"
                       PASS_REGULAR_EXPRESSION "embedded copy" FAIL_REGULAR_EXPRESSION "FAIL")
else()
  message(WARNING "Qt6 Widgets not found: GUI (ossim) will not be built. "
                  "Install with: sudo apt install qt6-base-dev")
endif()
