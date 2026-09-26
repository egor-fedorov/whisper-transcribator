#include "support/fixtures.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace wt;
using namespace wt::test;
namespace {
void download_hash_and_offline_reuse(const fs::path& root) {

    int calls = 0;
    auto fetch = [&](const auto&, const auto& path) {
        ++calls;
        atomic_write(path, "abc", true);
    };
    auto path = ensure_cached(model_fixture(), root, false, fetch);
    require(path.hash == model_fixture().hash && sha256(path.path) == path.hash && calls == 1);
    require(ensure_cached(model_fixture(), root, true, fetch).path == path.path && calls == 1);
    require(ensure_cached(model_fixture(), root, false, fetch).path == path.path && calls == 1);
}
void offline_missing_has_no_side_effects(const fs::path& root) {

    auto absent = root / "absent";
    rejects([&] {
        ensure_cached(model_fixture(), absent, true,
                      [](auto&, auto&) { throw std::logic_error("network"); });
    });
    require(!fs::exists(absent));
}
void corrupt_download_not_published(const fs::path& root) {

    rejects([&] {
        ensure_cached(model_fixture(), root, false,
                      [](const auto&, const auto& path) { atomic_write(path, "xyz", true); });
    });
    require(!fs::exists(root / model_fixture().file));
    for (const auto& item : fs::directory_iterator(root))
        require(item.path().extension() == ".lock");
}
void corrupt_cache_not_deleted(const fs::path& root) {

    atomic_write(root / model_fixture().file, "bad");
    rejects([&] { ensure_cached(model_fixture(), root, true); });
    rejects([&] { ensure_cached(model_fixture(), root, false); });
    require(read_text(root / model_fixture().file) == "bad");
}
void cached_model_does_not_acquire_download_lock(const fs::path& root) {

    int fd = open((root / "fixture.bin.lock").c_str(), O_CREAT | O_RDWR, 0600);
    require(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    atomic_write(root / model_fixture().file, "abc");
    require(ensure_cached(model_fixture(), root, false).hash == model_fixture().hash);
    close(fd);
}
void local_model_and_invalid_names(const fs::path& root) {

    Options o;
    o.local_files_only = true;
    o.download_root = root.string();
    atomic_write(root / "local.bin", "weights");
    auto prepared = prepare_model((root / "local.bin").string(), o);
    require(prepared.path == root / "local.bin" && prepared.hash == sha256(prepared.path));
    rejects([&] { prepare_model(root.string(), o); });
    rejects([&] { prepare_model("unknown", o); });
}
void catalog_and_listing_no_downloads(const fs::path& root) {

    Options o;
    o.download_root = (root / "absent").string();
    auto items = list_models(o);
    require(items.size() == 9 && !fs::exists(o.download_root));
    for (const auto& model : model_catalog())
        require(model.hash.size() == 64 && model.bytes > 0);
}
} // namespace
int main() {
    return run_tests({
        {"download hash and offline reuse", download_hash_and_offline_reuse},
        {"offline missing has no side effects", offline_missing_has_no_side_effects},
        {"corrupt download not published", corrupt_download_not_published},
        {"corrupt cache not deleted", corrupt_cache_not_deleted},
        {"cached model does not acquire download lock",
         cached_model_does_not_acquire_download_lock},
        {"local model and invalid names", local_model_and_invalid_names},
        {"catalog and listing no downloads", catalog_and_listing_no_downloads},
    });
}
