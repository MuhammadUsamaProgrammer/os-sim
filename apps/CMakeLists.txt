add_executable(ossim_console console_main.cpp)
target_link_libraries(ossim_console PRIVATE osengine oscore)

# Live Linux demo helper (Phase 2): launched by the GUI with QProcess and by test_native.
add_executable(ossim_helper ossim_helper.cpp)
target_link_libraries(ossim_helper PRIVATE Threads::Threads)
