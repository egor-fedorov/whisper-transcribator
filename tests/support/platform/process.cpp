#include "support/platform/process.hpp"
#include "support/cancel.hpp"
#include "support/test.hpp"
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace wt::test {
struct Process::State {
#ifdef _WIN32
    HANDLE process = nullptr;
    DWORD pid = 0;
    ~State() {
        if (process)
            CloseHandle(process);
    }
#else
    pid_t pid = -1;
#endif
    bool finished = false;
    int code = 0;
};
#ifdef _WIN32
namespace {
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (ch == L'\"')
            result += L'\\';
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
} // namespace
#endif
Process::Process(const fs::path& executable, const std::vector<std::string>& args,
                 const fs::path& log)
    : state(std::make_unique<State>()) {
#ifdef _WIN32
    // Hosted CI may start tests without a console. Control events require a shared console.
    if (!GetConsoleCP()) {
        require(AllocConsole() != 0, "Cannot allocate test console");
        ShowWindow(GetConsoleWindow(), SW_HIDE);
    }
    std::wstring command = quote(executable.wstring());
    for (const auto& arg : args)
        command += L" " + quote(fs::u8path(arg).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = INVALID_HANDLE_VALUE, input = INVALID_HANDLE_VALUE;
    if (!log.empty()) {
        output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                            OPEN_EXISTING, 0, nullptr);
        if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
            if (output != INVALID_HANDLE_VALUE)
                CloseHandle(output);
            if (input != INVALID_HANDLE_VALUE)
                CloseHandle(input);
            throw std::runtime_error("Cannot redirect test process");
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = output;
        startup.hStdInput = input;
    }
    PROCESS_INFORMATION info{};
    auto ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, !log.empty(),
                             CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &info);
    if (output != INVALID_HANDLE_VALUE)
        CloseHandle(output);
    if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
    require(ok != 0, "Cannot start test process");
    state->process = info.hProcess;
    state->pid = info.dwProcessId;
    CloseHandle(info.hThread);
#else
    std::vector<std::string> strings{executable.string()};
    strings.insert(strings.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& arg : strings)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    require(posix_spawn_file_actions_init(&actions) == 0);
    int error = 0;
    if (!log.empty()) {
        error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(),
                                                 O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (!error)
            error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    }
    if (!error)
        error =
            posix_spawn(&state->pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    require(error == 0, "Cannot start test process: " + std::to_string(error));
#endif
}
Process::~Process() {
    try {
        if (!state->finished) {
            terminate();
            wait(5);
        }
    } catch (...) {
    }
}
bool Process::running() {
    if (state->finished)
        return false;
#ifdef _WIN32
    auto status = WaitForSingleObject(state->process, 0);
    if (status == WAIT_TIMEOUT)
        return true;
    require(status == WAIT_OBJECT_0);
    DWORD code = 0;
    require(GetExitCodeProcess(state->process, &code) != 0);
    state->code = static_cast<int>(code);
#else
    int status = 0;
    auto pid = waitpid(state->pid, &status, WNOHANG);
    if (pid == 0 || (pid < 0 && errno == EINTR))
        return true;
    require(pid == state->pid, "waitpid failed");
    state->code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    state->finished = true;
    return false;
}
int Process::wait(int timeout_seconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (running()) {
        require(std::chrono::steady_clock::now() < deadline, "Child process timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return state->code;
}
void Process::interrupt(int signal) {
    require(running(), "Child exited before interrupt");
#ifdef _WIN32
    require(signal == SIGINT, "Windows console interruption maps to SIGINT");
    require(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, state->pid) != 0);
#else
    require(kill(state->pid, signal) == 0);
#endif
}
void Process::terminate() {
    if (!running())
        return;
#ifdef _WIN32
    require(TerminateProcess(state->process, 137) != 0);
#else
    require(kill(state->pid, SIGKILL) == 0);
#endif
}
void await_file(const fs::path& path) {
    for (int i = 0; i < 1000; ++i) {
        if (fs::exists(path))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Worker timed out: " + path.string());
}
void wait_for_interrupt() {
    while (!stop_signal)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check_cancelled();
}
} // namespace wt::test
