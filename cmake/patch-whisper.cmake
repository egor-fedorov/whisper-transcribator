# Applied by FetchContent to the pinned whisper.cpp sources; a FETCHCONTENT_SOURCE_DIR_WHISPER
# override is used unpatched. whisper_full() creates its VAD context with the default of four
# threads instead of whisper_full_params.n_threads. Without OpenMP (macOS, Linux aarch64
# archives), ggml's spinning thread pool then made VAD more than 40 times slower on fewer than
# four free cores.
set(file "${WT_WHISPER_SOURCE}/src/whisper.cpp")
file(READ "${file}" text)
set(original "${text}")
set(anchor "        struct whisper_vad_context_params vad_ctx_params = whisper_vad_default_context_params();\n")
set(line "        vad_ctx_params.n_threads = params.n_threads;\n")
string(FIND "${text}" "${anchor}${line}" patched)
if(patched EQUAL -1)
  string(FIND "${text}" "${anchor}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "whisper.cpp VAD context setup changed; review cmake/patch-whisper.cmake")
  endif()
  string(REPLACE "${anchor}" "${anchor}${line}" text "${text}")
endif()
# Explicit GPU selection must not silently initialize a CPU-only inference state.
set(anchor "    ggml_backend_t backend_gpu = whisper_backend_init_gpu(params);\n")
set(line "    if (params.use_gpu && !backend_gpu) {\n        return {};\n    }\n")
string(FIND "${text}" "${anchor}${line}" patched)
if(patched EQUAL -1)
  string(FIND "${text}" "${anchor}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "whisper.cpp GPU initialization changed; review cmake/patch-whisper.cmake")
  endif()
  string(REPLACE "${anchor}" "${anchor}${line}" text "${text}")
endif()
# The empty-backend error path frees state->batch before whisper_batch_init() ran.
# Value-initialize the state so early cleanup never frees indeterminate pointers.
set(anchor "whisper_state * state = new whisper_state;")
set(line "whisper_state * state = new whisper_state{};")
string(FIND "${text}" "${line}" patched)
if(patched EQUAL -1)
  string(FIND "${text}" "${anchor}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "whisper.cpp state allocation changed; review cmake/patch-whisper.cmake")
  endif()
  string(REPLACE "${anchor}" "${line}" text "${text}")
endif()
if(NOT text STREQUAL original)
  file(WRITE "${file}" "${text}")
endif()
