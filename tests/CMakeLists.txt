add_library(wt_test_support STATIC support/test.cpp support/fixtures.cpp)
target_include_directories(wt_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wt_test_support PUBLIC wt_core)
wt_warnings(wt_test_support)

function(wt_test_target name source)
  add_executable(wt-${name}-tests ${source})
  target_link_libraries(wt-${name}-tests PRIVATE wt_test_support)
  wt_warnings(wt-${name}-tests)
endfunction()
function(wt_test name label source)
  wt_test_target(${name} ${source})
  add_test(NAME ${label}/${name} COMMAND wt-${name}-tests)
  set_tests_properties(${label}/${name} PROPERTIES LABELS ${label} TIMEOUT 60)
endfunction()

wt_test(io unit unit/io.cpp)
wt_test(assertions unit unit/assertions.cpp)
wt_test(fd unit unit/fd.cpp)
wt_test(formatting unit unit/rendering.cpp)
wt_test(jobs unit unit/jobs.cpp)
wt_test(model-cache unit unit/model-cache.cpp)
wt_test(boundaries unit unit/boundaries.cpp)
wt_test(cpu-limits unit unit/cpu.cpp)
wt_test(report unit unit/report.cpp)
wt_test(resampler unit unit/resampler.cpp)
target_link_libraries(wt-resampler-tests PRIVATE wt_engine PkgConfig::AV)
wt_test(timeline unit unit/timeline.cpp)
target_link_libraries(wt-timeline-tests PRIVATE wt_engine)
wt_test(decode-errors unit unit/audio-recovery.cpp)
target_link_libraries(wt-decode-errors-tests PRIVATE wt_engine)

wt_test(audio integration integration/audio.cpp)
target_link_libraries(wt-audio-tests PRIVATE wt_engine)
wt_test_target(audio-recovery integration/audio-recovery.cpp)
target_link_libraries(wt-audio-recovery-tests PRIVATE wt_engine)
wt_test(audio-logging integration integration/audio-logging.cpp)
target_link_libraries(wt-audio-logging-tests PRIVATE wt_engine PkgConfig::AV)
wt_test(rendering integration integration/rendering.cpp)
wt_test(journal integration integration/journal.cpp)
wt_test(compatibility integration integration/compatibility.cpp)
target_compile_definitions(wt-compatibility-tests PRIVATE WT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
wt_test(pipeline integration integration/pipeline.cpp)
wt_test(publication integration integration/publication.cpp)
wt_test(recovery integration integration/recovery.cpp)
wt_test(permissionless integration integration/permissionless.cpp)
wt_test(model-concurrency integration integration/model-concurrency.cpp)
set_tests_properties(integration/model-concurrency PROPERTIES TIMEOUT 30)

# The TLS test server needs OpenSSL; macOS provides no headers, but Homebrew's keg-only one works.
if(NOT OPENSSL_ROOT_DIR AND NOT DEFINED ENV{OPENSSL_ROOT_DIR})
  wt_brew_prefix(openssl@3 WT_BREW_OPENSSL)
  if(WT_BREW_OPENSSL)
    set(OPENSSL_ROOT_DIR "${WT_BREW_OPENSSL}")
  endif()
endif()
find_package(OpenSSL REQUIRED COMPONENTS SSL)
find_package(Threads REQUIRED)
# Prefer the command of the OpenSSL found above; macOS provides LibreSSL as openssl.
get_filename_component(WT_OPENSSL_PREFIX "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
find_program(WT_TEST_OPENSSL openssl HINTS "${WT_OPENSSL_PREFIX}/bin" REQUIRED)
wt_test_target(download integration/downloads.cpp)
target_link_libraries(wt-download-tests PRIVATE OpenSSL::SSL Threads::Threads)
add_test(NAME integration/downloads COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/integration/downloads.sh"
  $<TARGET_FILE:wt-download-tests> "${WT_TEST_OPENSSL}")
set_tests_properties(integration/downloads PROPERTIES LABELS integration TIMEOUT 45)

# Homebrew's ffmpeg lacks the libvorbis encoder used for test fixtures; ffmpeg-full has it.
wt_brew_prefix(ffmpeg-full WT_BREW_FFMPEG)
find_program(WT_TEST_FFMPEG ffmpeg HINTS "${WT_BREW_FFMPEG}/bin")
# These tests compare decoding with the ffmpeg command, so it must use the linked decoders'
# version. Archive recipes link their own FFmpeg and skip them, like builds without ffmpeg.
set(WT_TEST_FFMPEG_MATCHES OFF)
if(WT_TEST_FFMPEG)
  execute_process(COMMAND "${WT_TEST_FFMPEG}" -hide_banner -version
    OUTPUT_VARIABLE WT_TEST_FFMPEG_VERSION ERROR_QUIET)
  string(REGEX MATCH "libavcodec +([0-9]+)\\. *([0-9]+)\\. *([0-9]+)" match "${WT_TEST_FFMPEG_VERSION}")
  set(version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
  if(match AND version VERSION_EQUAL AV_libavcodec_VERSION)
    set(WT_TEST_FFMPEG_MATCHES ON)
  else()
    message(STATUS "Skipping FFmpeg decoding comparisons: ${WT_TEST_FFMPEG} uses libavcodec "
      "${version}, the build links ${AV_libavcodec_VERSION}")
  endif()
endif()
if(WT_TEST_FFMPEG_MATCHES)
  add_test(NAME integration/audio-changes COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/integration/audio-changes.sh"
    $<TARGET_FILE:wt-audio-tests> "${WT_TEST_FFMPEG}")
  set_tests_properties(integration/audio-changes PROPERTIES LABELS integration TIMEOUT 60)
  add_test(NAME integration/audio-timeline COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/integration/audio-timeline.sh"
    $<TARGET_FILE:wt-audio-tests> "${WT_TEST_FFMPEG}" $<TARGET_FILE:whisper-transcribator>)
  set_tests_properties(integration/audio-timeline PROPERTIES LABELS integration TIMEOUT 60)
  add_test(NAME integration/audio-recovery COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/integration/audio-recovery.sh"
    $<TARGET_FILE:wt-audio-tests> $<TARGET_FILE:wt-audio-recovery-tests> "${WT_TEST_FFMPEG}")
  set_tests_properties(integration/audio-recovery PROPERTIES LABELS integration TIMEOUT 60)
endif()
add_test(NAME integration/cli COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/integration/cli.sh"
  $<TARGET_FILE:whisper-transcribator> "${CMAKE_CURRENT_BINARY_DIR}/cli" "${CMAKE_BINARY_DIR}/generated/package.env" "${WT_WHISPER_REVISION}")
set_tests_properties(integration/cli PROPERTIES LABELS integration TIMEOUT 60)
add_test(NAME integration/build-version COMMAND "${CMAKE_COMMAND}"
  "-DWT_SOURCE=${CMAKE_SOURCE_DIR}" "-DWT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/version-fixture"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/version.cmake")
set_tests_properties(integration/build-version PROPERTIES LABELS integration TIMEOUT 60)

wt_test(planning resource resource/planning.cpp)
if(NOT WT_SANITIZERS)
  wt_test(memory resource resource/memory.cpp)
  target_link_libraries(wt-memory-tests PRIVATE wt_engine)
  set_tests_properties(resource/memory PROPERTIES TIMEOUT 180)
endif()

add_executable(wt-audio-fixture support/audio-fixture.cpp)
target_include_directories(wt-audio-fixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wt-audio-fixture PRIVATE wt_engine)
wt_warnings(wt-audio-fixture)

# Compile each project header alone, without backend or CLI include directories.
set(WT_CHECK_HEADERS
  app/cli.hpp
  app/commands.hpp
  audio/audio.hpp
  audio/resampler.hpp
  audio/recovery.hpp
  audio/timeline.hpp
  inference/runtime.hpp
  inference/whisper.hpp
  models/download.hpp
  models/models.hpp
  support/cancel.hpp
  support/cpu.hpp
  support/error.hpp
  support/fd.hpp
  support/fs.hpp
  support/hash.hpp
  support/io.hpp
  support/json.hpp
  support/options.hpp
  support/report.hpp
  transcript/jobs.hpp
  transcript/journal.hpp
  transcript/metadata.hpp
  transcript/outputs.hpp
  transcript/pipeline.hpp
  transcript/types.hpp
)
set(WT_HEADER_SOURCES)
foreach(header IN LISTS WT_CHECK_HEADERS)
  string(REPLACE "/" "_" name "${header}")
  set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/${name}.cpp")
  file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
  list(APPEND WT_HEADER_SOURCES "${source}")
endforeach()
add_library(wt-header-checks OBJECT ${WT_HEADER_SOURCES})
target_link_libraries(wt-header-checks PRIVATE wt_core)
wt_warnings(wt-header-checks)
