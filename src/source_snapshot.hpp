#pragma once
#include "session.hpp"

namespace swiftedit {
struct SnapshotRange {
    std::uint64_t offset{}, length{};
};
struct SnapshotLimits {
    std::uint64_t bytes{};
    std::size_t parts{}, chunks{};
};
struct SnapshotSlice {
    std::uint64_t source_offset{};
    std::string bytes{};
};
// Immutable owned source, partitioned explicitly rather than joining unrelated
// selections. Concurrent readers require the owner to remain alive. No Session,
// native layout or mutable file handle is retained after capture.
class SourceSnapshot final {
public:
    SourceSnapshot(const SourceSnapshot &) = delete;
    SourceSnapshot &operator=(const SourceSnapshot &) = delete;
    [[nodiscard]] DocumentStamp stamp() const { return stamp_; }
    [[nodiscard]] std::uint64_t source_size() const { return source_size_; }
    [[nodiscard]] std::uint64_t bytes() const { return bytes_; }
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }
    [[nodiscard]] const std::vector<SnapshotRange> &parts() const { return parts_; }
    [[nodiscard]] SnapshotSlice read(std::size_t part, std::uint64_t relative,
                                     std::size_t budget = 4096) const;
private:
    friend class SnapshotCapture;
    SourceSnapshot() = default;
    DocumentStamp stamp_{};
    std::uint64_t source_size_{}, bytes_{};
    std::filesystem::path path_{};
    std::vector<SnapshotRange> parts_{};
    std::vector<std::vector<std::string>> storage_{};
};
enum class SnapshotState { reading, complete, cancelled, failed, taken };
// Runs on the Session's executor. Each step copies <=64 KiB into fixed-size
// owned chunks. Quotas are supplied by the caller, not product defaults. Data
// bytes and chunk/part metadata are charged separately before allocation.
class SnapshotCapture final {
public:
    SnapshotCapture(const Session &, const std::vector<SnapshotRange> &, SnapshotLimits);
    SnapshotCapture(const SnapshotCapture &) = delete;
    SnapshotCapture &operator=(const SnapshotCapture &) = delete;
    void step(const Session &, std::size_t budget = 8192);
    void cancel() noexcept;
    [[nodiscard]] SnapshotState state() const { return state_; }
    [[nodiscard]] std::uint64_t copied() const { return copied_; }
    [[nodiscard]] std::unique_ptr<const SourceSnapshot> take(const Session &);
private:
    void validate(const Session &) const;
    void advance_empty_parts();
    std::unique_ptr<SourceSnapshot> pending_{};
    std::size_t part_{};
    std::uint64_t part_copied_{}, copied_{};
    SnapshotState state_{SnapshotState::reading};
};
} // namespace swiftedit
