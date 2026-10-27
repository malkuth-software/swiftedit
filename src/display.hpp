#pragma once
#include "session.hpp"
#include <optional>

namespace swiftedit {
enum class DisplayKind { text, control, illegal_byte };
struct DisplayUnit {
    SourceRange source{};
    SourceRange display{};
    DisplayKind kind{DisplayKind::text};
};
// Bounded view only. Source bytes remain owned by Session; generated labels are
// never interpreted as source text. Labels, CRLF and complete source graphemes
// are indivisible for edit mapping. Input is a complete bounded source fragment;
// callers must establish surrounding boundary context before projecting pages.
class DisplayPage {
public:
    explicit DisplayPage(std::string_view source);
    const std::string &text() const { return text_; }
    const std::vector<DisplayUnit> &units() const { return units_; }
    [[nodiscard]] SourceRange source_range(SourceRange display) const;
    [[nodiscard]] std::size_t display_offset(std::size_t source_offset) const;

private:
    std::string text_{};
    std::vector<DisplayUnit> units_{};
    std::size_t source_size_{};
};
} // namespace swiftedit
