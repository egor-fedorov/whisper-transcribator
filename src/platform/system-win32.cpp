#include "platform/system.hpp"
#include <csignal>
#include <cstdlib>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace wt::platform {
namespace {
void (*interrupt_handler)(int) = nullptr;
// Runs on a thread of its own, which the system creates for each event.
BOOL WINAPI console_event(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        interrupt_handler(SIGINT);
        return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        interrupt_handler(SIGTERM);
        return TRUE;
    default:
        return FALSE;
    }
}
fs::path environment_path(const wchar_t* name) {
    const wchar_t* value = _wgetenv(name);
    return value ? value : L"";
}
fs::path module_path(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    while (path.size() <= 32768) {
        auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length)
            return {};
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
    return {};
}
struct Console {
    UINT code_page = 0;
    DWORD mode = 0;
    HANDLE output = nullptr;
    bool escapes = false;
};
Console& console() {
    static Console state;
    return state;
}
} // namespace
bool handle_interrupts(void (*handler)(int signal)) {
    interrupt_handler = handler;
    return SetConsoleCtrlHandler(console_event, TRUE);
}
void exit_now(int status) { _exit(status); }
void prepare_console() {
    auto& state = console();
    // Text is UTF-8 throughout (the executable's manifest selects UTF-8 as its code page); the
    // console otherwise shows it in the OEM code page.
    state.code_page = GetConsoleOutputCP();
    if (state.code_page && state.code_page != CP_UTF8)
        SetConsoleOutputCP(CP_UTF8);
    state.output = GetStdHandle(STD_ERROR_HANDLE);
    if (GetConsoleMode(state.output, &state.mode))
        state.escapes =
            (state.mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) ||
            SetConsoleMode(state.output, state.mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    std::atexit([] {
        auto& state = console();
        if (state.code_page && state.code_page != CP_UTF8)
            SetConsoleOutputCP(state.code_page);
        if (state.escapes && !(state.mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            SetConsoleMode(state.output, state.mode);
    });
}
bool stderr_is_terminal() {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode) &&
           (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
fs::path home_directory() { return environment_path(L"USERPROFILE"); }
fs::path cache_directory() { return environment_path(L"LOCALAPPDATA"); }
fs::path executable_path() {
    auto path = module_path(nullptr);
    std::error_code error;
    auto result = path.empty() ? path : fs::canonical(path, error);
    return error ? fs::path{} : result;
}
fs::path library_path(const void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module))
        return {};
    return module_path(module);
}
} // namespace wt::platform
