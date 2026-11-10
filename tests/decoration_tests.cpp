// Decorations, style-run composition and the CSS outline: the pieces the
// theme editor builds on (rule highlighting, compile-error markers, search
// matches).
#include "highlight/css_language.hpp"
#include "highlight/css_outline.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace hl = swiftedit::highlight;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

hl::ByteRange range(std::size_t start, std::size_t end) {
    const hl::ByteRange result{hl::ByteOffset{start}, hl::ByteOffset{end}};
    return result;
}

hl::DecorationMask mask_of(std::initializer_list<hl::DecorationKind> kinds) {
    hl::DecorationMask result{};
    for (const hl::DecorationKind kind : kinds)
        result.add(kind);
    return result;
}

void composition_tests() {
    const std::string text = "a { b: c }";
    hl::DocumentHighlighter highlighter(hl::css_theme_sheet_language());
    highlighter.reset(text);
    std::vector<hl::Token> tokens{};
    highlighter.tokens_in(range(0U, text.size()), tokens);
    hl::StyleComposer composer{};
    std::vector<hl::StyleRun> runs{};
    composer.compose(range(0U, text.size()), tokens, {}, runs);
    check(runs.size() == 10U && runs.front().range == range(0U, 1U) &&
              runs.front().syntax == hl::TokenKind::type_selector &&
              runs[1].syntax == hl::TokenKind::plain && runs.back().range == range(9U, 10U),
          "Runs cover the window exactly, gaps as plain");

    hl::DecorationLayer rules{};
    const std::array<hl::Decoration, 1U> rule_items{
        hl::Decoration{range(0U, 10U), hl::DecorationKind::rule_match, 0U}};
    rules.replace(rule_items);
    hl::DecorationLayer search{};
    const std::array<hl::Decoration, 2U> search_items{
        hl::Decoration{range(7U, 8U), hl::DecorationKind::search_current, 1U},
        hl::Decoration{range(4U, 8U), hl::DecorationKind::search_match, 0U}};
    search.replace(search_items);
    check(search.items()[0].range == range(4U, 8U), "A layer keeps its decorations in start order");
    const std::array<const hl::DecorationLayer *, 2U> layers{&rules, &search};
    composer.compose(range(0U, text.size()), tokens, layers, runs);
    check(runs.size() == 10U, "Decorations inside token bounds add no runs");
    check(runs[4].range == range(4U, 5U) && runs[4].syntax == hl::TokenKind::property &&
              runs[4].decorations ==
                  mask_of({hl::DecorationKind::rule_match, hl::DecorationKind::search_match}),
          "A run carries every decoration over it");
    check(runs[7].range == range(7U, 8U) &&
              runs[7].decorations == mask_of({hl::DecorationKind::rule_match,
                                              hl::DecorationKind::search_match,
                                              hl::DecorationKind::search_current}),
          "Overlapping decorations combine");
    check(runs[8].decorations == mask_of({hl::DecorationKind::rule_match}),
          "A decoration ends where its range ends");

    composer.compose(range(5U, 8U), tokens, layers, runs);
    check(runs.size() == 3U && runs[0].range == range(5U, 6U) && runs[2].range == range(7U, 8U),
          "A partial window clips tokens and decorations");
    composer.compose(range(5U, 5U), tokens, layers, runs);
    check(runs.empty(), "An empty window has no runs");

    const std::string plain = "hello";
    hl::DecorationLayer matches{};
    const std::array<hl::Decoration, 2U> adjacent{
        hl::Decoration{range(1U, 3U), hl::DecorationKind::search_match, 0U},
        hl::Decoration{range(3U, 4U), hl::DecorationKind::search_match, 1U}};
    matches.replace(adjacent);
    const std::array<const hl::DecorationLayer *, 1U> one_layer{&matches};
    const std::vector<hl::Token> no_tokens{};
    composer.compose(range(0U, plain.size()), no_tokens, one_layer, runs);
    check(runs.size() == 3U && runs[1].range == range(1U, 4U), "Equal adjacent runs merge");
}

void layer_tests() {
    hl::DecorationLayer layer{};
    const std::array<hl::Decoration, 3U> items{
        hl::Decoration{range(2U, 5U), hl::DecorationKind::diagnostic_error, 0U},
        hl::Decoration{range(10U, 12U), hl::DecorationKind::diagnostic_error, 1U},
        hl::Decoration{range(6U, 8U), hl::DecorationKind::diagnostic_warning, 2U}};
    layer.replace(items);
    layer.shift_for_edit(hl::TextEdit{hl::ByteOffset{6U}, 2U, 3U});
    check(layer.items().size() == 2U && layer.items()[0].range == range(2U, 5U) &&
              layer.items()[1].range == range(11U, 13U),
          "An edit shifts later decorations and drops removed ones");
    layer.shift_for_edit(hl::TextEdit{hl::ByteOffset{4U}, 2U, 0U});
    check(layer.items()[0].range == range(2U, 4U), "A decoration the edit cuts keeps its outer bytes");
    check(layer.next_after(hl::ByteOffset{5U}) == 1U && layer.next_after(hl::ByteOffset{9U}) == 0U,
          "Next wraps to the first");
    check(layer.previous_before(hl::ByteOffset{9U}) == 0U &&
              layer.previous_before(hl::ByteOffset{0U}) == 1U,
          "Previous wraps to the last");

    bool refused = false;
    try {
        const std::array<hl::Decoration, 1U> backwards{
            hl::Decoration{range(5U, 2U), hl::DecorationKind::rule_match, 0U}};
        layer.replace(backwards);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && layer.items().size() == 2U, "A backwards range is refused and changes nothing");
    hl::DecorationLayer empty{};
    check(!empty.next_after(hl::ByteOffset{}).has_value(), "An empty layer has no next");
}

void outline_tests() {
    const std::string text = "@import \"palette\";\n"
                             ":root { ink: #102030 on #fff; }\n"
                             "@media (prefers-color-scheme: dark) {\n"
                             "  button { opacity: 0.9; }\n"
                             "}\n"
                             "label { color: role(ink) !important }\n";
    hl::DocumentHighlighter highlighter(hl::css_theme_sheet_language());
    highlighter.reset(text);
    hl::CssOutline outline{};
    outline.build(text, highlighter);
    check(outline.rules().size() == 5U, "Five rules, nested ones included");
    const hl::OutlineRule &import_rule = outline.rules()[0];
    check(import_rule.kind == hl::OutlineRuleKind::at_rule && import_rule.block.empty() &&
              import_rule.prelude == range(0U, text.find(';')),
          "A block-less at-rule ends at its semicolon");
    const hl::OutlineRule &button = outline.rules()[3];
    check(button.parent == 2U && button.depth == 1U && button.closed &&
              button.kind == hl::OutlineRuleKind::style_rule && button.declaration_count == 1U,
          "A rule inside @media names its parent");
    check(outline.rules()[2].kind == hl::OutlineRuleKind::at_rule &&
              outline.rules()[2].block.end.value == text.find("}\nlabel") + 1U,
          "The media block runs to its closing brace");
    check(outline.declarations().size() == 3U, "Three declarations");
    const hl::OutlineDeclaration &color = outline.declarations()[2];
    check(color.rule == 4U && color.important &&
              color.value == range(text.find("role("), text.find(" }\n", text.find("label"))),
          "A declaration without a semicolon ends at its value");

    const std::size_t opacity = text.find("opacity");
    check(outline.innermost_rule_at(hl::ByteOffset{opacity}) == 3U, "The innermost rule at the caret");
    check(outline.innermost_rule_at(hl::ByteOffset{text.find("@media") + 2U}) == 2U,
          "The at-rule at its prelude");
    check(outline.declaration_at(hl::ByteOffset{opacity + 2U}) == 1U, "The declaration at the caret");
    check(!outline.innermost_rule_at(hl::ByteOffset{text.find(":root") - 1U}).has_value(),
          "A newline between rules is in none");

    const std::array<hl::ByteOffset, 3U> reported{hl::ByteOffset{text.find("button")},
                                                  hl::ByteOffset{opacity},
                                                  hl::ByteOffset{text.find("label")}};
    std::vector<hl::Decoration> rule_decorations{};
    outline.append_rule_decorations(reported, hl::DecorationKind::rule_match, rule_decorations);
    check(rule_decorations.size() == 2U && rule_decorations[0].tag == 3U &&
              rule_decorations[0].range == outline.rules()[3].extent() &&
              rule_decorations[1].tag == 4U,
          "Each reported rule is decorated once over its extent");

    const std::array<hl::SourceDiagnostic, 3U> diagnostics{
        hl::SourceDiagnostic{hl::SourcePosition{2U, 9U}, hl::DiagnosticSeverity::error},
        hl::SourceDiagnostic{hl::SourcePosition{6U, 200U}, hl::DiagnosticSeverity::warning},
        hl::SourceDiagnostic{hl::SourcePosition{0U, 0U}, hl::DiagnosticSeverity::error}};
    std::vector<hl::Decoration> markers{};
    hl::append_diagnostic_decorations(text, highlighter, diagnostics, hl::ColumnUnit::utf8_byte,
                                      markers);
    const std::size_t ink = text.find("ink:");
    const std::size_t label_end = text.size() - 1U;
    check(markers.size() == 2U && markers[0].range == range(ink, ink + 3U) &&
              markers[0].kind == hl::DecorationKind::diagnostic_error && markers[0].tag == 0U,
          "An error marks the token at its position");
    check(markers[1].range == range(label_end - 1U, label_end) &&
              markers[1].kind == hl::DecorationKind::diagnostic_warning && markers[1].tag == 1U,
          "A position past the line marks the line's last byte");

    const std::string form = "form a.window {\n  panel a.root {\n    label a.l { text: \"x\"; }\n  }\n}\n";
    hl::DocumentHighlighter form_highlighter(hl::css_form_document_language());
    form_highlighter.reset(form);
    outline.build(form, form_highlighter);
    check(outline.rules().size() == 3U && outline.rules()[2].kind == hl::OutlineRuleKind::object &&
              outline.rules()[2].parent == 1U && outline.declarations().size() == 1U,
          "Form objects nest");

    const std::string unclosed = "a { b: c";
    highlighter.reset(unclosed);
    outline.build(unclosed, highlighter);
    check(outline.rules().size() == 1U && !outline.rules()[0].closed &&
              outline.rules()[0].block.end.value == unclosed.size() &&
              outline.declarations().size() == 1U,
          "An unclosed block runs to the end");
}

} // namespace

int main() {
    try {
        composition_tests();
        layer_tests();
        outline_tests();
        std::cout << "Decoration tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
