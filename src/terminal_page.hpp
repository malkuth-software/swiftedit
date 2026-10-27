#pragma once
#include "session.hpp"
#include <deque>
#include <optional>
#include <memory>
namespace swiftedit {
struct TerminalPageCursor {
    std::uint64_t offset{};
    std::size_t label_cell{};
    bool after_wrap{};
    bool operator==(const TerminalPageCursor &other) const {
        const bool equal = offset == other.offset && label_cell == other.label_cell &&
                           after_wrap == other.after_wrap;
        return equal;
    }
};
struct TerminalPageRun {
    std::size_t row{}, column{};
    std::uint64_t source_offset{}, source_length{};
    std::string text{};
    std::size_t cells{};
    bool starts_grapheme{}, ends_grapheme{};
};
struct TerminalPageNewlineCaret {
    std::uint64_t offset{};
    std::size_t row{}, column{};
};
struct TerminalPageFrame {
    std::vector<TerminalPageRun> runs{};
    std::vector<TerminalPageCursor> row_starts{};
    std::vector<SourceRange> graphemes{};
    std::vector<TerminalPageNewlineCaret> newline_carets{};
    TerminalPageCursor next{};
    bool more{};
};
struct TerminalPageCaret {
    std::size_t row{}, column{};
};
[[nodiscard]] std::optional<std::uint64_t> terminal_page_horizontal(
    const TerminalPageFrame &, std::uint64_t caret, bool right);
[[nodiscard]] std::optional<TerminalPageCaret> terminal_page_caret(
    const TerminalPageFrame &, std::uint64_t caret, std::size_t width, std::size_t rows);
// Returns the nearest legal source boundary at or before the desired display
// column, or the first legal boundary on that row. Never splits a source unit.
[[nodiscard]] std::optional<std::uint64_t> terminal_page_target(
    const TerminalPageFrame &, std::size_t row, std::size_t column,
    std::size_t width, std::size_t rows);
// Starts with 8 KiB of context, doubling up to 64 KiB only to fill the viewport
// or complete a grapheme (at most 120 KiB total reads across retries). Never
// treats an unfinished trailing grapheme as complete. Source-independent
// labels may continue across rows/pages.
[[nodiscard]] TerminalPageFrame terminal_page(const Session &, TerminalPageCursor,
                                              std::size_t width, std::size_t rows);
// Rows prefer spaces/tabs without removing source bytes. Reconstruction carries
// the last word break across reads and may rewind an unfinished word.
// Normally reads 8 KiB per step. Incomplete first graphemes grow context up to
// 64 KiB (at most 120 KiB total reads for the default adaptive step). Retains the final
// viewport and at most 32768 preceding row cursors for backward navigation.
// The caller owns cancellation by dropping this task.
class TerminalPageEnd final {
public:
    TerminalPageEnd(const Session &, std::size_t width, std::size_t rows,
                    std::size_t source_budget = 8192);
    // Reconstruct the rows immediately before an existing visual row cursor.
    TerminalPageEnd(const Session &, std::size_t width, std::size_t rows,
                    TerminalPageCursor before);
    [[nodiscard]] bool step(const Session &);
    [[nodiscard]] TerminalPageCursor result(const Session &) const;
private:
    friend class TerminalPager;
    void validate(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{};
    std::size_t width_{}, rows_{};
    std::size_t source_budget_{}, column_{};
    std::optional<TerminalPageCursor> word_break_{};
    TerminalPageCursor cursor_{};
    std::optional<TerminalPageCursor> before_{};
    std::deque<TerminalPageCursor> tail_{};
    bool complete_{};
};
// Logical-line navigation from a proven source grapheme boundary. Each step
// reads at most 8 KiB; cancellation drops the owner without publishing a caret.
class TerminalLineBoundary final {
public:
    TerminalLineBoundary(const Session &, std::uint64_t caret, bool end);
    [[nodiscard]] bool step(const Session &);
    [[nodiscard]] std::uint64_t result(const Session &) const;
private:
    void validate(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{}, cursor_{};
    bool end_{}, complete_{};
};
class TerminalPager {
public:
    void reset(const Session &);
    const TerminalPageFrame &frame(const Session &, std::size_t width, std::size_t rows);
    void next();
    void previous();
    void down();
    void up();
    void first();
    // Caller supplies a source grapheme boundary from a completed search.
    void reveal(const Session &, std::uint64_t offset);
    // Returns work only when retained history cannot satisfy Up/Page Up.
    // The caller must retain the viewport until successful finish_end publication.
    [[nodiscard]] std::unique_ptr<TerminalPageEnd> prepare_previous(const Session &, bool page) const;
    void finish_end(const Session &, const TerminalPageEnd &);
    [[nodiscard]] std::uint64_t source_offset() const { return cursor_.offset; }

private:
    void retain(TerminalPageCursor);
    DocumentStamp stamp_{};
    TerminalPageCursor cursor_{};
    std::deque<TerminalPageCursor> history_{};
    std::deque<TerminalPageCursor> page_history_{};
    TerminalPageFrame frame_{};
    std::size_t width_{}, rows_{};
    bool ready_{};
};
} // namespace swiftedit
