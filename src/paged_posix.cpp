#include "paged_file.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace swiftedit {
class PagedFileState {
public:
    int descriptor{-1};
    struct stat original{};
    PagedFileState() = default;
    ~PagedFileState() {
        if (descriptor >= 0)
            close(descriptor);
    }
    PagedFileState(const PagedFileState &) = delete;
    PagedFileState &operator=(const PagedFileState &) = delete;
};
namespace {
void verify_unchanged(const PagedFileState &state) {
    struct stat current{};
    if (fstat(state.descriptor, &current) != 0)
        throw std::runtime_error("Cannot inspect paged source.");
    const struct stat &original = state.original;
#ifdef __APPLE__
    const timespec modified = current.st_mtimespec;
    const timespec expected_modified = original.st_mtimespec;
    const timespec changed = current.st_ctimespec;
    const timespec expected_changed = original.st_ctimespec;
#else
    const timespec modified = current.st_mtim;
    const timespec expected_modified = original.st_mtim;
    const timespec changed = current.st_ctim;
    const timespec expected_changed = original.st_ctim;
#endif
    if (current.st_dev != original.st_dev || current.st_ino != original.st_ino ||
        current.st_size != original.st_size || modified.tv_sec != expected_modified.tv_sec ||
        modified.tv_nsec != expected_modified.tv_nsec ||
        changed.tv_sec != expected_changed.tv_sec || changed.tv_nsec != expected_changed.tv_nsec)
        throw std::runtime_error(
            "Paged source changed outside SwiftEdit. Reopen it before reading.");
}
} // namespace
PagedFile::PagedFile(const std::filesystem::path &path) {
    state_ = std::make_unique<PagedFileState>();
    PagedFileState &state = *state_;
    // NONBLOCK prevents a FIFO/device path from hanging before regular-file
    // validation. NOFOLLOW rejects a final symlink; normal parent aliases work.
    state.descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (state.descriptor < 0)
        throw std::runtime_error("Cannot open read-only paged file.");
    if (fstat(state.descriptor, &state.original) != 0 || !S_ISREG(state.original.st_mode) ||
        state.original.st_size < 0)
        throw std::runtime_error("Paged source must be a regular file.");
    size_ = static_cast<std::uint64_t>(state.original.st_size);
}
PagedFile::~PagedFile() = default;
Page PagedFile::page(std::uint64_t offset, std::size_t budget) const {
    if (!budget || budget > maximum_page)
        throw std::runtime_error("Page budget must be 1..65536 bytes.");
    if (offset > size_ || offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
        throw std::runtime_error("Page offset exceeds file size.");
    verify_unchanged(*state_);
    Page result{offset, offset, size_, {}};
    result.bytes.resize(static_cast<std::size_t>(std::min<std::uint64_t>(budget, size_ - offset)));
    std::size_t copied = 0;
    while (copied < result.bytes.size()) {
        const ssize_t count =
            pread((*state_).descriptor, result.bytes.data() + copied, result.bytes.size() - copied,
                  static_cast<off_t>(offset + copied));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("Paged read failed; no document changed.");
        copied += static_cast<std::size_t>(count);
    }
    verify_unchanged(*state_);
    result.next += copied;
    return result;
}
} // namespace swiftedit
