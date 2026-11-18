#pragma once
// The model behind an embeddable code editor: a mirror of the edited text,
// its highlighter and outline, and the decoration layers the editor and its
// host fill (theme rules, compile diagnostics, search matches). It holds no
// GUI.Forms type, so the theme editor, the command tools and the tests use it
// without a window.
//
// The text control stays the authority for the text. After each change the
// owner hands the whole new text to update(), which finds the one edit that
// turns the previous text into it (the common prefix and suffix), brings the
// tokens in step from the line before the edit, shifts every layer and
// rebuilds the outline. One execution context owns a CodeDocument.
#include "css_outline.hpp"
#include "decorations.hpp"
#include "highlighter.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace swiftedit::highlight {

// The edit that turns `before` into `after`: the bytes between their common
// prefix and common suffix. The prefix and suffix are cut back to scalar
// boundaries, so the edit never starts or ends inside a UTF-8 sequence. Equal
// texts give an empty edit at their end.
[[nodiscard]] TextEdit edit_between(std::string_view before, std::string_view after) noexcept;

// One mark for an overview strip: a decoration's first line, its kind, and the
// decoration's tag (the diagnostic's or rule's index).
struct LineMarker final {
    LineIndex line{};
    DecorationKind kind{DecorationKind::diagnostic_error};
    std::uint32_t tag{};
    friend constexpr bool operator==(const LineMarker &left, const LineMarker &right) noexcept {
        return left.line == right.line && left.kind == right.kind && left.tag == right.tag;
    }
};

// Which layer a navigation walks.
enum class MarkerLayer : std::uint8_t { diagnostics, rules, search };

class CodeDocument final {
public:
    // The most matches a search records; a search finding more stops there and
    // reports the cap (search_truncated).
    static constexpr std::size_t maximum_search_matches = 100000U;

    // An empty document in `language` (borrowed; registered languages live for
    // the whole program).
    explicit CodeDocument(const Language &language);

    // Lexes the whole text again in `language`; the layers stay.
    void set_language(const Language &language);
    [[nodiscard]] const Language &language() const noexcept { return highlighter_.language(); }

    // Brings the mirror in step with the control's whole new text and returns
    // the edit found. Rule and diagnostic decorations shift with the edit
    // (they stay where the host reported them until it reports again); the
    // search runs again over the new text. A text past the highlighter's
    // maximum is refused with std::length_error and changes nothing.
    TextEdit update(std::string_view text);
    // Replaces the mirror outright (opening a file): no edit, every layer
    // cleared but the search query, which runs again.
    void reset(std::string_view text);

    [[nodiscard]] std::string_view text() const noexcept { return text_; }
    [[nodiscard]] const DocumentHighlighter &highlighter() const noexcept { return highlighter_; }
    // Empty unless the language is of the CSS family (css, form, css-inline).
    [[nodiscard]] const CssOutline &outline() const noexcept { return outline_; }
    [[nodiscard]] bool has_outline() const noexcept { return outlined_; }

    // Theme rules: decorates each rule an offset falls in (see
    // CssOutline::append_rule_decorations), replacing the previous rules. The
    // offsets are byte offsets into the current text, as a theme engine
    // reports the rules that style a control. Without an outline nothing is
    // decorated.
    void set_rule_offsets(std::span<const ByteOffset> offsets);
    void clear_rules() noexcept { rules_.clear(); }
    [[nodiscard]] const DecorationLayer &rules() const noexcept { return rules_; }

    // Compile diagnostics: one decoration each (see
    // append_diagnostic_decorations), tagged with its index in `diagnostics`,
    // replacing the previous ones.
    void set_diagnostics(std::span<const SourceDiagnostic> diagnostics, ColumnUnit unit);
    void clear_diagnostics() noexcept { diagnostics_.clear(); }
    [[nodiscard]] const DecorationLayer &diagnostics() const noexcept { return diagnostics_; }

    // Finds every non-overlapping occurrence of `needle`, replacing the
    // previous matches, and returns how many were recorded. Without
    // `match_case`, ASCII letters match either case; other scalars match
    // exactly. An empty needle clears the search.
    std::size_t search(std::string_view needle, bool match_case);
    [[nodiscard]] const DecorationLayer &matches() const noexcept { return matches_; }
    [[nodiscard]] bool search_truncated() const noexcept { return truncated_; }
    [[nodiscard]] std::string_view search_needle() const noexcept { return needle_; }
    // Marks one recorded match as the current one (search_current), or none.
    void set_current_match(std::optional<std::size_t> index);
    [[nodiscard]] std::optional<std::size_t> current_match() const noexcept { return current_index_; }

    // The decoration of `layer` after (or before) `offset`, wrapping around,
    // as an index into that layer's items.
    [[nodiscard]] std::optional<std::size_t> next_in(MarkerLayer layer, ByteOffset offset) const noexcept;
    [[nodiscard]] std::optional<std::size_t> previous_in(MarkerLayer layer,
                                                         ByteOffset offset) const noexcept;
    [[nodiscard]] const DecorationLayer &layer(MarkerLayer layer) const noexcept;

    // Fills `runs` with the style runs covering `window` (clamped to the
    // text): syntax under the rule, diagnostic, search and current-match
    // decorations. Reuses the document's scratch storage.
    void style_runs(ByteRange window, std::vector<StyleRun> &runs);
    // Fills `markers` (cleared first) with one mark per diagnostic and rule
    // decoration, diagnostics first, each in start order.
    void line_markers(std::vector<LineMarker> &markers) const;

private:
    void rebuild_outline();
    void run_search();

    std::string text_{};
    DocumentHighlighter highlighter_;
    CssOutline outline_{};
    bool outlined_{};
    DecorationLayer rules_{};
    DecorationLayer diagnostics_{};
    DecorationLayer matches_{};
    DecorationLayer current_{};
    std::optional<std::size_t> current_index_{};
    std::string needle_{};
    bool match_case_{};
    bool truncated_{};
    StyleComposer composer_{};
    // Reused scratch: tokens for a window, decorations being built.
    std::vector<Token> tokens_{};
    std::vector<Decoration> building_{};
};

} // namespace swiftedit::highlight
