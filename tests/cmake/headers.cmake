# Compile each project header alone, without backend or CLI include directories.
set(WT_CHECK_HEADERS
  app/cli.hpp
  app/commands.hpp
  app/options.hpp
  app/configuration.hpp
  audio/reader.hpp
  audio/runtime.hpp
  audio/detail/error.hpp
  audio/detail/logging.hpp
  audio/options.hpp
  audio/resampler.hpp
  audio/recovery.hpp
  audio/timeline.hpp
  inference/runtime.hpp
  inference/whisper.hpp
  inference/options.hpp
  models/download.hpp
  models/models.hpp
  models/types.hpp
  models/options.hpp
  platform/cpu.hpp
  platform/file.hpp
  platform/sha256.hpp
  platform/system.hpp
  support/cancel.hpp
  support/error.hpp
  support/fs.hpp
  support/hash.hpp
  support/files.hpp
  support/atomic.hpp
  support/permissions.hpp
  support/strings.hpp
  support/json.hpp
  support/report.hpp
  transcript/jobs.hpp
  transcript/checkpoint/journal.hpp
  transcript/checkpoint/detail/privacy.hpp
  transcript/checkpoint/detail/storage.hpp
  transcript/checkpoint/detail/records.hpp
  transcript/checkpoint/detail/recovery.hpp
  transcript/checkpoint/detail/publication.hpp
  transcript/metadata.hpp
  transcript/render/render.hpp
  transcript/render/detail/formatters.hpp
  transcript/publication.hpp
  transcript/options.hpp
  transcript/pipeline.hpp
  transcript/boundaries.hpp
  transcript/types.hpp
)
if(WIN32)
  list(APPEND WT_CHECK_HEADERS platform/win32/handles.hpp platform/win32/security.hpp platform/win32/status.hpp)
else()
  list(APPEND WT_CHECK_HEADERS platform/posix.hpp)
endif()
set(WT_HEADER_SOURCES)
foreach(header IN LISTS WT_CHECK_HEADERS)
  string(REPLACE "/" "_" name "${header}")
  set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/${name}.cpp")
  file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
  list(APPEND WT_HEADER_SOURCES "${source}")
endforeach()

# Test APIs must also be independently includable, without a fixture umbrella.
set(WT_TEST_CHECK_HEADERS
  support/test.hpp
  support/scoped.hpp
  support/fixtures/directory.hpp
  support/fixtures/jobs.hpp
  support/fixtures/models.hpp
  support/fixtures/transcript.hpp
  support/fixtures/wav.hpp
  support/platform/permissions.hpp
  support/platform/memory.hpp
  support/platform/diagnostics.hpp
  support/platform/process.hpp
  support/platform/socket.hpp
)
foreach(header IN LISTS WT_TEST_CHECK_HEADERS)
  string(REPLACE "/" "_" name "${header}")
  set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/test_${name}.cpp")
  file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
  list(APPEND WT_HEADER_SOURCES "${source}")
endforeach()
add_library(wt-header-checks OBJECT ${WT_HEADER_SOURCES})
target_link_libraries(wt-header-checks PRIVATE wt_core)
target_include_directories(wt-header-checks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
wt_warnings(wt-header-checks)
