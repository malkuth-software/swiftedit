#pragma once
// The CSS family as GUI.Forms writes it: theme sheets (selectors, at-rules,
// declarations, roles and engine functions), form documents (nested objects
// written as `type stable.id { property: value; }`) and inline styles
// (declarations and nested part rules with no enclosing selector).
//
// The lexer is line-at-a-time. Its state carries what crosses a line: an open
// comment, a string continued by a backslash, the brace depth, whether each
// open block holds rules or declarations, the statement context and the
// parenthesis depth. Inside a declaration block a statement is a nested rule
// when a `{` comes before any `;` or `}` on the same line, and a declaration
// otherwise; a nested selector that spans lines is therefore read as a
// declaration until its brace line. That is the one approximation, and it
// only changes colours, never offsets.
#include "language.hpp"

namespace swiftedit::highlight {

enum class CssDialect : std::uint8_t {
    theme_sheet,    // a .css or .theme.css file: rules at the top level
    form_document,  // a .form file: one object at the top level, objects nested
    inline_style,   // Control::set_inline_style text: declarations at the top level
};

class CssLanguage final : public Language {
public:
    explicit CssLanguage(CssDialect dialect) noexcept : dialect_(dialect) {}
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view display_name() const noexcept override;
    [[nodiscard]] LexState initial_state() const noexcept override;
    [[nodiscard]] LexState lex_line(std::string_view line, LexState entry,
                                    std::vector<LineToken> &tokens) const override;
    [[nodiscard]] CssDialect dialect() const noexcept { return dialect_; }

    // The deepest brace nesting whose block kind the state records. Deeper
    // blocks are read as declaration blocks; the depth itself saturates at 63.
    static constexpr std::uint32_t recorded_block_depth = 32U;

private:
    CssDialect dialect_{CssDialect::theme_sheet};
};

// The three dialects, with static storage duration (the registry's entries).
[[nodiscard]] const CssLanguage &css_theme_sheet_language() noexcept;
[[nodiscard]] const CssLanguage &css_form_document_language() noexcept;
[[nodiscard]] const CssLanguage &css_inline_style_language() noexcept;

} // namespace swiftedit::highlight
