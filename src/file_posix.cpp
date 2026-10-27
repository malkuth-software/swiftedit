#include "document.hpp"
#include "new_file_writer.hpp"
#include <atomic>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <copyfile.h>
#include <stdio.h>
#else
#include <linux/fs.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#endif

namespace notepad {
#ifdef SWIFTEDIT_FILE_TESTING
// Only the isolated adapter test target supplies this deterministic race seam.
void file_test_before_publish(const std::filesystem::path &);
#endif
namespace {
struct Descriptor {
    int value{-1};
    explicit Descriptor(int source) noexcept : value(source) {}
    ~Descriptor() {
        if (value >= 0)
            close(value);
    }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};
[[noreturn]] void fail(const char *action) {
    const int error = errno;
    throw std::runtime_error(std::string(action) + " (POSIX error " + std::to_string(error) +
                             "). Keep the document open; its in-memory contents are retained.");
}
timespec modified_time(const struct stat &info) {
#ifdef __APPLE__
    return info.st_mtimespec;
#else
    return info.st_mtim;
#endif
}
timespec changed_time(const struct stat &info) {
#ifdef __APPLE__
    return info.st_ctimespec;
#else
    return info.st_ctim;
#endif
}
bool same_state(const struct stat &first, const struct stat &second) {
    const timespec first_modified = modified_time(first);
    const timespec second_modified = modified_time(second);
    const timespec first_changed = changed_time(first);
    const timespec second_changed = changed_time(second);
    const bool equal = first.st_dev == second.st_dev && first.st_ino == second.st_ino &&
                       first.st_size == second.st_size && first.st_mode == second.st_mode &&
                       first.st_uid == second.st_uid && first.st_gid == second.st_gid &&
                       first.st_nlink == second.st_nlink &&
                       first_modified.tv_sec == second_modified.tv_sec &&
                       first_modified.tv_nsec == second_modified.tv_nsec &&
                       first_changed.tv_sec == second_changed.tv_sec &&
                       first_changed.tv_nsec == second_changed.tv_nsec;
    return equal;
}
struct stat inspect(int descriptor) {
    struct stat info{};
    if (fstat(descriptor, &info) != 0)
        fail("Cannot inspect file");
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1)
        throw std::runtime_error("Choose an ordinary file with one hard link.");
    if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > maximum_bytes)
        throw std::runtime_error("This save adapter supports files up to 16 MiB.");
    return info;
}
FileSnapshot snapshot(int descriptor) {
    const struct stat before = inspect(descriptor);
    const timespec modified = modified_time(before);
    FileSnapshot result{true,
                        static_cast<std::uint64_t>(before.st_dev),
                        static_cast<std::uint64_t>(before.st_ino),
                        static_cast<std::uint64_t>(modified.tv_sec) * 1000000000ULL +
                            static_cast<std::uint64_t>(modified.tv_nsec),
                        {}};
    result.bytes.resize(static_cast<std::size_t>(before.st_size));
    std::size_t copied = 0;
    while (copied < result.bytes.size()) {
        const ssize_t count = pread(descriptor, result.bytes.data() + copied,
                                    result.bytes.size() - copied, static_cast<off_t>(copied));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            fail("Cannot read complete file");
        copied += static_cast<std::size_t>(count);
    }
    const struct stat after = inspect(descriptor);
    if (!same_state(before, after))
        throw std::runtime_error("The file changed while it was being read. Try opening it again.");
    return result;
}
int open_parent(const std::filesystem::path &path) {
    const std::string &native = path.native();
    if (!path.is_absolute() || path.filename().empty() || path.filename() == "." ||
        path.filename() == ".." || native.find('\0') != std::string::npos)
        throw std::runtime_error("An absolute ordinary file path is required.");
    const int descriptor = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
        fail("Cannot open parent directory");
    return descriptor;
}
int open_source(int parent, const std::string &name) {
    const int descriptor =
        openat(parent, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0 && errno != ENOENT)
        fail("Cannot open ordinary source file");
    return descriptor;
}
FileSnapshot read_at(int parent, const std::string &name) {
    const Descriptor file(open_source(parent, name));
    if (file.value < 0)
        return {};
    const FileSnapshot result = snapshot(file.value);
    return result;
}
void copy_metadata(int source, int target, const struct stat &original) {
#ifdef __APPLE__
    if (fcopyfile(source, target, nullptr, COPYFILE_METADATA) != 0)
        fail("Cannot preserve file metadata");
#else
    struct stat created{};
    if (fstat(target, &created) != 0)
        fail("Cannot inspect prepared file");
    if ((created.st_uid != original.st_uid || created.st_gid != original.st_gid) &&
        fchown(target, original.st_uid, original.st_gid) != 0)
        fail("Cannot preserve file ownership");
    if (fchmod(target, original.st_mode & 07777) != 0)
        fail("Cannot preserve file permissions");
    const ssize_t names_size = flistxattr(source, nullptr, 0);
    if (names_size < 0)
        fail("Cannot inspect extended attributes");
    if (names_size > 65536)
        throw std::runtime_error("File metadata exceeds the supported copy budget; save refused.");
    std::vector<char> names(static_cast<std::size_t>(names_size));
    if (names_size && flistxattr(source, names.data(), names.size()) != names_size)
        fail("File metadata changed while copying");
    for (std::size_t offset = 0; offset < names.size();) {
        std::size_t end = offset;
        while (end < names.size() && names[end] != '\0')
            ++end;
        if (end == names.size())
            throw std::runtime_error("Invalid extended-attribute names; save refused.");
        const char *name = names.data() + offset;
        const ssize_t size = fgetxattr(source, name, nullptr, 0);
        if (size < 0)
            fail("Cannot read extended attribute");
        if (size > 1024 * 1024)
            throw std::runtime_error("Extended attribute exceeds the copy budget; save refused.");
        std::vector<char> value(static_cast<std::size_t>(size));
        if (fgetxattr(source, name, value.data(), value.size()) != size ||
            fsetxattr(target, name, value.data(), value.size(), 0) != 0)
            fail("Cannot preserve extended attribute");
        offset = end + 1;
    }
#endif
    static_cast<void>(original);
}
int exchange_names(int parent, const std::string &temporary, const std::string &destination) {
#ifdef __APPLE__
    const int result =
        renameatx_np(parent, temporary.c_str(), parent, destination.c_str(), RENAME_SWAP);
#else
    const long status = syscall(SYS_renameat2, parent, temporary.c_str(), parent,
                                destination.c_str(), RENAME_EXCHANGE);
    const int result = static_cast<int>(status);
#endif
    return result;
}
struct Temporary {
    int parent;
    const std::string &name;
    bool remove{true};
    Temporary(int directory, const std::string &filename) noexcept
        : parent(directory), name(filename) {}
    ~Temporary() {
        if (remove)
            unlinkat(parent, name.c_str(), 0);
    }
    Temporary(const Temporary &) = delete;
    Temporary &operator=(const Temporary &) = delete;
};
} // namespace
class NewFileWriterState final {
public:
    explicit NewFileWriterState(const std::filesystem::path &path)
        : parent(open_parent(path)), name(path.filename().native()) {
        struct stat existing{};
        if (fstatat(parent.value, name.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0)
            throw std::runtime_error("Text copy destination already exists.");
        if (errno != ENOENT)
            fail("Cannot inspect text copy destination");
        static std::atomic<unsigned long> sequence{};
        for (int attempt = 0; attempt < 100; ++attempt) {
            const unsigned long ordinal = ++sequence;
            temporary = ".swiftedit-copy-" + std::to_string(getpid()) + "-" +
                        std::to_string(ordinal) + ".tmp";
            prepared.value = openat(parent.value, temporary.c_str(),
                                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (prepared.value >= 0)
                return;
            if (errno != EEXIST)
                fail("Cannot create text copy temporary");
        }
        fail("Cannot reserve text copy temporary");
    }
    ~NewFileWriterState() {
        if (prepared.value < 0)
            return;
        struct stat owned{}, named{};
        if (fstat(prepared.value, &owned) == 0 &&
            fstatat(parent.value, temporary.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            owned.st_dev == named.st_dev && owned.st_ino == named.st_ino)
            unlinkat(parent.value, temporary.c_str(), 0);
    }
    Descriptor parent;
    std::string name{}, temporary{};
    Descriptor prepared{-1};
    bool published{}, failed{};
};
NewFileWriter::NewFileWriter(const std::filesystem::path &path)
    : state_(std::make_unique<NewFileWriterState>(path)) {}
NewFileWriter::~NewFileWriter() = default;
void NewFileWriter::append(const std::string_view bytes) {
    NewFileWriterState &state = *state_;
    if (state.published || state.failed)
        throw std::runtime_error("Text copy writer is no longer writable.");
    if (bytes.size() > 65539)
        throw std::runtime_error("Text copy output chunk exceeds its bounded size.");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = write(state.prepared.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            state.failed = true;
            fail("Cannot write text copy temporary");
        }
        offset += static_cast<std::size_t>(count);
    }
}
void NewFileWriter::publish() {
    NewFileWriterState &state = *state_;
    if (state.published || state.failed)
        throw std::runtime_error("Text copy writer cannot publish again.");
    state.failed = true;
    if (fsync(state.prepared.value) != 0)
        fail("Cannot flush text copy temporary");
    struct stat owned{}, named{};
    if (fstat(state.prepared.value, &owned) != 0 ||
        fstatat(state.parent.value, state.temporary.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0)
        fail("Cannot validate text copy temporary");
    if (owned.st_dev != named.st_dev || owned.st_ino != named.st_ino || !S_ISREG(named.st_mode))
        throw std::runtime_error("Text copy temporary changed before publication.");
    if (linkat(state.parent.value, state.temporary.c_str(), state.parent.value, state.name.c_str(), 0) != 0)
        fail("Cannot publish text copy without overwriting");
    state.published = true;
    if (unlinkat(state.parent.value, state.temporary.c_str(), 0) != 0)
        throw std::runtime_error("Text copy was published, but temporary cleanup failed.");
    if (fsync(state.parent.value) != 0)
        throw std::runtime_error("Text copy was published, but directory flush failed.");
}
FileSnapshot read_file(const std::filesystem::path &path) {
    const Descriptor parent(open_parent(path));
    const FileSnapshot result = read_at(parent.value, path.filename().native());
    return result;
}
FileSnapshot write_file(const std::filesystem::path &path, std::string_view bytes,
                        const FileSnapshot &expected) {
    if (bytes.size() > maximum_bytes)
        throw std::runtime_error("Output exceeds 16 MiB.");
    const Descriptor parent(open_parent(path));
    const std::string name = path.filename().native();
    const Descriptor current(open_source(parent.value, name));
    FileSnapshot actual{};
    struct stat original{};
    if (current.value >= 0) {
        if (flock(current.value, LOCK_EX | LOCK_NB) != 0)
            fail("Another cooperating writer holds the file");
        original = inspect(current.value);
        actual = snapshot(current.value);
    }
    if (actual != expected)
        throw std::runtime_error(
            "The destination changed outside SwiftEdit. Nothing was overwritten.");
    if (actual.exists && (!(original.st_mode & 0222) ||
                          faccessat(parent.value, name.c_str(), W_OK, AT_EACCESS) != 0))
        throw std::runtime_error("The destination is read-only. Use Save As to another name.");
    static std::atomic<unsigned long> sequence{};
    std::string temporary{};
    int created = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const unsigned long ordinal = ++sequence;
        temporary =
            ".swiftedit-" + std::to_string(getpid()) + "-" + std::to_string(ordinal) + ".tmp";
        created =
            openat(parent.value, temporary.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (created >= 0)
            break;
        if (errno != EEXIST)
            fail("Cannot create sibling temporary file");
    }
    if (created < 0)
        fail("Cannot reserve temporary file");
    const Descriptor prepared(created);
    Temporary cleanup(parent.value, temporary);
    const std::filesystem::path recovery = path.parent_path() / temporary;
    std::size_t copied = 0;
    while (copied < bytes.size()) {
        const ssize_t count = write(prepared.value, bytes.data() + copied, bytes.size() - copied);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            fail("Cannot write prepared file");
        copied += static_cast<std::size_t>(count);
    }
    if (actual.exists) {
        copy_metadata(current.value, prepared.value, original);
        // Preserve access time and metadata, but an edited file must receive a
        // fresh modification time even when COPYFILE_METADATA copied the old one.
        std::array<timespec, 2> times{};
#ifdef __APPLE__
        times[0] = original.st_atimespec;
#else
        times[0] = original.st_atim;
#endif
        times[1].tv_nsec = UTIME_NOW;
        if (futimens(prepared.value, times.data()) != 0)
            fail("Cannot set edited file timestamp");
    }
    if (fsync(prepared.value) != 0)
        fail("Cannot flush prepared file");
    const FileSnapshot candidate = snapshot(prepared.value);
    const FileSnapshot before = read_at(parent.value, name);
    if (before != expected)
        throw std::runtime_error(
            "Destination changed before publication. Nothing was overwritten.");
    if (actual.exists) {
        const struct stat now = inspect(current.value);
        if (!same_state(original, now))
            throw std::runtime_error("File metadata changed before publication. Save refused.");
    }
#ifdef SWIFTEDIT_FILE_TESTING
    file_test_before_publish(path);
#endif
    if (actual.exists) {
        if (exchange_names(parent.value, temporary, name) != 0)
            fail("Cannot atomically replace file");
        // From this point the temporary name contains the displaced source. On
        // any verification failure leave it recoverable; never delete blindly.
        cleanup.remove = false;
        try {
            const FileSnapshot displaced = read_at(parent.value, temporary);
            const FileSnapshot installed = read_at(parent.value, name);
            if (displaced != expected || installed != candidate)
                throw std::runtime_error("A concurrent change was detected during publication.");
        } catch (const std::exception &failure) {
            throw std::runtime_error(std::string(failure.what()) +
                                     " Displaced contents retained at " + recovery.string() +
                                     ". Keep this document open.");
        }
    } else {
        // linkat is atomic and refuses an existing destination, including a
        // destination created by another process after the last snapshot.
        if (linkat(parent.value, temporary.c_str(), parent.value, name.c_str(), 0) != 0)
            fail("Cannot publish new file without overwriting");
        cleanup.remove = false;
    }
    if (unlinkat(parent.value, temporary.c_str(), 0) != 0)
        throw std::runtime_error("File was published, but cleanup failed at " + recovery.string() +
                                 ". Keep the document open and inspect both paths.");
    if (fsync(parent.value) != 0)
        throw std::runtime_error(
            "File was published, but directory flush failed. Keep the document open.");
    const FileSnapshot result = read_at(parent.value, name);
    if (result != candidate)
        throw std::runtime_error("File changed after publication. Keep the document open.");
    return result;
}
} // namespace notepad
