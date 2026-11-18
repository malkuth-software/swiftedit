#pragma once
// Decorations over a document's text, and the composition of syntax tokens
// and decorations into the style runs a presenter paints.
//
// A decoration is a byte range with a meaning that is not syntax: a theme
// rule that applies to the control the person clicked, a compile diagnostic,
// a search match. Each producer owns one DecorationLayer and replaces its
// contents whenever its answer changes; the presenter composes the visible
// window from the tokens and every layer, so producers never see each other.
#include "highlight_types.hpp"
#include <optional>
#include <span>
#include <vector>

namespace swiftedit::highlight {

// What a decoration means. A presenter maps each kind to a theme role
// (`.decoration-rule-match` and so on); the order is part of that contract.
enum class DecorationKind : std::uint8_t {
    rule_match,          // a theme rule that affects the selected control
    rule_focus,          // the rule that decides the selected property
    diagnostic_error,    // a compile error at this text
    diagnostic_warning,  // a compile warning at this text
    search_match,        // a match of the find bar's query
    search_current,      // the match the find bar selected
};
inline constexpr std::size_t decoration_kind_count = 6U;

// A set of decoration kinds, one bit per kind.
struct DecorationMask final {
    std::uint16_t bits{};
    [[nodiscard]] constexpr bool has(DecorationKind kind) const noexcept {
        const bool result = (bits & (1U << static_cast<unsigned>(kind))) != 0U;
        return result;
    }
    constexpr void add(DecorationKind kind) noexcept {
        bits = static_cast<std::uint16_t>(bits | (1U << static_cast<unsigned>(kind)));
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits == 0U; }
    friend constexpr bool operator==(DecorationMask left, DecorationMask right) noexcept {
        return left.bits == right.bits;
    }
    friend constexpr bool operator!=(DecorationMask left, DecorationMask right) noexcept {
        return left.bits != right.bits;
    }
};

// One decoration. `tag` belongs to the producer: a diagnostic's index in the
// compile's findings, a rule's index in the outline, a match's ordinal.
struct Decoration final {
    ByteRange range{};
    DecorationKind kind{DecorationKind::rule_match};
    std::uint32_t tag{};
    friend constexpr bool operator==(const Decoration &left, const Decoration &right) noexcept {
        return left.range == right.range && left.kind == right.kind && left.tag == right.tag;
    }
};

// One producer's decorations, kept in start order. Decorations of one layer
// may overlap (a nested rule inside its parent).
class DecorationLayer final {
public:
    // Replaces every decoration with a copy of `decorations`. Ranges with
    // end < start are refused with std::invalid_argument before anything
    // changes; empty ranges are dropped.
    void replace(std::span<const Decoration> decorations);
    void clear() noexcept { items_.clear(); }
    // Follows one edit of the text: ranges after the edit shift; ranges the
    // edit touches keep their outer bytes and take the replacement in; ranges
    // the edit removes entirely are dropped.
    void shift_for_edit(TextEdit edit);
    [[nodiscard]] std::span<const Decoration> items() const noexcept { return items_; }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    // Appends to `out` the decorations that overlap `window`, in start order.
    void append_overlapping(ByteRange window, std::vector<Decoration> &out) const;
    // The index of the first decoration starting after `offset`, wrapping to
    // the first; empty for an empty layer. Drives "next diagnostic".
    [[nodiscard]] std::optional<std::size_t> next_after(ByteOffset offset) const noexcept;
    // The index of the last decoration starting before `offset`, wrapping to
    // the last; empty for an empty layer.
    [[nodiscard]] std::optional<std::size_t> previous_before(ByteOffset offset) const noexcept;

private:
    std::vector<Decoration> items_{};
};

// One run of uniformly styled text.
struct StyleRun final {
    ByteRange range{};
    TokenKind syntax{TokenKind::plain};
    DecorationMask decorations{};
    friend constexpr bool operator==(const StyleRun &left, const StyleRun &right) noexcept {
        return left.range == right.range && left.syntax == right.syntax &&
               left.decorations == right.decorations;
    }
};

// Composes tokens and decoration layers into style runs. Owns scratch storage
// reused across calls, so composing the visible window on each paint does not
// allocate once the storage has grown. One execution context owns a composer.
class StyleComposer final {
public:
    // Fills `runs` (cleared first) with runs that cover `window` exactly, in
    // order and without gaps; adjacent runs with the same style are merged.
    // `tokens` are sorted and non-overlapping (as tokens_in gives them); they
    // and the layers are borrowed for the call. An empty window gives no runs.
    void compose(ByteRange window, std::span<const Token> tokens,
                 std::span<const DecorationLayer *const> layers, std::vector<StyleRun> &runs);

private:
    struct Event final {
        ByteOffset at{};
        DecorationKind kind{DecorationKind::rule_match};
        bool opening{};
    };
    struct EventBefore final {
        bool operator()(const Event &left, const Event &right) const noexcept {
            return left.at < right.at;
        }
    };
    std::vector<Decoration> overlapping_{};
    std::vector<Event> events_{};
    std::vector<ByteOffset> cuts_{};
};

} // namespace swiftedit::highlight
