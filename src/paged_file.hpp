#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
namespace swiftedit {
constexpr std::size_t maximum_page = 64 * 1024;
struct Page {
    std::uint64_t offset{}, next{}, size{};
    std::string bytes{};
};
class PagedFileState;
// Retains an ordinary file handle, never a whole-file allocation. Windows denies
// write sharing. POSIX detects metadata changes before/after each bounded read;
// it cannot prevent an uncooperative external writer from modifying the file.
class PagedFile {
public:
    explicit PagedFile(const std::filesystem::path &);
    ~PagedFile();
    PagedFile(const PagedFile &) = delete;
    PagedFile &operator=(const PagedFile &) = delete;
    [[nodiscard]] Page page(std::uint64_t offset, std::size_t budget) const;
    [[nodiscard]] std::uint64_t size() const { return size_; }

private:
    std::unique_ptr<PagedFileState> state_{};
    std::uint64_t size_{};
};
} // namespace swiftedit
