#pragma once
// A document's tokens, kept current under edits.
//
// The highlighter keeps one record per line (its start, its length without
// the terminator, the lexer state it is entered in and its slice of the
// token array) and the tokens themselves in one array, with line-relative
// columns. An edit re-lexes from the line before the edit until a line after
// the edit is entered in the state it had before; those lines' records and
// tokens are replaced, and every later line only has its start shifted. A
// typical keystroke therefore lexes one or two lines whatever the document's
// size; an edit that opens a comment re-lexes to the comment's end.
//
// The highlighter does not keep the text. Each call that needs it borrows the
// caller's current text for the call; the caller keeps that text and the
// highlighter in step (every change of the text is one apply_edit, or a
// reset). One execution context owns a highlighter.
#include "highlight_types.hpp"
#include "language.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace swiftedit::highlight {

// A place in a document as a person or a compiler reports it: both counting
// from 1. A diagnostic's column may count bytes or Unicode scalar values.
struct SourcePosition final {
    std::uint32_t line{1U};
    std::uint32_t column{1U};
    friend constexpr bool operator==(SourcePosition left, SourcePosition right) noexcept {
        return left.line == right.line && left.column == right.column;
    }
};

enum class ColumnUnit : std::uint8_t {
    utf8_byte,     // column 1 + n is the n-th byte of the line
    unicode_scalar // column 1 + n is the n-th scalar value of the line
};

// Work counters, for tests and the instrumentation panel.
struct HighlightCounters final {
    std::uint64_t resets{};
    std::uint64_t edits{};
    std::uint64_t lines_lexed{};          // over the highlighter's life
    std::uint64_t last_update_lines{};    // lexed by the last reset or edit
    friend constexpr bool operator==(const HighlightCounters &left,
                                     const HighlightCounters &right) noexcept {
        return left.resets == right.resets && left.edits == right.edits &&
               left.lines_lexed == right.lines_lexed &&
               left.last_update_lines == right.last_update_lines;
    }
};

class DocumentHighlighter final {
public:
    // The largest text a highlighter accepts. Larger texts are refused with
    // std::length_error before anything changes.
    static constexpr std::size_t maximum_text_bytes = 256U * 1024U * 1024U;

    // Starts with an empty document in `language`. The language is borrowed;
    // registered languages live for the whole program.
    explicit DocumentHighlighter(const Language &language);

    // Lexes all of `text`, replacing every line and token. On failure (a text
    // past the maximum, or allocation) the previous lines and tokens remain.
    void reset(std::string_view text);
    // Changes the language and lexes all of `text` in it.
    void set_language(const Language &language, std::string_view text);
    // Brings the tokens in step with one edit. `text` is the whole new text;
    // `edit` says which bytes of the previous text it replaced. Returns the
    // range of the new text whose tokens were replaced (for damage). A
    // description that does not fit the previous and new sizes is refused with
    // std::invalid_argument before anything changes.
    ByteRange apply_edit(std::string_view text, TextEdit edit);

    [[nodiscard]] const Language &language() const noexcept { return *language_; }
    [[nodiscard]] std::size_t text_size() const noexcept { return text_size_; }
    // Always at least 1: an empty document has one empty line.
    [[nodiscard]] std::size_t line_count() const noexcept { return lines_.size(); }
    // The line's first byte, and the byte after its content (its terminator,
    // or the document's end). `line` must be below line_count().
    [[nodiscard]] ByteOffset line_start(LineIndex line) const;
    [[nodiscard]] ByteOffset line_end(LineIndex line) const;
    // The line holding `offset`; offsets past the end give the last line.
    [[nodiscard]] LineIndex line_at(ByteOffset offset) const noexcept;
    [[nodiscard]] LexState line_entry_state(LineIndex line) const;
    // The line's tokens, valid until the next reset or edit.
    [[nodiscard]] std::span<const LineToken> line_tokens(LineIndex line) const;
    [[nodiscard]] std::size_t token_count() const noexcept { return tokens_.size(); }
    // Fills `tokens` (cleared first; its capacity is reused) with the tokens
    // that overlap `window`, in document order, unclipped. An empty window
    // overlaps nothing.
    void tokens_in(ByteRange window, std::vector<Token> &tokens) const;
    // The token holding `offset`, or a plain token covering the gap or the
    // line's end when no token holds it.
    [[nodiscard]] Token token_at(ByteOffset offset) const;
    // The byte offset of `position` in `text` (the highlighter's current
    // text), clamped to the line's end; a line past the last gives the
    // document's end.
    [[nodiscard]] ByteOffset offset_at(std::string_view text, SourcePosition position,
                                       ColumnUnit unit) const;
    // The 1-based line and byte column of `offset`.
    [[nodiscard]] SourcePosition position_of(ByteOffset offset) const noexcept;
    [[nodiscard]] HighlightCounters counters() const noexcept { return counters_; }

private:
    struct LineRecord final {
        ByteOffset start{};
        std::size_t length{};
        LexState entry{};
        std::size_t first_token{};
        std::size_t token_count{};
    };
    const Language *language_{};
    std::size_t text_size_{};
    std::vector<LineRecord> lines_{};
    std::vector<LineToken> tokens_{};
    // Reused across updates: the replacement lines and tokens being built.
    std::vector<LineRecord> scratch_lines_{};
    std::vector<LineToken> scratch_tokens_{};
    HighlightCounters counters_{};
};

} // namespace swiftedit::highlight
