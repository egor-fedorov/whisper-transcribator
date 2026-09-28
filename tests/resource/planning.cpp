#include "app/configuration.hpp"
#include "support/test.hpp"
#include "transcript/jobs.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
void planning(const wt::fs::path& root) {
    wt::CliOptions options;
    options.jobs.output_dir = (root / "output").string();
    options.jobs.format = "all";
    for (int count : {200, 800, 3200}) {
        while (options.jobs.inputs.size() < static_cast<size_t>(count)) {
            auto path = root / (std::to_string(options.jobs.inputs.size()) + ".wav");
            std::ofstream file(path);
            file << "fixture";
            file.close();
            if (!file)
                throw std::runtime_error("Cannot create planning fixture");
            options.jobs.inputs.push_back(path.string());
        }
        auto start = std::chrono::steady_clock::now();
        auto jobs = wt::prepare_jobs(options.jobs, options.checkpoint);
        if (jobs.size() != static_cast<size_t>(count))
            throw std::runtime_error("Unexpected job count");
        std::cout << count << " files: "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                  << "s\n";
    }
}
} // namespace
int main() { return wt::test::run_tests({{"job planning at 200, 800 and 3200 files", planning}}); }
