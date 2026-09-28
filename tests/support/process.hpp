#pragma once
#include "support/fs.hpp"
#include <memory>
#include <string>
#include <vector>

namespace wt::test {
// Child processes never outlive their owning test; every wait has a deadline.
class Process {
    struct State;
    std::unique_ptr<State> state;

  public:
    Process(const fs::path& executable, const std::vector<std::string>& args,
            const fs::path& log = {});
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    bool running();
    int wait(int timeout_seconds = 30);
    void interrupt(int signal);
    void terminate();
};
void await_file(const fs::path& path);
void wait_for_interrupt();
} // namespace wt::test
