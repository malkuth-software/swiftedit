#include "css_outline.hpp"
#include <algorithm>

namespace swiftedit::highlight {
namespace {

enum class Phase : std::uint8_t {
    idle,               // between statements
    prelude,            // reading a selector, an at-rule prelude or an object header
    declaration_name,   // reading a declaration's name
    declaration_value,  // reading a declaration's value
};

struct RuleStartsAfterOffset final {
    bool operator()(ByteOffset offset, const OutlineRule &rule) const noexcept {
        return offset < rule.prelude.start;
    }
};

struct DeclarationStartsAfterOffset final {
    bool operator()(ByteOffset offset, const OutlineDeclaration &declaration) const noexcept {
        return offset < declaration.whole.start;
    }
};

} // namespace

void CssOutline::build(std::string_view text, const DocumentHighlighter &highlighter) {
    rules_.clear();
    declarations_.clear();
    open_.clear();
    const ByteRange everything{ByteOffset{}, ByteOffset{text.size()}};
    highlighter.tokens_in(everything, tokens_);
    const bool form = highlighter.language().name() == "form";
    const OutlineRuleKind selector_kind = form ? OutlineRuleKind::object : OutlineRuleKind::style_rule;

    Phase phase = Phase::idle;
    OutlineRuleKind prelude_kind = selector_kind;
    ByteRange prelude{};
    OutlineDeclaration pending{};
    bool value_started = false;

    for (const Token &token : tokens_) {
        if (token.kind == TokenKind::comment)
            continue;
        const std::uint32_t enclosing =
            open_.empty() ? no_outline_parent : open_.back();
        const bool in_declaration =
            phase == Phase::declaration_name || phase == Phase::declaration_value;
        if (token.kind == TokenKind::punctuation && token.range.length() == 1U) {
            const char character = text[token.range.start.value];
            if (character == '{') {
                // A nested selector read as a declaration (it spanned lines)
                // becomes the prelude it was.
                if (in_declaration) {
                    prelude = ByteRange{pending.name.start,
                                        value_started ? pending.value.end : pending.name.end};
                    prelude_kind = selector_kind;
                } else if (phase == Phase::idle) {
                    prelude = ByteRange{token.range.start, token.range.start};
                    prelude_kind = selector_kind;
                }
                OutlineRule rule{};
                rule.kind = prelude_kind;
                rule.prelude = prelude;
                rule.block = token.range;
                rule.parent = enclosing;
                rule.depth = static_cast<std::uint32_t>(open_.size());
                rules_.push_back(rule);
                open_.push_back(static_cast<std::uint32_t>(rules_.size() - 1U));
                phase = Phase::idle;
                value_started = false;
                continue;
            }
            if (character == '}') {
                if (in_declaration) {
                    pending.whole.end = value_started ? pending.value.end : pending.name.end;
                    declarations_.push_back(pending);
                    if (pending.rule != no_outline_parent)
                        ++rules_[pending.rule].declaration_count;
                }
                if (!open_.empty()) {
                    OutlineRule &rule = rules_[open_.back()];
                    rule.block.end = token.range.end;
                    rule.closed = true;
                    open_.pop_back();
                }
                phase = Phase::idle;
                value_started = false;
                continue;
            }
            if (character == ';') {
                if (in_declaration) {
                    pending.whole.end = token.range.end;
                    declarations_.push_back(pending);
                    if (pending.rule != no_outline_parent)
                        ++rules_[pending.rule].declaration_count;
                } else if (phase == Phase::prelude && prelude_kind == OutlineRuleKind::at_rule) {
                    OutlineRule rule{};
                    rule.kind = OutlineRuleKind::at_rule;
                    rule.prelude = prelude;
                    rule.block = ByteRange{prelude.end, prelude.end};
                    rule.closed = true;
                    rule.parent = enclosing;
                    rule.depth = static_cast<std::uint32_t>(open_.size());
                    rules_.push_back(rule);
                }
                phase = Phase::idle;
                value_started = false;
                continue;
            }
            if (character == ':' && phase == Phase::declaration_name) {
                phase = Phase::declaration_value;
                pending.value = ByteRange{token.range.end, token.range.end};
                continue;
            }
        }
        switch (phase) {
        case Phase::idle:
            if (token.kind == TokenKind::property || token.kind == TokenKind::custom_property) {
                pending = OutlineDeclaration{};
                pending.name = token.range;
                pending.value = ByteRange{token.range.end, token.range.end};
                pending.whole = token.range;
                pending.rule = enclosing;
                value_started = false;
                phase = Phase::declaration_name;
            } else {
                prelude = token.range;
                prelude_kind = token.kind == TokenKind::at_rule ? OutlineRuleKind::at_rule : selector_kind;
                phase = Phase::prelude;
            }
            break;
        case Phase::prelude:
            prelude.end = token.range.end;
            break;
        case Phase::declaration_name:
            pending.name.end = token.range.end;
            break;
        case Phase::declaration_value:
            if (!value_started) {
                pending.value.start = token.range.start;
                value_started = true;
            }
            pending.value.end = token.range.end;
            if (token.kind == TokenKind::important)
                pending.important = true;
            break;
        }
    }
    if (phase == Phase::declaration_name || phase == Phase::declaration_value) {
        pending.whole.end = value_started ? pending.value.end : pending.name.end;
        declarations_.push_back(pending);
        if (pending.rule != no_outline_parent)
            ++rules_[pending.rule].declaration_count;
    }
    for (const std::uint32_t index : open_) {
        OutlineRule &rule = rules_[index];
        rule.block.end = ByteOffset{text.size()};
        rule.closed = false;
    }
    open_.clear();
}

std::optional<std::size_t> CssOutline::innermost_rule_at(ByteOffset offset) const noexcept {
    // Rules are in prelude order and a nested rule follows its parent, so the
    // last rule starting at or before `offset` whose extent holds it is the
    // innermost.
    const std::vector<OutlineRule>::const_iterator after =
        std::upper_bound(rules_.cbegin(), rules_.cend(), offset, RuleStartsAfterOffset{});
    std::size_t index = static_cast<std::size_t>(after - rules_.cbegin());
    while (index > 0U) {
        --index;
        const ByteRange extent = rules_[index].extent();
        const bool holds = extent.start <= offset && (offset < extent.end || extent.empty());
        if (holds)
            return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> CssOutline::declaration_at(ByteOffset offset) const noexcept {
    const std::vector<OutlineDeclaration>::const_iterator after = std::upper_bound(
        declarations_.cbegin(), declarations_.cend(), offset, DeclarationStartsAfterOffset{});
    if (after == declarations_.cbegin())
        return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(after - declarations_.cbegin()) - 1U;
    if (!declarations_[index].whole.contains(offset))
        return std::nullopt;
    return index;
}

void CssOutline::append_rule_decorations(std::span<const ByteOffset> offsets, DecorationKind kind,
                                         std::vector<Decoration> &out) const {
    const std::size_t first_appended = out.size();
    for (const ByteOffset offset : offsets) {
        const std::optional<std::size_t> rule = innermost_rule_at(offset);
        if (!rule)
            continue;
        const std::uint32_t tag = static_cast<std::uint32_t>(*rule);
        bool seen = false;
        for (std::size_t index = first_appended; index < out.size(); ++index)
            seen = seen || out[index].tag == tag;
        if (seen)
            continue;
        const Decoration decoration{rules_[*rule].extent(), kind, tag};
        out.push_back(decoration);
    }
}

void append_diagnostic_decorations(std::string_view text, const DocumentHighlighter &highlighter,
                                   std::span<const SourceDiagnostic> diagnostics, ColumnUnit unit,
                                   std::vector<Decoration> &out) {
    for (std::size_t index = 0U; index < diagnostics.size(); ++index) {
        const SourceDiagnostic &diagnostic = diagnostics[index];
        if (diagnostic.position.line == 0U)
            continue;
        const ByteOffset offset = highlighter.offset_at(text, diagnostic.position, unit);
        const LineIndex line = highlighter.line_at(offset);
        const ByteOffset line_start = highlighter.line_start(line);
        const ByteOffset line_end = highlighter.line_end(line);
        const Token token = highlighter.token_at(offset);
        ByteRange range{};
        if (token.kind != TokenKind::plain && !token.range.empty())
            range = token.range;
        else if (offset < line_end)
            range = ByteRange{offset, line_end};
        else if (line_end > line_start)
            range = ByteRange{ByteOffset{line_end.value - 1U}, line_end};
        else if (line_end.value < text.size())
            range = ByteRange{line_end, ByteOffset{line_end.value + 1U}};
        else
            continue;
        const DecorationKind kind = diagnostic.severity == DiagnosticSeverity::error
                                        ? DecorationKind::diagnostic_error
                                        : DecorationKind::diagnostic_warning;
        const Decoration decoration{range, kind, static_cast<std::uint32_t>(index)};
        out.push_back(decoration);
    }
}

} // namespace swiftedit::highlight
