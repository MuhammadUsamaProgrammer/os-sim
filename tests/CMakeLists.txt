# One executable per module; each returns non-zero on failure.
foreach(t test_scheduler test_memory test_deadlock test_engine test_integrated test_sync test_sync_engine test_disk test_procmgmt test_io test_memadv)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE osengine oscore)
  target_compile_definitions(${t} PRIVATE OSSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Live Linux demos through the helper executable (Phase 2).
add_executable(test_native test_native.cpp)
target_compile_definitions(test_native PRIVATE OSSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}" OSSIM_HELPER="$<TARGET_FILE:ossim_helper>")
add_dependencies(test_native ossim_helper)
add_test(NAME test_native COMMAND test_native WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
