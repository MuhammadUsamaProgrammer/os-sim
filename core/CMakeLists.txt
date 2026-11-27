# oscore: pure C++17 algorithms (no Qt, no threads)
add_library(oscore STATIC scheduler.cpp memory.cpp deadlock.cpp io_formats.cpp integrated.cpp sync.cpp)
target_include_directories(oscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
