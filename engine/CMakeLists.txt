# osengine: pthreads + semaphores + mutex + condvar + pipe
add_library(osengine STATIC engine.cpp logger.cpp)
target_include_directories(osengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(osengine PUBLIC oscore Threads::Threads)
