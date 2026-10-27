#include "paged_file.hpp"
#include <algorithm>
#include <stdexcept>
#include <windows.h>
namespace swiftedit {
class PagedFileState {
public:
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~PagedFileState() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    PagedFileState() = default;
    PagedFileState(const PagedFileState &) = delete;
    PagedFileState &operator=(const PagedFileState &) = delete;
};
PagedFile::PagedFile(const std::filesystem::path &path) {
    state_ = std::make_unique<PagedFileState>();
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open read-only paged file.");
    (*state_).handle = h;
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL inspected = GetFileInformationByHandle(h, &info);
    if (!inspected ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        throw std::runtime_error("Paged source must be a regular file.");
    }
    size_ = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
PagedFile::~PagedFile() = default;
Page PagedFile::page(std::uint64_t offset, std::size_t n) const {
    if (!n || n > maximum_page)
        throw std::runtime_error("Page budget must be 1..65536 bytes.");
    if (offset > size_)
        throw std::runtime_error("Page offset exceeds file size.");
    Page p{offset, offset, size_, {}};
    p.bytes.resize(static_cast<std::size_t>(std::min<std::uint64_t>(n, size_ - offset)));
    LARGE_INTEGER at{};
    at.QuadPart = static_cast<LONGLONG>(offset);
    const BOOL sought = SetFilePointerEx((*state_).handle, at, nullptr, FILE_BEGIN);
    if (!sought)
        throw std::runtime_error("Cannot seek paged source.");
    DWORD got{};
    const DWORD requested = static_cast<DWORD>(p.bytes.size());
    const BOOL read = ReadFile((*state_).handle, p.bytes.data(), requested, &got, nullptr);
    if (!read || got != requested)
        throw std::runtime_error("Paged read failed; no document changed.");
    p.next += got;
    return p;
}
} // namespace swiftedit
