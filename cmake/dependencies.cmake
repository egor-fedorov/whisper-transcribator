include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
set(GGML_CUDA ${WT_CUDA} CACHE BOOL "" FORCE)
set(GGML_VULKAN ${WT_VULKAN} CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "Optimize for the build host CPU")
if(GGML_BACKEND_DL AND GGML_NATIVE)
  message(FATAL_ERROR "Choose GGML_NATIVE or dynamic portable backends, not both")
endif()
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(WT_WHISPER_REVISION 927cfce34f31707e17f2bff35c349632fb9e2c3a)
set(WT_CLI11_VERSION 2.5.0)
set(WT_JSON_VERSION 3.11.2)
# Include patch contents in FetchContent's stamp, including when reusing a build directory.
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/patch-whisper.cmake" WT_WHISPER_PATCH_SHA256)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/patch-whisper.cmake")
FetchContent_Declare(whisper
  URL https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/${WT_WHISPER_REVISION}
  URL_HASH SHA256=41b664fee09e79176ac277b5237debec34f8d74af3c7d71f333f1ec67989ecde
  PATCH_COMMAND "${CMAKE_COMMAND}" -DWT_WHISPER_SOURCE=<SOURCE_DIR>
    -DWT_PATCH_SHA256=${WT_WHISPER_PATCH_SHA256}
    -P "${CMAKE_CURRENT_LIST_DIR}/patch-whisper.cmake")
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cli11
  URL https://codeload.github.com/CLIUtils/CLI11/tar.gz/refs/tags/v${WT_CLI11_VERSION}
  URL_HASH SHA256=17e02b4cddc2fa348e5dbdbb582c59a3486fa2b2433e70a0c3bacb871334fd55)
FetchContent_Declare(nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v${WT_JSON_VERSION}
  URL_HASH SHA256=d69f9deb6a75e2580465c6c4c5111b89c4dc2fa94e3a85fcd2ffcd9a143d9273
  SOURCE_SUBDIR single_include)
FetchContent_MakeAvailable(whisper cli11 nlohmann_json)
file(READ "${whisper_SOURCE_DIR}/src/whisper.cpp" WT_WHISPER_SOURCE_TEXT)
string(FIND "${WT_WHISPER_SOURCE_TEXT}" "if (params.use_gpu && !backend_gpu)" WT_GPU_GUARD)
string(FIND "${WT_WHISPER_SOURCE_TEXT}" "whisper_state * state = new whisper_state{};" WT_STATE_INIT)
if(WT_GPU_GUARD EQUAL -1 OR WT_STATE_INIT EQUAL -1)
  message(FATAL_ERROR "whisper.cpp lacks the safe GPU initialization guard; apply cmake/patch-whisper.cmake to the source override")
endif()
unset(WT_WHISPER_SOURCE_TEXT)
# Pinned ggml defines AVX-VNNI for MSVC-style builds but omits ClangCL's target flag.
# Keep it private to this plugin: enabling it globally would break baseline CPUs.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND TARGET ggml-cpu-alderlake)
  target_compile_options(ggml-cpu-alderlake PRIVATE /clang:-mavxvnni)
endif()
# This pinned header needs no upstream CMake project (which predates CMake 4).
add_library(wt_json INTERFACE)
target_include_directories(wt_json SYSTEM INTERFACE "${nlohmann_json_SOURCE_DIR}/single_include")
add_library(nlohmann_json::nlohmann_json ALIAS wt_json)
