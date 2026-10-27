#pragma once
#include "terminal_row.hpp"

namespace swiftedit {
struct TerminalWrapSpan {
    SourceRange source{};
    bool logical_end{};
};
struct TerminalWrappedRow {
    TerminalWrapSpan span{};
    TerminalRow display{};
};
// One visual row, starting at a source grapheme within the requested logical
// line. Spaces/tabs are preferred break positions and remain in source order.
// At a full-width logical end, one empty continuation row owns the end caret.
// Queries never modify Session. Navigation metadata is lazily prepared by the
// buffer; this API does not claim interruptible first-time Unicode indexing.
[[nodiscard]] TerminalWrapSpan terminal_wrap_span(TerminalBuffer &, std::size_t line,
                                                  std::size_t start, std::size_t width);
[[nodiscard]] TerminalWrappedRow terminal_wrapped_row(TerminalBuffer &, std::size_t line,
                                                      std::size_t start, std::size_t width);
// Maps a desired display cell to a caret owned by this visual row. A cell
// inside a wide glyph/tab/control label maps to that whole source grapheme's
// start. Past-row columns clamp to the last caret owned by this row, so a soft
// boundary never accidentally moves the caret onto the following visual row.
[[nodiscard]] std::size_t terminal_wrap_source(TerminalBuffer &, std::size_t line,
                                               std::size_t start, std::size_t width,
                                               std::size_t column);
} // namespace swiftedit
