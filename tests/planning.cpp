#include "app.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main() {
    char pattern[] = "/tmp/whisper-planning-XXXXXX";
    auto created = mkdtemp(pattern);
    if (!created)
        return 1;
    wt::fs::path root = created;
    try {
        wt::Options options;
        options.output_dir = (root / "output").string();
        options.format = "all";
        for (int count : {200, 800, 3200}) {
            while (options.inputs.size() < static_cast<size_t>(count)) {
                auto path = root / (std::to_string(options.inputs.size()) + ".wav");
                std::ofstream file(path);
                file << "fixture";
                file.close();
                if (!file)
                    throw std::runtime_error("Cannot create planning fixture");
                options.inputs.push_back(path.string());
            }
            auto start = std::chrono::steady_clock::now();
            auto jobs = wt::prepare_jobs(options);
            if (jobs.size() != static_cast<size_t>(count))
                throw std::runtime_error("Unexpected job count");
            std::cout
                << count << " files: "
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                << "s\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        wt::fs::remove_all(root);
        return 1;
    }
    wt::fs::remove_all(root);
}
