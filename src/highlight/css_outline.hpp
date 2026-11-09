#pragma once
// The rule structure of a CSS-family document, read from its tokens: each
// rule's prelude (selector, at-rule or form object header) and block, its
// nesting, and each declaration's name and value.
//
// The theme editor uses it to turn what the theme compiler reports (a rule
// or a diagnostic at a line and column) into byte ranges to highlight, and
// to answer "which rule is the caret in". It is rebuilt from the
// highlighter's tokens whenever the text changes; building reuses the
// outline's storage.
#include "decorations.hpp"
#include "highlighter.hpp"
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace swiftedit::highlight {

enum class OutlineRuleKind : std::uint8_t {
    style_rule,  // selector { declarations }
    at_rule,     // @media ... { rules }, @theme { ... }, or @import "x";
    object,      // a form document's `type stable.id { ... }`
};

inline constexpr std::uint32_t no_outline_parent = UINT32_MAX;

struct OutlineRule final {
    OutlineRuleKind kind{OutlineRuleKind::style_rule};
    // From the prelude's first token to its last, before the `{` or `;`.
    ByteRange prelude{};
    // From the `{` to the `}` inclusive. Empty at the prelude's end for an
    // at-rule ended by `;`. An unclosed block runs to the document's end.
    ByteRange block{};
    bool closed{};
    // The enclosing rule's index, or no_outline_parent at the top level.
    std::uint32_t parent{no_outline_parent};
    std::uint32_t depth{};
    // The declarations written directly in this block (each names this rule
    // as its `rule`; nested rules' declarations may come between them).
    std::uint32_t declaration_count{};
    // The whole rule: prelude through block.
    [[nodiscard]] constexpr ByteRange extent() const noexcept {
        const ByteRange result{prelude.start, block.empty() ? prelude.end : block.end};
        return result;
    }
};

struct OutlineDeclaration final {
    ByteRange name{};
    // The value's tokens; empty after the colon when the value is missing.
    ByteRange value{};
    // The name through the `;`, or through the value when no `;` ends it.
    ByteRange whole{};
    // The rule whose block holds it, or no_outline_parent for an inline style.
    std::uint32_t rule{no_outline_parent};
    bool important{};
};

class CssOutline final {
public:
    // Reads the rules of `text` from `highlighter`'s tokens. `highlighter`
    // must be in step with `text`; both are borrowed for the call. Replaces
    // the previous outline.
    void build(std::string_view text, const DocumentHighlighter &highlighter);
    [[nodiscard]] std::span<const OutlineRule> rules() const noexcept { return rules_; }
    [[nodiscard]] std::span<const OutlineDeclaration> declarations() const noexcept {
        return declarations_;
    }
    // The innermost rule whose extent holds `offset`.
    [[nodiscard]] std::optional<std::size_t> innermost_rule_at(ByteOffset offset) const noexcept;
    // The declaration whose whole range holds `offset`.
    [[nodiscard]] std::optional<std::size_t> declaration_at(ByteOffset offset) const noexcept;
    // Appends one decoration of `kind` for each rule an offset falls in (the
    // innermost; each rule once), covering the rule's extent, tagged with the
    // rule's index. A theme engine reports a rule by where its selector
    // starts; any offset inside the rule finds it.
    void append_rule_decorations(std::span<const ByteOffset> offsets, DecorationKind kind,
                                 std::vector<Decoration> &out) const;

private:
    std::vector<OutlineRule> rules_{};
    std::vector<OutlineDeclaration> declarations_{};
    // Reused while building: the tokens, and the open rules' indexes.
    std::vector<Token> tokens_{};
    std::vector<std::uint32_t> open_{};
};

// A diagnostic as the theme editor hands it over from the theme compiler or
// the form compiler: a place, a severity and words.
enum class DiagnosticSeverity : std::uint8_t { error, warning };

struct SourceDiagnostic final {
    SourcePosition position{};
    DiagnosticSeverity severity{DiagnosticSeverity::error};
};

// Appends one decoration per diagnostic, tagged with the diagnostic's index:
// the token at its position, or the rest of its line when it points at a gap,
// or one byte before the line's end when it points past the line. A
// diagnostic with no place (line 0) is not decorated. `text` and
// `highlighter` must be in step; both are borrowed for the call.
void append_diagnostic_decorations(std::string_view text, const DocumentHighlighter &highlighter,
                                   std::span<const SourceDiagnostic> diagnostics, ColumnUnit unit,
                                   std::vector<Decoration> &out);

} // namespace swiftedit::highlight
