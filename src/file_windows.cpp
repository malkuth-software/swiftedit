#include "document.hpp"
#include "new_file_writer.hpp"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <windows.h>

namespace notepad {
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE h) noexcept : value(h) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
[[noreturn]] void fail(const char *action, DWORD error) {
    const std::string error_text = std::to_string(error);
    const std::string message = std::string(action) + " (Windows error " + error_text +
                                "). The document remains in memory.";
    throw std::runtime_error(message);
}
[[noreturn]] void fail(const char *action) {
    const DWORD error = GetLastError();
    fail(action, error);
}
std::uint64_t pair(DWORD high, DWORD low) {
    const std::uint64_t value = (static_cast<std::uint64_t>(high) << 32) | low;
    return value;
}
FileSnapshot snapshot(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL inspected = GetFileInformationByHandle(handle, &info);
    if (!inspected)
        fail("Cannot inspect file");
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error(
            "Links, reparse points and directories are not supported. Choose an ordinary file.");
    if (info.nNumberOfLinks != 1)
        throw std::runtime_error(
            "Files with multiple hard links are not supported by this save policy.");
    const std::uint64_t file_size = pair(info.nFileSizeHigh, info.nFileSizeLow);
    if (file_size > maximum_bytes)
        throw std::runtime_error("This build supports files up to 16 MiB.");
    FileSnapshot result{
        true,
        info.dwVolumeSerialNumber,
        pair(info.nFileIndexHigh, info.nFileIndexLow),
        pair(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime),
        {}};
    LARGE_INTEGER zero{};
    const BOOL positioned = SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN);
    if (!positioned)
        fail("Cannot seek file");
    result.bytes.resize(info.nFileSizeLow);
    if (!result.bytes.empty()) {
        const DWORD requested = static_cast<DWORD>(result.bytes.size());
        DWORD read{};
        const BOOL read_succeeded =
            ReadFile(handle, result.bytes.data(), requested, &read, nullptr);
        if (!read_succeeded)
            fail("Cannot read complete file");
        if (read != requested)
            fail("Cannot read complete file");
    }
    BY_HANDLE_FILE_INFORMATION after{};
    const BOOL revalidated = GetFileInformationByHandle(handle, &after);
    if (!revalidated)
        fail("Cannot revalidate file");
    const bool size_changed =
        info.nFileSizeLow != after.nFileSizeLow || info.nFileSizeHigh != after.nFileSizeHigh;
    LONG time_comparison{};
    if (!size_changed)
        time_comparison = CompareFileTime(&info.ftLastWriteTime, &after.ftLastWriteTime);
    if (size_changed || time_comparison != 0)
        throw std::runtime_error("The file changed while it was being read. Try opening it again.");
    return result;
}
void validate_path(const std::filesystem::path &path) {
    if (path.empty() || !path.is_absolute())
        throw std::runtime_error("An absolute file path is required.");
    // Alternate streams are outside the first editor's file contract.
    const std::wstring text = path.native();
    if (text.find(L':', 2) != std::wstring::npos)
        throw std::runtime_error("Alternate data stream paths are unsupported.");
    for (std::filesystem::path parent = path.parent_path(); !parent.empty();) {
        const DWORD attrs = GetFileAttributesW(parent.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            fail("Cannot inspect parent directory");
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error(
                "Paths through reparse-point directories are unsupported by this build.");
        std::filesystem::path next = parent.parent_path();
        if (next == parent)
            break;
        parent = std::move(next);
    }
}
} // namespace
class NewFileWriterState final {
public:
    explicit NewFileWriterState(const std::filesystem::path &path) : destination(path) {
        validate_path(path);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Text copy destination already exists.");
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND)
            fail("Cannot inspect text copy destination", error);
        static std::atomic<unsigned long> sequence{};
        for (int attempt = 0; attempt < 100; ++attempt) {
            const unsigned long ordinal = ++sequence;
            const std::wstring filename = L".swiftedit-copy-" + std::to_wstring(GetCurrentProcessId()) +
                                          L"-" + std::to_wstring(ordinal) + L".tmp";
            const std::filesystem::path temporary = path.parent_path() / filename;
            prepared.value = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (prepared.value != INVALID_HANDLE_VALUE)
                return;
            const DWORD create_error = GetLastError();
            if (create_error != ERROR_FILE_EXISTS && create_error != ERROR_ALREADY_EXISTS)
                fail("Cannot create text copy temporary", create_error);
        }
        throw std::runtime_error("Cannot reserve text copy temporary.");
    }
    ~NewFileWriterState() {
        if (!published && prepared.value != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(prepared.value, FileDispositionInfo, &disposition, sizeof(disposition));
        }
    }
    std::filesystem::path destination{};
    Handle prepared{INVALID_HANDLE_VALUE};
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
        DWORD written = 0;
        const DWORD requested = static_cast<DWORD>(bytes.size() - offset);
        if (!WriteFile(state.prepared.value, bytes.data() + offset, requested, &written, nullptr) || !written) {
            state.failed = true;
            fail("Cannot write text copy temporary");
        }
        offset += written;
    }
}
void NewFileWriter::publish() {
    NewFileWriterState &state = *state_;
    if (state.published || state.failed)
        throw std::runtime_error("Text copy writer cannot publish again.");
    const std::wstring &name = state.destination.native();
    if (name.size() > (MAXDWORD - sizeof(FILE_RENAME_INFO)) / sizeof(wchar_t))
        throw std::runtime_error("Text copy destination name is too long.");
    const std::size_t name_bytes = name.size() * sizeof(wchar_t);
    const std::size_t storage_bytes = sizeof(FILE_RENAME_INFO) + name_bytes;
    const std::size_t words = (storage_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
    std::vector<std::max_align_t> storage(words);
    FILE_RENAME_INFO &rename = *reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
    rename.ReplaceIfExists = FALSE;
    rename.RootDirectory = nullptr;
    rename.FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(rename.FileName, name.data(), name_bytes);
    state.failed = true;
    if (!FlushFileBuffers(state.prepared.value))
        fail("Cannot flush text copy temporary");
    if (!SetFileInformationByHandle(state.prepared.value, FileRenameInfo, &rename,
                                    static_cast<DWORD>(storage_bytes)))
        fail("Cannot publish text copy without overwriting");
    state.published = true;
}
FileSnapshot read_file(const std::filesystem::path &path) {
    validate_path(path);
    const HANDLE opened =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Handle file(opened);
    if (file.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            return {};
        fail("Cannot open file", error);
    }
    const FileSnapshot result = snapshot(file.value);
    return result;
}
FileSnapshot write_file(const std::filesystem::path &path, std::string_view bytes,
                        const FileSnapshot &expected) {
    if (bytes.size() > maximum_bytes)
        throw std::runtime_error("Output exceeds 16 MiB.");
    validate_path(path);
    // Keep a no-write-sharing read handle alive through publication. Existing
    // incompatible writers cause refusal rather than a last-writer-wins save.
    const HANDLE opened =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD open_error = GetLastError();
    Handle current(opened);
    FileSnapshot actual{};
    if (current.value == INVALID_HANDLE_VALUE) {
        if (open_error != ERROR_FILE_NOT_FOUND)
            fail("Cannot validate save destination", open_error);
    } else
        actual = snapshot(current.value);
    if (actual != expected)
        throw std::runtime_error("The destination changed outside SwiftEdit. Open it again or use "
                                 "Save As to another name. Nothing was overwritten.");
    if (actual.exists) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes & FILE_ATTRIBUTE_READONLY)
            throw std::runtime_error("The destination is read-only. Use Save As to another name.");
    }
    static std::atomic<unsigned long> sequence{};
    const std::filesystem::path parent = path.parent_path();
    const DWORD process_number = GetCurrentProcessId();
    const std::wstring process_id = std::to_wstring(process_number);
    std::filesystem::path temporary{};
    HANDLE created = INVALID_HANDLE_VALUE;
    DWORD reservation_error = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const unsigned long ordinal = ++sequence;
        const std::wstring sequence_text = std::to_wstring(ordinal);
        const std::wstring filename = L".notepad-" + process_id + L"-" + sequence_text + L".tmp";
        temporary = parent / filename;
        created = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (created != INVALID_HANDLE_VALUE)
            break;
        reservation_error = GetLastError();
        if (reservation_error != ERROR_FILE_EXISTS)
            fail("Cannot create sibling temporary file", reservation_error);
    }
    if (created == INVALID_HANDLE_VALUE)
        fail("Cannot reserve temporary filename", reservation_error);
    struct Cleanup {
        // Borrow the stable local path without allocation after acquisition.
        // temporary outlives this guard and is not mutated after reservation.
        // The inner Handle closes before this guard removes the temporary file.
        const std::filesystem::path &path;
        explicit Cleanup(const std::filesystem::path &source) noexcept : path(source) {}
        Cleanup(const Cleanup &) = delete;
        Cleanup &operator=(const Cleanup &) = delete;
        ~Cleanup() { DeleteFileW(path.c_str()); }
    } cleanup{temporary};
    {
        Handle temp(created);
        const DWORD requested = static_cast<DWORD>(bytes.size());
        DWORD written{};
        const BOOL write_succeeded =
            WriteFile(temp.value, bytes.data(), requested, &written, nullptr);
        if (!write_succeeded)
            fail("Cannot write complete temporary file");
        if (written != requested)
            fail("Cannot write complete temporary file");
        const BOOL flushed = FlushFileBuffers(temp.value);
        if (!flushed)
            fail("Cannot flush temporary file");
    }
    // The visible name must still identify the exact original snapshot.
    const FileSnapshot before_publication = read_file(path);
    if (before_publication != expected)
        throw std::runtime_error(
            "The destination changed before publication. Nothing was overwritten.");
    if (actual.exists) {
        // ReplaceFile preserves the original file's security and named streams.
        // A backup also keeps original bytes recoverable on unusual OS failure.
        std::filesystem::path backup = temporary;
        backup += L".previous";
        const DWORD backup_attributes = GetFileAttributesW(backup.c_str());
        if (backup_attributes != INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("A recovery filename already exists; save refused.");
        const BOOL replaced =
            ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr);
        if (!replaced) {
            const DWORD error = GetLastError();
            throw std::runtime_error("Windows could not replace the file (error " +
                                     std::to_string(error) +
                                     "). Original bytes may be retained in " + backup.string() +
                                     ". Keep this document open and use Save As.");
        }
        DeleteFileW(backup.c_str());
    } else {
        const BOOL published = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
        if (!published)
            fail("Cannot publish new file without overwriting");
    }
    const FileSnapshot result = read_file(path);
    return result;
}
} // namespace notepad
