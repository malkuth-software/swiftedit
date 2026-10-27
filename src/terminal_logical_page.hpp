#pragma once
#include "session.hpp"
#include <vector>

namespace swiftedit {
// Reconstruct earlier logical rows independently of wrap width or retained
// screen history. One read of at most 8 KiB per step, constant retained state.
// Start must be a complete logical-line boundary; result clamps at source start.
class TerminalLogicalPrevious final {
public:
    TerminalLogicalPrevious(const Session &, std::uint64_t start, std::size_t rows);
    [[nodiscard]] bool step(const Session &, std::size_t budget = 8192);
    [[nodiscard]] std::uint64_t result(const Session &) const;
    [[nodiscard]] std::uint64_t scanned_offset() const { return scan_; }

private:
    void validate(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{}, start_{}, scan_{}, result_{};
    std::size_t remaining_{};
    char following_{};
    bool complete_{};
};
struct TerminalLogicalRow {
    std::uint64_t offset{}, length{};
    std::size_t separator_bytes{};
};
// Discovers a bounded viewport of logical lines without retaining their text.
// Start must be a logical-line boundary. Cancellation drops this owner; no
// Session or visible viewport is changed. Construction reads at most two bytes
// to validate a nonzero start; each step reads at most 8 KiB.
class TerminalLogicalPage final {
public:
    TerminalLogicalPage(const Session &, std::uint64_t start, std::size_t rows);
    [[nodiscard]] bool step(const Session &, std::size_t budget = 8192);
    // Result borrows this completed task's immutable rows until destruction.
    [[nodiscard]] const std::vector<TerminalLogicalRow> &result(const Session &) const;
    [[nodiscard]] std::uint64_t next(const Session &) const;
    [[nodiscard]] bool more(const Session &) const;
    [[nodiscard]] std::uint64_t scanned_offset() const { return scan_; }

private:
    void validate(const Session &) const;
    void require_complete(const Session &) const;
    void finish_row(std::size_t separator_bytes);
    DocumentStamp stamp_{};
    std::uint64_t size_{}, scan_{}, line_start_{};
    std::size_t limit_{};
    std::vector<TerminalLogicalRow> rows_{};
    bool pending_cr_{}, complete_{}, more_{};
};
} // namespace swiftedit
