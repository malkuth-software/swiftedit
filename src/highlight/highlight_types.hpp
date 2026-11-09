#pragma once
// The highlighter's vocabulary: byte offsets into a document's UTF-8 text,
// the token kinds every language reports, and the opaque lexer state that is
// carried from one line to the next. Nothing here depends on GUI.Forms, so
// the theme editor and the command session can link the highlighter alone.
#include <cstddef>
#include <cstdint>

namespace swiftedit::highlight {

// A byte offset into a document's UTF-8 text. Character positions and line
// numbers are other types; this one only counts bytes.
struct ByteOffset final {
    std::size_t value{};
    friend constexpr bool operator==(ByteOffset left, ByteOffset right) noexcept {
        return left.value == right.value;
    }
    friend constexpr bool operator!=(ByteOffset left, ByteOffset right) noexcept {
        return left.value != right.value;
    }
    friend constexpr bool operator<(ByteOffset left, ByteOffset right) noexcept {
        return left.value < right.value;
    }
    friend constexpr bool operator<=(ByteOffset left, ByteOffset right) noexcept {
        return left.value <= right.value;
    }
    friend constexpr bool operator>(ByteOffset left, ByteOffset right) noexcept {
        return left.value > right.value;
    }
    friend constexpr bool operator>=(ByteOffset left, ByteOffset right) noexcept {
        return left.value >= right.value;
    }
};

// The half-open byte range [start, end). An empty range has start == end.
struct ByteRange final {
    ByteOffset start{};
    ByteOffset end{};
    [[nodiscard]] constexpr std::size_t length() const noexcept {
        const std::size_t result = end.value - start.value;
        return result;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return start.value == end.value; }
    [[nodiscard]] constexpr bool contains(ByteOffset offset) const noexcept {
        const bool result = start.value <= offset.value && offset.value < end.value;
        return result;
    }
    friend constexpr bool operator==(ByteRange left, ByteRange right) noexcept {
        return left.start == right.start && left.end == right.end;
    }
    friend constexpr bool operator!=(ByteRange left, ByteRange right) noexcept {
        return !(left == right);
    }
};

// A zero-based line index into a document. Line i begins after the i-th line
// terminator (LF, CR or CRLF).
struct LineIndex final {
    std::size_t value{};
    friend constexpr bool operator==(LineIndex left, LineIndex right) noexcept {
        return left.value == right.value;
    }
    friend constexpr bool operator!=(LineIndex left, LineIndex right) noexcept {
        return left.value != right.value;
    }
    friend constexpr bool operator<(LineIndex left, LineIndex right) noexcept {
        return left.value < right.value;
    }
};

// What a token is. Every language reports these kinds; a presenter maps each
// to a colour of its own (StylePalette in src/code_editor), so a language
// never names a colour. The order is part of the presenter contract: a presenter may index
// a table of `token_kind_count` entries by the kind's value.
enum class TokenKind : std::uint8_t {
    plain,              // text no other kind describes; also every gap between tokens
    comment,            // /* ... */
    string,             // "..." or '...'
    number,             // 12, 1.5, 90ms, 50%: a number with its unit
    color,              // #rgb, #rrggbb, #rrggbbaa
    keyword,            // a value keyword: on, none, inherit, auto, true, false
    at_rule,            // @import, @media, @keyframes, @theme
    type_selector,      // button, track-bar; a form document's object type
    identity_selector,  // #settings\.ok; a form document's stable ID
    class_selector,     // .heading
    pseudo_selector,    // :hover, ::part, :family, :state
    property,           // background-color, text, bounds
    custom_property,    // --lift
    function,           // role(, rgb(, bind(, var(
    punctuation,        // { } ; : , ( )
    operator_symbol,    // > + ~ * /
    value_identifier,   // any other identifier in a value or a selector argument
    important,          // !important
    invalid,            // an unterminated string, a malformed colour, a stray brace
};
inline constexpr std::size_t token_kind_count = 19U;

// One token: the bytes [column, column + length) of one line, and its kind.
// Columns are line-relative so that an edit only shifts line starts, never
// the tokens of the lines after it.
struct LineToken final {
    std::uint32_t column{};
    std::uint32_t length{};
    TokenKind kind{TokenKind::plain};
    friend constexpr bool operator==(const LineToken &left, const LineToken &right) noexcept {
        return left.column == right.column && left.length == right.length &&
               left.kind == right.kind;
    }
};

// One token in document coordinates, as the highlighter hands it out.
struct Token final {
    ByteRange range{};
    TokenKind kind{TokenKind::plain};
    friend constexpr bool operator==(const Token &left, const Token &right) noexcept {
        return left.range == right.range && left.kind == right.kind;
    }
};

// The lexer's state at a line boundary. Its bits belong to the language that
// produced it; the highlighter only stores and compares states, and stops
// re-lexing after an edit once a line's entry state is what it was before.
struct LexState final {
    std::uint64_t value{};
    friend constexpr bool operator==(LexState left, LexState right) noexcept {
        return left.value == right.value;
    }
    friend constexpr bool operator!=(LexState left, LexState right) noexcept {
        return left.value != right.value;
    }
};

// One replacement in a document: the bytes [start, start + removed) of the old
// text became `inserted` bytes starting at `start` in the new text.
struct TextEdit final {
    ByteOffset start{};
    std::size_t removed{};
    std::size_t inserted{};
};

} // namespace swiftedit::highlight
