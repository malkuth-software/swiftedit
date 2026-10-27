#pragma once
#include "terminal_logical_page.hpp"
#include "terminal_page.hpp"

namespace swiftedit {
struct TerminalHorizontalCaret {
    std::uint64_t source_offset{}, column{};
};
struct TerminalHorizontalFrame {
    std::vector<TerminalPageRun> runs{};
    // Absolute logical-line columns at legal source grapheme boundaries.
    // Edge guards may lie outside the visible horizontal interval.
    std::vector<TerminalHorizontalCaret> carets{};
    std::optional<std::uint64_t> total_cells{};
    // Requested source caret's absolute logical column, even off screen.
    std::optional<std::uint64_t> source_caret_column{};
    bool clipped_left{}, clipped_right{};
};
// Renders one row from a completed, current logical page. Reads at most 8 KiB
// per step and retains at most 64 KiB of source context for a complete grapheme.
// Optional source-caret lookup continues beyond the visible right edge until
// that boundary is located, retaining only visible runs. A caret splitting a
// grapheme is refused. Dropping the owner cancels work; no partial publication.
class TerminalHorizontalLine final {
public:
    TerminalHorizontalLine(const Session &, const TerminalLogicalPage &, std::size_t row,
                           std::uint64_t left, std::size_t width,
                           std::optional<std::uint64_t> source_caret = std::nullopt);
    [[nodiscard]] bool step(const Session &, std::size_t budget = 8192);
    // Borrows the completed frame until task destruction; revalidate source
    // identity before a later viewport publication.
    [[nodiscard]] const TerminalHorizontalFrame &result(const Session &) const;
    [[nodiscard]] std::uint64_t read_offset() const { return read_offset_; }

private:
    friend class TerminalHorizontalPage;
    void validate(const Session &) const;
    void retain_caret(std::uint64_t offset, std::uint64_t column);
    void skip_offscreen_ascii();
    void finish_preparation();
    void prepare();
    DocumentStamp stamp_{};
    std::uint64_t size_{}, offset_{}, read_offset_{}, end_{}, column_{}, left_{}, right_{};
    std::string source_{};
    TerminalHorizontalFrame frame_{};
    std::optional<std::uint64_t> source_caret_{};
    bool complete_{}, failed_{};
};
// Cooperative no-wrap viewport preparation. Each step advances either the
// logical scan or one row renderer, never both. No partial viewport is exposed.
// Result references borrow this owner; dropping it cancels unfinished work.
class TerminalHorizontalPage final {
public:
    TerminalHorizontalPage(const Session &, std::uint64_t start, std::size_t rows,
                           std::uint64_t left, std::size_t width);
    // Reuses only a completed, current viewport's proven row/grapheme metadata.
    // Copies retained metadata; does not borrow the previous task.
    TerminalHorizontalPage(const Session &, const TerminalHorizontalPage &previous,
                           std::uint64_t left, std::size_t width);
    [[nodiscard]] bool step(const Session &, std::size_t budget = 8192);
    [[nodiscard]] const std::vector<TerminalHorizontalFrame> &result(const Session &) const;
    [[nodiscard]] const std::vector<TerminalLogicalRow> &rows(const Session &) const;
    [[nodiscard]] std::uint64_t next(const Session &) const;
    [[nodiscard]] bool more(const Session &) const;

private:
    void validate(const Session &) const;
    void require_complete(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{}, left_{};
    std::size_t width_{};
    TerminalLogicalPage logical_;
    std::unique_ptr<TerminalHorizontalLine> line_{};
    std::vector<TerminalHorizontalFrame> frames_{};
    std::vector<TerminalHorizontalFrame> seed_frames_{};
    bool indexed_{}, complete_{}, failed_{};
};
} // namespace swiftedit
