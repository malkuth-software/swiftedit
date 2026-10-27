#pragma once
#include "terminal_buffer.hpp"
#include "terminal_cells.hpp"
namespace swiftedit {
struct TerminalRun {
    std::size_t column{}, cells{};
    std::string text{};
    SourceRange source{};
    bool selected{};
};
struct TerminalRow {
    std::vector<TerminalRun> runs{};
    std::optional<std::size_t> caret_column{};
    std::size_t total_cells{};
    bool clipped_left{}, clipped_right{};
};
// Logical-line viewport, with horizontal clipping in cells. Never slices a
// printable source grapheme. A partially visible wide glyph occupies blanks.
// Labels may be visually clipped; their source/edit range stays atomic.
[[nodiscard]] TerminalRow terminal_row(TerminalBuffer &, std::size_t line, std::size_t first_column,
                                       std::size_t width);
// Sparse cell/source indexes for up to 320 logical lines (the host shows at most
// 296). Entries are bound to document identity/revision. A checkpoint every 256
// graphemes bounds retained index storage to roughly 1 MiB for editable files.
// Preparation is synchronous; warm viewport and caret queries scan at most one
// checkpoint interval plus visible content. No source or selection is retained.
class TerminalRowCache {
public:
    [[nodiscard]] TerminalRow row(TerminalBuffer &, std::size_t line, std::size_t first_column,
                                  std::size_t width);
    [[nodiscard]] std::size_t source_column(TerminalBuffer &, std::size_t line,
                                            std::size_t source_offset);

private:
    struct Point {
        std::size_t offset{}, column{};
    };
    struct Entry {
        std::size_t line{}, cells{};
        SourceRange source{};
        std::vector<Point> points{};
    };
    Entry &prepare(TerminalBuffer &, std::size_t line);
    DocumentStamp stamp_{};
    std::vector<Entry> entries_{};
    std::size_t next_eviction_{};
};
} // namespace swiftedit
