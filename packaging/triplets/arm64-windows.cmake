set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

if(PORT STREQUAL "openssl")
    # The MSVC 14.51 ARM64 build crashes in the test server's TLS handshake.
    # Keep the same pinned OpenSSL sources, using the application's compiler.
    set(VCPKG_LOAD_VCVARS_ENV ON)
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../windows-arm64-clang.cmake")
endif()
