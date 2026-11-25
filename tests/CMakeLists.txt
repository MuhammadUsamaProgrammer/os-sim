# One executable per module; each returns non-zero on failure.
foreach(t test_scheduler test_memory test_deadlock test_engine test_integrated)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE osengine oscore)
  target_compile_definitions(${t} PRIVATE OSSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
