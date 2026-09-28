#include "support/hash.hpp"
#include "platform/sha256.hpp"
#include "support/cancel.hpp"
#include "support/report.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace wt {
namespace {
class Sha256 {
    platform::Sha256 digest;

  public:
    void update(const char* data, size_t size) { digest.update(data, size); }
    std::string hex() {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (unsigned char byte : digest.finish())
            output << std::setw(2) << static_cast<int>(byte);
        return output.str();
    }
};
} // namespace
std::string sha256_text(const std::string& text) {
    Sha256 digest;
    digest.update(text.data(), text.size());
    return digest.hex();
}
std::string sha256(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot hash: " + path.string());
    Sha256 digest;
    char buffer[65536];
    uint64_t hashed = 0;
    while (input) {
        check_cancelled();
        input.read(buffer, sizeof(buffer));
        hashed += static_cast<uint64_t>(input.gcount());
        report_progress("Verifying SHA-256", path.filename().string() + " | " +
                                                 std::to_string(hashed / 1024 / 1024) + " MiB");
        digest.update(buffer, static_cast<size_t>(input.gcount()));
    }
    if (!input.eof())
        throw std::runtime_error("Hash read failed");
    return digest.hex();
}
} // namespace wt
