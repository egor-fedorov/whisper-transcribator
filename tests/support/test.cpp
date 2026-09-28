#include "support/test.hpp"
#include "platform/file.hpp"
#include "platform/system.hpp"
#include "support/cancel.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
#ifdef _WIN32
#include <aclapi.h>
#include <psapi.h>
#include <sddl.h>
#include <windows.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace wt::test {
TempDirectory::TempDirectory(const fs::path& parent) {
    fs::create_directories(parent);
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = parent / ("whisper-test-" + std::to_string(random()));
        if (platform::create_private_directory(candidate)) {
            path = fs::canonical(candidate);
            return;
        }
        if (errno != EEXIST)
            break;
    }
    throw std::runtime_error("Cannot create test directory");
}
TempDirectory::~TempDirectory() {
    if (!retained) {
        std::error_code error;
        fs::remove_all(path, error);
    }
}
ScopedCurrentPath::ScopedCurrentPath(const fs::path& path) : previous(fs::current_path()) {
    fs::current_path(path);
}
ScopedCurrentPath::~ScopedCurrentPath() {
    std::error_code error;
    fs::current_path(previous, error);
}
ScopedEnv::ScopedEnv(const std::string& name, const std::optional<std::string>& value) : key(name) {
    if (const auto* old = std::getenv(key.c_str()))
        previous = old;
#ifdef _WIN32
    auto result = _putenv_s(key.c_str(), value ? value->c_str() : "");
#else
    auto result = value ? setenv(key.c_str(), value->c_str(), 1) : unsetenv(key.c_str());
#endif
    if (result)
        throw std::runtime_error("Cannot set test environment: " + key);
}
ScopedEnv::~ScopedEnv() {
#ifdef _WIN32
    _putenv_s(key.c_str(), previous ? previous->c_str() : "");
#else
    if (previous)
        setenv(key.c_str(), previous->c_str(), 1);
    else
        unsetenv(key.c_str());
#endif
}
int64_t peak_rss_kib() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0);
    return static_cast<int64_t>(counters.PeakWorkingSetSize / 1024);
#else
    rusage usage{};
    require(getrusage(RUSAGE_SELF, &usage) == 0);
#ifdef __APPLE__
    return usage.ru_maxrss / 1024;
#else
    return usage.ru_maxrss;
#endif
#endif
}
void permissions(const fs::path& path, fs::perms mode, std::error_code& error) {
#ifdef _WIN32
    if ((mode & fs::perms::others_all) != fs::perms::none) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr)) {
            error.assign(static_cast<int>(GetLastError()), std::system_category());
            return;
        }
        BOOL present = FALSE, defaulted = FALSE;
        PACL acl = nullptr;
        DWORD result = ERROR_INVALID_SECURITY_DESCR;
        if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present)
            result = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION |
                                               PROTECTED_DACL_SECURITY_INFORMATION,
                                           nullptr, nullptr, acl, nullptr);
        LocalFree(descriptor);
        error.assign(static_cast<int>(result), std::system_category());
        return;
    }
    auto file =
        fs::is_directory(path) ? platform::open_directory(path) : platform::open_for_reading(path);
    if (!file || !platform::set_permissions(file, static_cast<unsigned>(mode)))
        error.assign(errno, std::generic_category());
    else
        error.clear();
#else
    fs::permissions(path, mode, error);
#endif
}
void permissions(const fs::path& path, fs::perms mode) {
    std::error_code error;
    wt::test::permissions(path, mode, error);
    if (error)
        throw std::runtime_error("Cannot set fixture permissions: " + error.message());
}
StreamCapture::StreamCapture(std::ostream& stream, std::streambuf* buffer)
    : stream(stream), previous(stream.rdbuf(buffer)) {}
StreamCapture::~StreamCapture() { stream.rdbuf(previous); }
int run_tests(std::initializer_list<TestCase> cases) {
    size_t failures = 0;
    for (const auto& item : cases) {
        fs::path fixtures;
        auto signal = stop_signal.load();
        try {
            TempDirectory directory;
            fixtures = directory.path;
            try {
                item.action(directory.path);
            } catch (...) {
                directory.preserve();
                throw;
            }
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAILED " << item.name << ": " << error.what()
                      << " (fixtures: " << fixtures << ")\n";
        } catch (...) {
            ++failures;
            std::cerr << "FAILED " << item.name << ": unexpected exception (fixtures: " << fixtures
                      << ")\n";
        }
        stop_signal = signal;
    }
    std::cout << cases.size() - failures << '/' << cases.size() << " scenarios passed\n";
    return failures ? 1 : 0;
}
} // namespace wt::test
