// The highlighter core: the language registry, the CSS family's lexer in its
// three dialects, and incremental re-lexing (every edit must leave exactly the
// tokens and line states a full lex of the new text gives).
#include "highlight/css_language.hpp"
#include "highlight/highlighter.hpp"
#include <array>
#include <cstdint>
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

constexpr std::array<const char *, hl::token_kind_count> kind_names{
    "plain",    "comment",  "string",           "number",         "color",
    "keyword",  "at",       "type",             "id",             "class",
    "pseudo",   "property", "custom",           "function",       "punct",
    "operator", "ident",    "important",        "invalid"};

// Every token of `text` as "kind:bytes" joined by single spaces.
std::string describe(const hl::DocumentHighlighter &highlighter, std::string_view text) {
    std::vector<hl::Token> tokens{};
    highlighter.tokens_in({hl::ByteOffset{}, hl::ByteOffset{text.size()}}, tokens);
    std::string result{};
    for (const hl::Token &token : tokens) {
        if (!result.empty())
            result += ' ';
        result += kind_names[static_cast<std::size_t>(token.kind)];
        result += ':';
        result += text.substr(token.range.start.value, token.range.length());
    }
    return result;
}

std::string lex(const hl::Language &language, std::string_view text) {
    hl::DocumentHighlighter highlighter(language);
    highlighter.reset(text);
    const std::string result = describe(highlighter, text);
    return result;
}

void expect_tokens(const hl::Language &language, std::string_view text, std::string_view expected,
                   const char *message) {
    const std::string actual = lex(language, text);
    if (actual != expected) {
        std::cerr << "expected: " << expected << "\nactual:   " << actual << '\n';
        throw std::runtime_error(message);
    }
}

void registry_tests() {
    check(hl::language_for_file_name("dark.THEME.CSS").name() == "css",
          "A .css file name selects the theme sheet language regardless of case");
    check(hl::language_for_file_name("settings.form").name() == "form",
          "A .form file name selects the form document language");
    check(hl::language_for_file_name("notes.txt").name() == "plain" &&
              hl::language_for_file_name("").name() == "plain",
          "Other file names select plain text");
    check(hl::language_named("css-inline").name() == "css-inline" &&
              hl::language_named("cobol").name() == "plain",
          "Languages are found by name, with plain text as the fallback");
    check(hl::registered_languages().size() == 4U &&
              (*hl::registered_languages()[0]).name() == "plain",
          "The registry lists plain text first");
    expect_tokens(hl::language_named("plain"), "a { b: c; }", "", "Plain text has no tokens");
}

void theme_sheet_tests() {
    const hl::Language &css = hl::css_theme_sheet_language();
    expect_tokens(css, ":root { ink-on-paper: #1d3557 on #ffffff; }",
                  "pseudo::root punct:{ property:ink-on-paper punct:: color:#1d3557 keyword:on "
                  "color:#ffffff punct:; punct:}",
                  "A colour role pair reads as two colours joined by the keyword on");
    expect_tokens(css, ".status { color: role(ink-on-paper); font-size: 15px; }",
                  "class:.status punct:{ property:color punct:: function:role punct:( "
                  "ident:ink-on-paper punct:) punct:; property:font-size punct:: number:15px "
                  "punct:; punct:}",
                  "Functions, identifiers and dimensions in values");
    expect_tokens(css, "button.default-action::part(face):hover { scale: 1.02; }",
                  "type:button class:.default-action pseudo:::part punct:( ident:face punct:) "
                  "pseudo::hover punct:{ property:scale punct:: number:1.02 punct:; punct:}",
                  "Part arguments are names, not type selectors");
    expect_tokens(css, "#settings\\.root > button:is(:hover, .x) { opacity: 0.9 !important }",
                  "id:#settings\\.root operator:> type:button pseudo::is punct:( pseudo::hover "
                  "punct:, class:.x punct:) punct:{ property:opacity punct:: number:0.9 "
                  "important:!important punct:}",
                  "Escaped stable IDs, combinators, selector arguments and !important");
    expect_tokens(css, "@import \"palette\";\n@media (prefers-color-scheme: dark) { button { opacity: 0.9 } }",
                  "at:@import string:\"palette\" punct:; at:@media punct:( "
                  "property:prefers-color-scheme punct:: ident:dark punct:) punct:{ type:button "
                  "punct:{ property:opacity punct:: number:0.9 punct:} punct:}",
                  "A media block holds rules; its feature names read as properties");
    expect_tokens(css, "@keyframes pulse { from { opacity: 0.5 } 50% { opacity: 1 } }",
                  "at:@keyframes ident:pulse punct:{ type:from punct:{ property:opacity punct:: "
                  "number:0.5 punct:} number:50% punct:{ property:opacity punct:: number:1 "
                  "punct:} punct:}",
                  "Keyframe selectors");
    expect_tokens(css, "a { --lift: 0.6; scale: var(--lift, 1); margin: -2px; }",
                  "type:a punct:{ custom:--lift punct:: number:0.6 punct:; property:scale punct:: "
                  "function:var punct:( custom:--lift punct:, number:1 punct:) punct:; "
                  "property:margin punct:: number:-2px punct:; punct:}",
                  "Custom properties and negative numbers");
    expect_tokens(css, "a { color: #12345; b: 'open",
                  "type:a punct:{ property:color punct:: invalid:#12345 punct:; property:b "
                  "punct:: invalid:'open",
                  "A malformed colour and an unterminated string are invalid");
    expect_tokens(css, "} a { }", "invalid:} type:a punct:{ punct:}",
                  "A stray closing brace is invalid");
    expect_tokens(css, "button { background: pattern(checker, #d4d0c8, #fff) }",
                  "type:button punct:{ property:background punct:: function:pattern punct:( "
                  "ident:checker punct:, color:#d4d0c8 punct:, color:#fff punct:) punct:}",
                  "Engine functions take colours and names");
    expect_tokens(css, "/* palette */\nlabel { /* one\nline\ntwo */ color: red }",
                  "comment:/* palette */ type:label punct:{ comment:/* one comment:line "
                  "comment:two */ property:color punct:: ident:red punct:}",
                  "A comment spans lines through the line state");
    expect_tokens(css, "a { content: \"one\\\ntwo\"; }",
                  "type:a punct:{ property:content punct:: string:\"one\\ string:two\" punct:; "
                  "punct:}",
                  "A backslash continues a string onto the next line");
    expect_tokens(css, "card {\n  ::part(face):hover { scale: 1.04 }\n  opacity: 1;\n}",
                  "type:card punct:{ pseudo:::part punct:( ident:face punct:) pseudo::hover "
                  "punct:{ property:scale punct:: number:1.04 punct:} property:opacity punct:: "
                  "number:1 punct:; punct:}",
                  "A nested rule inside a declaration block is found by its brace");
}

void form_document_tests() {
    const hl::Language &form = hl::css_form_document_language();
    const std::string_view document =
        "form example.window {\n"
        "  text: \"GUI.Forms example\";\n"
        "  client-size: 420 360;\n"
        "  panel example.root {\n"
        "    check-box example.shout { checked: bind(shout, one_way); classes: \"wide\"; }\n"
        "  }\n"
        "}\n";
    expect_tokens(form, document,
                  "type:form id:example.window punct:{ property:text punct:: "
                  "string:\"GUI.Forms example\" punct:; property:client-size punct:: number:420 "
                  "number:360 punct:; type:panel id:example.root punct:{ type:check-box "
                  "id:example.shout punct:{ property:checked punct:: function:bind punct:( "
                  "ident:shout punct:, keyword:one_way punct:) punct:; property:classes punct:: "
                  "string:\"wide\" punct:; punct:} punct:} punct:}",
                  "Form objects are a type and a dotted stable ID; bindings are functions");
}

void inline_style_tests() {
    const hl::Language &inline_style = hl::css_inline_style_language();
    expect_tokens(inline_style, "--lift: 0.6; opacity: 0.9 !important; ::part(face):hover { scale: 1.04 }",
                  "custom:--lift punct:: number:0.6 punct:; property:opacity punct:: number:0.9 "
                  "important:!important punct:; pseudo:::part punct:( ident:face punct:) "
                  "pseudo::hover punct:{ property:scale punct:: number:1.04 punct:}",
                  "An inline style starts among declarations");
}

// The line structure and every line's entry state and tokens.
std::string snapshot(const hl::DocumentHighlighter &highlighter, std::string_view text) {
    std::string result = describe(highlighter, text);
    for (std::size_t line = 0U; line < highlighter.line_count(); ++line) {
        const hl::LineIndex index{line};
        result += '|';
        result += std::to_string(highlighter.line_start(index).value);
        result += ',';
        result += std::to_string(highlighter.line_end(index).value);
        result += ',';
        result += std::to_string(highlighter.line_entry_state(index).value);
    }
    return result;
}

// A deterministic generator, so a failure reproduces.
struct Generator final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};
    std::size_t below(std::size_t bound) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::size_t result = bound == 0U ? 0U : static_cast<std::size_t>((state >> 33U) % bound);
        return result;
    }
};

void incremental_tests() {
    const std::string seed =
        "/* sheet */\n:root { ink: #102030 on #ffffff; }\r\n.status { color: role(ink); }\r"
        "@media (prefers-color-scheme: dark) {\n  button::part(face) { opacity: 0.9 }\n}\n"
        "a { content: \"x\\\ny\"; }\n";
    constexpr std::array<std::string_view, 14U> pieces{
        "/*", "*/", "{", "}", "\n", "\r", "\r\n", "\"", "\\", "a: b;", "@media x {", " ",
        "#fff", "button"};
    const std::array<const hl::Language *, 3U> languages{&hl::css_theme_sheet_language(),
                                                         &hl::css_form_document_language(),
                                                         &hl::css_inline_style_language()};
    for (const hl::Language *language : languages) {
        Generator generator{};
        std::string text = seed;
        hl::DocumentHighlighter incremental(*language);
        incremental.reset(text);
        for (int round = 0; round < 600; ++round) {
            const std::size_t start = generator.below(text.size() + 1U);
            const std::size_t removed = generator.below(std::min<std::size_t>(6U, text.size() - start) + 1U);
            const std::string_view piece = generator.below(3U) == 0U ? std::string_view{}
                                                                     : pieces[generator.below(pieces.size())];
            text.replace(start, removed, piece);
            const hl::TextEdit edit{hl::ByteOffset{start}, removed, piece.size()};
            const hl::ByteRange changed = incremental.apply_edit(text, edit);
            check(changed.start.value <= start && changed.end.value <= text.size(),
                  "The changed range starts at or before the edit");
            hl::DocumentHighlighter full(*language);
            full.reset(text);
            if (snapshot(incremental, text) != snapshot(full, text)) {
                std::cerr << "round " << round << " language " << (*language).name() << '\n';
                throw std::runtime_error("An edit leaves the tokens a full lex gives");
            }
        }
    }

    // A keystroke in a large document lexes a line or two, not the document.
    std::string large{};
    for (int rule = 0; rule < 1000; ++rule)
        large += ".rule { color: #102030; }\n";
    hl::DocumentHighlighter highlighter(hl::css_theme_sheet_language());
    highlighter.reset(large);
    check(highlighter.line_count() == 1001U, "Each rule is a line, plus the empty last line");
    const std::size_t inside = large.find("color", 500U * 26U);
    large.insert(inside, "x");
    static_cast<void>(highlighter.apply_edit(large, hl::TextEdit{hl::ByteOffset{inside}, 0U, 1U}));
    check(highlighter.counters().last_update_lines <= 2U, "A keystroke lexes at most two lines");
    // Opening a comment re-lexes to the end; closing it converges again.
    large.insert(inside, "/*");
    static_cast<void>(highlighter.apply_edit(large, hl::TextEdit{hl::ByteOffset{inside}, 0U, 2U}));
    check(highlighter.counters().last_update_lines > 400U, "An open comment re-lexes the rest");
    large.erase(inside, 2U);
    static_cast<void>(highlighter.apply_edit(large, hl::TextEdit{hl::ByteOffset{inside}, 2U, 0U}));
    hl::DocumentHighlighter full(hl::css_theme_sheet_language());
    full.reset(large);
    check(snapshot(highlighter, large) == snapshot(full, large), "Removing the comment restores the tokens");

    bool refused = false;
    try {
        static_cast<void>(highlighter.apply_edit(large, hl::TextEdit{hl::ByteOffset{3U}, 1U, 0U}));
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && snapshot(highlighter, large) == snapshot(full, large),
          "An edit that does not fit the sizes is refused and changes nothing");
}

void position_tests() {
    const std::string text = "a\r\nb\rc\n\xC3\xA9x { y: z }";
    hl::DocumentHighlighter highlighter(hl::css_theme_sheet_language());
    highlighter.reset(text);
    check(highlighter.line_count() == 4U, "LF, CR and CRLF each end a line");
    check(highlighter.line_start(hl::LineIndex{3U}).value == 7U &&
              highlighter.line_end(hl::LineIndex{0U}).value == 1U,
          "Line ends exclude terminators");
    const hl::ByteOffset by_scalar =
        highlighter.offset_at(text, hl::SourcePosition{4U, 2U}, hl::ColumnUnit::unicode_scalar);
    const hl::ByteOffset by_byte =
        highlighter.offset_at(text, hl::SourcePosition{4U, 2U}, hl::ColumnUnit::utf8_byte);
    check(by_scalar.value == 9U && by_byte.value == 8U, "Columns count scalars or bytes");
    check(highlighter.offset_at(text, hl::SourcePosition{9U, 1U}, hl::ColumnUnit::utf8_byte).value ==
              text.size(),
          "A line past the last gives the end");
    const hl::SourcePosition position = highlighter.position_of(hl::ByteOffset{9U});
    check(position.line == 4U && position.column == 3U, "Offsets map back to 1-based byte columns");
    const hl::Token token = highlighter.token_at(hl::ByteOffset{13U});
    check(token.kind == hl::TokenKind::property && token.range.start.value == 13U,
          "The token at an offset");
    const hl::Token gap = highlighter.token_at(hl::ByteOffset{12U});
    check(gap.kind == hl::TokenKind::plain && gap.range.start.value == 12U && gap.range.end.value == 13U,
          "A gap between tokens is a plain token");
}

} // namespace

int main() {
    try {
        registry_tests();
        theme_sheet_tests();
        form_document_tests();
        inline_style_tests();
        incremental_tests();
        position_tests();
        std::cout << "Highlight tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
