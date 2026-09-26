add_executable(wt-tests core.cpp)
target_link_libraries(wt-tests PRIVATE wt_core)
target_compile_options(wt-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME core COMMAND wt-tests)
add_test(NAME cli COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/cli.sh"
  $<TARGET_FILE:whisper-transcribator> "${CMAKE_CURRENT_BINARY_DIR}/cli")
set_tests_properties(core cli PROPERTIES TIMEOUT 60)
