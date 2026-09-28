add_library(wt_test_support STATIC
  support/test.cpp support/scoped.cpp
  support/fixtures/directory.cpp support/fixtures/jobs.cpp
  support/fixtures/models.cpp support/fixtures/transcript.cpp
  support/platform/permissions.cpp support/platform/memory.cpp support/platform/diagnostics.cpp
  support/platform/process.cpp support/platform/socket.cpp)
target_include_directories(wt_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wt_test_support PUBLIC wt_core)
wt_warnings(wt_test_support)
if(WIN32)
  target_link_libraries(wt_test_support PUBLIC psapi ws2_32)
  find_program(WT_TEST_BASH bash PATHS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin" NO_DEFAULT_PATH REQUIRED)
else()
  find_program(WT_TEST_BASH bash REQUIRED)
endif()

function(wt_test_target name source)
  add_executable(wt-${name}-tests ${source})
  target_link_libraries(wt-${name}-tests PRIVATE wt_test_support)
  wt_warnings(wt-${name}-tests)
  wt_manifest(wt-${name}-tests)
endfunction()
function(wt_test name label source)
  wt_test_target(${name} ${source})
  add_test(NAME ${label}/${name} COMMAND wt-${name}-tests)
  set_tests_properties(${label}/${name} PROPERTIES LABELS ${label} TIMEOUT 60)
endfunction()
