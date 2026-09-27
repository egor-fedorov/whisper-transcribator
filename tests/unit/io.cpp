#include "support/io.hpp"
#include "platform/file.hpp"
#include "platform/posix.hpp"
#include "support/cancel.hpp"
#include "support/test.hpp"
#include <cerrno>

using namespace wt;
using namespace wt::test;
using platform::NoReplaceSteps;
using platform::rename_noreplace;
namespace {
int exclusive_calls = 0, link_calls = 0;
// Injected primitives fail like a kernel or filesystem without the corresponding support.
template <int Error> int failed_exclusive(const char*, const char*) {
    ++exclusive_calls;
    errno = Error;
    return -1;
}
template <int Error> int failed_link(const char*, const char*) {
    ++link_calls;
    errno = Error;
    return -1;
}
int counted_link(const char* from, const char* to) {
    ++link_calls;
    return link(from, to);
}
void expect_no_replace(const fs::path& from, const fs::path& to, const NoReplaceSteps& steps) {
    atomic_write(from, "new");
    atomic_write(to, "old");
    require(!rename_noreplace(from, to, steps) && errno == EEXIST, "existing target");
    require(read_text(from) == "new" && read_text(to) == "old");
    fs::remove(to);
    require(rename_noreplace(from, to, steps), "absent target");
    require(!fs::exists(from) && read_text(to) == "new");
    fs::remove(to);
}
void atomic_no_clobber_and_replacement(const fs::path& root) {

    auto path = root / "result";
    atomic_write(path, "original");
    rejects([&] { atomic_write(path, "new"); }, "Cannot publish");
    require(read_text(path) == "original");
    atomic_write(path, "new", true);
    require(read_text(path) == "new");
    require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
}
void cancellation_preserves_output(const fs::path& root) {

    auto path = root / "result";
    atomic_write(path, "old");
    stop_signal = SIGTERM;
    bool stopped = false;
    try {
        atomic_write(path, "new", true);
    } catch (const Cancelled&) {
        stopped = true;
    }
    stop_signal = 0;
    require(stopped && read_text(path) == "old");
    require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
}
void rename_noreplace_keeps_existing_targets(const fs::path& root) {
    expect_no_replace(root / "staged", root / "target", {});
    fs::create_directory(root / "directory");
    atomic_write(root / "staged", "new");
    require(!rename_noreplace(root / "staged", root / "directory") && errno == EEXIST);
    require(fs::is_empty(root / "directory") && read_text(root / "staged") == "new");
}
void rename_noreplace_falls_back_to_hard_links(const fs::path& root) {
    // ENOTSUP is how macOS reports a filesystem without RENAME_EXCL.
    for (auto exclusive :
         {failed_exclusive<EINVAL>, failed_exclusive<ENOSYS>, failed_exclusive<ENOTSUP>}) {
        exclusive_calls = link_calls = 0;
        expect_no_replace(root / "staged", root / "target", {exclusive, counted_link});
        require(exclusive_calls == 2 && link_calls == 2);
    }
}
void rename_noreplace_falls_back_to_checked_rename(const fs::path& root) {
    for (auto link :
         {failed_link<EPERM>, failed_link<EOPNOTSUPP>, failed_link<ENOTSUP>, failed_link<ENOSYS>}) {
        exclusive_calls = link_calls = 0;
        expect_no_replace(root / "staged", root / "target", {failed_exclusive<ENOSYS>, link});
        require(exclusive_calls == 2 && link_calls == 2);
    }
    // Names without a directory lock the current directory.
    ScopedCurrentPath current(root);
    NoReplaceSteps unsupported{failed_exclusive<EINVAL>, failed_link<EPERM>};
    atomic_write("./staged", "new");
    atomic_write("./target", "old");
    require(!rename_noreplace("staged", "target", unsupported) && errno == EEXIST);
    fs::remove("target");
    require(rename_noreplace("staged", "target", unsupported));
    require(!fs::exists("staged") && read_text("target") == "new");
    fs::create_directory("directory");
    require(!rename_noreplace("target", "directory", unsupported) && errno == EEXIST);
    require(!rename_noreplace("target", "missing/name", unsupported) && errno == ENOENT);
    require(!rename_noreplace("absent", "name", unsupported) && errno == ENOENT);
    require(read_text("target") == "new" && fs::is_empty("directory") && !fs::exists("name"));
}
void rename_noreplace_reports_other_errors(const fs::path& root) {
    atomic_write(root / "staged", "new");
    exclusive_calls = link_calls = 0;
    require(!rename_noreplace(root / "staged", root / "target",
                              {failed_exclusive<EACCES>, counted_link}) &&
            errno == EACCES);
    require(exclusive_calls == 1 && link_calls == 0);
    require(!rename_noreplace(root / "staged", root / "target",
                              {failed_exclusive<EINVAL>, failed_link<EMLINK>}) &&
            errno == EMLINK);
    require(!rename_noreplace(root / "staged", root / "target",
                              {failed_exclusive<ENOSYS>, failed_link<EXDEV>}) &&
            errno == EXDEV);
    require(read_text(root / "staged") == "new" && !fs::exists(root / "target"));
}
void stored_permissions_relax_only_missing_attributes(const fs::path& root) {
    platform::FileStatus own, foreign;
    own.owned = true;
    StoredPermissions all, owner{true, false}, none{false, false};
    require(all.accepts(own, true) && !all.accepts(own, false) && !all.accepts(foreign, true));
    require(owner.accepts(own, false) && !owner.accepts(foreign, false));
    require(none.accepts(foreign, false));
    auto probed = probe_permissions(root / "missing");
    require(probed.owner && probed.mode, "an unprobeable directory keeps every check");
    probe_permissions(root);
    require(fs::is_empty(root), "the probe file is removed");
}
} // namespace
int main() {
    return run_tests({
        {"atomic no clobber and replacement", atomic_no_clobber_and_replacement},
        {"cancellation preserves output", cancellation_preserves_output},
        {"no-replace rename keeps existing targets", rename_noreplace_keeps_existing_targets},
        {"no-replace rename falls back to hard links", rename_noreplace_falls_back_to_hard_links},
        {"no-replace rename falls back to a checked rename",
         rename_noreplace_falls_back_to_checked_rename},
        {"no-replace rename reports other errors", rename_noreplace_reports_other_errors},
        {"stored permissions relax only missing attributes",
         stored_permissions_relax_only_missing_attributes},
    });
}
