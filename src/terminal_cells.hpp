#pragma once
#include <cstddef>
#include <string>
#include <string_view>
namespace swiftedit {
struct TerminalGlyph {
    std::string text{};
    std::size_t cells{};
    bool label{};
};
// Input is one source grapheme, or one malformed byte. Output has no executable
// terminal controls. Tabs expand to four-cell stops; source remains untouched.
// Width policy: Unicode 17 W/F=2, ambiguous=1, marks=0, emoji clusters=2.
// Terminal font/shaping implementations still require native compatibility QA.
[[nodiscard]] TerminalGlyph terminal_glyph(std::string_view, std::size_t column);
// Width-only path for a proven standalone ASCII grapheme. Non-ASCII is refused.
[[nodiscard]] std::size_t terminal_ascii_cells(unsigned char byte, std::size_t column);
} // namespace swiftedit
