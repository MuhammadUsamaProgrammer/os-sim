# Qt GUI is optional: core/engine/tests still build without Qt.
# Charts are drawn with our own QPainter widget (BarChartWidget), so only Widgets is needed.
find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND AND Qt6Widgets_FOUND)
  set(CMAKE_AUTOMOC ON)
  add_executable(ossim
    main.cpp Style.h
    MainWindow.h MainWindow.cpp
    SchedulingTab.h SchedulingTab.cpp
    MemoryTab.h MemoryTab.cpp
    DeadlockTab.h DeadlockTab.cpp
    GanttWidget.h GanttWidget.cpp
    RagView.h RagView.cpp
    BarChartWidget.h BarChartWidget.cpp
    EngineBridge.h EngineBridge.cpp)
  target_link_libraries(ossim PRIVATE osengine oscore Qt6::Widgets)
  target_compile_definitions(ossim PRIVATE OSSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
else()
  message(WARNING "Qt6 Widgets not found: GUI (ossim) will not be built. "
                  "Install with: sudo apt install qt6-base-dev")
endif()
