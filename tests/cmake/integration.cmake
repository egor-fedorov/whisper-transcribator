wt_test(audio integration integration/audio/audio.cpp)
target_link_libraries(wt-audio-tests PRIVATE wt_engine)
wt_test_target(audio-recovery integration/audio/audio-recovery.cpp)
target_link_libraries(wt-audio-recovery-tests PRIVATE wt_engine)
wt_test(audio-logging integration integration/audio/audio-logging.cpp)
target_link_libraries(wt-audio-logging-tests PRIVATE wt_engine PkgConfig::AV)
wt_test(rendering integration integration/transcript/rendering.cpp)
wt_test(journal integration integration/transcript/journal.cpp)
wt_test(compatibility integration integration/transcript/compatibility.cpp)
target_compile_definitions(wt-compatibility-tests PRIVATE WT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
wt_test(pipeline integration integration/transcript/pipeline.cpp)
wt_test(publication integration integration/transcript/publication.cpp)
wt_test(recovery integration integration/transcript/recovery.cpp)
wt_test(permissionless integration integration/platform/permissionless.cpp)
if(WIN32)
  wt_test(windows integration integration/platform/windows.cpp)
endif()
wt_test(model-concurrency integration integration/models/model-concurrency.cpp)
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
wt_test_target(download integration/models/downloads.cpp)
target_link_libraries(wt-download-tests PRIVATE OpenSSL::SSL Threads::Threads)
if(WIN32)
  find_program(WT_TEST_POWERSHELL pwsh REQUIRED)
  add_test(NAME integration/downloads COMMAND "${WT_TEST_POWERSHELL}" -NoProfile
    -File "${CMAKE_CURRENT_SOURCE_DIR}/integration/models/downloads.ps1"
    -Binary $<TARGET_FILE:wt-download-tests> -OpenSSL "${WT_TEST_OPENSSL}")
else()
  add_test(NAME integration/downloads COMMAND "${WT_TEST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/models/downloads.sh"
    $<TARGET_FILE:wt-download-tests> "${WT_TEST_OPENSSL}")
endif()
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
  add_test(NAME integration/audio-changes COMMAND "${WT_TEST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/audio/audio-changes.sh"
    $<TARGET_FILE:wt-audio-tests> "${WT_TEST_FFMPEG}")
  set_tests_properties(integration/audio-changes PROPERTIES LABELS integration TIMEOUT 60)
  add_test(NAME integration/audio-timeline COMMAND "${WT_TEST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/audio/audio-timeline.sh"
    $<TARGET_FILE:wt-audio-tests> "${WT_TEST_FFMPEG}" $<TARGET_FILE:whisper-transcribator>)
  set_tests_properties(integration/audio-timeline PROPERTIES LABELS integration TIMEOUT 60)
  add_test(NAME integration/audio-recovery COMMAND "${WT_TEST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/audio/audio-recovery.sh"
    $<TARGET_FILE:wt-audio-tests> $<TARGET_FILE:wt-audio-recovery-tests> "${WT_TEST_FFMPEG}")
  set_tests_properties(integration/audio-recovery PROPERTIES LABELS integration TIMEOUT 60)
endif()
add_test(NAME integration/cli COMMAND "${WT_TEST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/app/cli.sh"
  $<TARGET_FILE:whisper-transcribator> "${CMAKE_CURRENT_BINARY_DIR}/cli" "${CMAKE_BINARY_DIR}/generated/package.env" "${WT_WHISPER_REVISION}")
set_tests_properties(integration/cli PROPERTIES LABELS integration TIMEOUT 60)
add_test(NAME integration/build-version COMMAND "${CMAKE_COMMAND}"
  "-DWT_SOURCE=${CMAKE_SOURCE_DIR}" "-DWT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/version-fixture"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/version.cmake")
set_tests_properties(integration/build-version PROPERTIES LABELS integration TIMEOUT 60)
