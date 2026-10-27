#pragma once
#include "document.hpp"
#include "paged_file.hpp"
#include <vector>

namespace swiftedit {
enum class DecodedFileState { preparing, ready, cancelled, failed };
// Executor-confined incremental read-only source. Each step reads <=64 KiB
// encoded bytes and retains one sparse scalar-boundary checkpoint. No complete
// decoded file is retained. Destroy or cancel before adoption to abandon open.
// page offsets and size are logical UTF-8 bytes; encoded offsets stay private.
class DecodedPagedFile final {
public:
    explicit DecodedPagedFile(const std::filesystem::path &);
    DecodedPagedFile(const DecodedPagedFile &) = delete;
    DecodedPagedFile &operator=(const DecodedPagedFile &) = delete;
    void step();
    void cancel() noexcept;
    [[nodiscard]] DecodedFileState state() const { return state_; }
    [[nodiscard]] std::uint64_t scanned() const { return encoded_offset_; }
    [[nodiscard]] std::uint64_t encoded_size() const { return encoded_size_; }
    // Final only when ready; while preparing, this is the validated prefix size.
    [[nodiscard]] std::uint64_t size() const { return logical_size_; }
    [[nodiscard]] notepad::Encoding encoding() const { return encoding_; }
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }
    [[nodiscard]] Page page(std::uint64_t offset, std::size_t budget) const;
private:
    struct Checkpoint { std::uint64_t encoded{}, logical{}; };
    struct Block { std::uint64_t next{}; std::string text{}; };
    [[nodiscard]] Block decode_block(std::uint64_t offset) const;
    std::filesystem::path path_{};
    std::unique_ptr<PagedFile> file_{};
    std::vector<Checkpoint> checkpoints_{};
    notepad::Encoding encoding_{notepad::Encoding::utf8};
    DecodedFileState state_{DecodedFileState::preparing};
    std::uint64_t encoded_size_{}, encoded_offset_{}, logical_size_{};
};
}
