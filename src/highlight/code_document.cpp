#include "code_document.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace swiftedit::highlight {
namespace {

bool continuation_byte(char value) noexcept {
    const unsigned char byte = static_cast<unsigned char>(value);
    const bool result = (byte & 0xC0U) == 0x80U;
    return result;
}

char ascii_folded(char value) noexcept {
    const bool upper = value >= 'A' && value <= 'Z';
    const char result = upper ? static_cast<char>(value - 'A' + 'a') : value;
    return result;
}

bool equal_at(std::string_view text, std::size_t at, std::string_view needle, bool match_case) noexcept {
    for (std::size_t index = 0U; index < needle.size(); ++index) {
        const char left = text[at + index];
        const char right = needle[index];
        const bool same = match_case ? left == right : ascii_folded(left) == ascii_folded(right);
        if (!same)
            return false;
    }
    return true;
}

bool css_family(const Language &language) noexcept {
    const std::string_view name = language.name();
    const bool result = name == "css" || name == "form" || name == "css-inline";
    return result;
}

} // namespace

TextEdit edit_between(std::string_view before, std::string_view after) noexcept {
    const std::size_t shorter = std::min(before.size(), after.size());
    std::size_t prefix = 0U;
    while (prefix < shorter && before[prefix] == after[prefix])
        ++prefix;
    // Back to the start of the scalar the first difference is in.
    while (prefix > 0U && prefix < before.size() && continuation_byte(before[prefix]))
        --prefix;
    while (prefix > 0U && prefix < after.size() && continuation_byte(after[prefix]))
        --prefix;
    // The suffix may not reach into the prefix of either text.
    const std::size_t suffix_room = shorter - prefix;
    std::size_t suffix = 0U;
    while (suffix < suffix_room && before[before.size() - 1U - suffix] == after[after.size() - 1U - suffix])
        ++suffix;
    // Forward to the start of a scalar: the suffix must begin on a lead byte.
    while (suffix > 0U && continuation_byte(before[before.size() - suffix]))
        --suffix;
    TextEdit result{};
    result.start = ByteOffset{prefix};
    result.removed = before.size() - prefix - suffix;
    result.inserted = after.size() - prefix - suffix;
    return result;
}

CodeDocument::CodeDocument(const Language &language) : highlighter_(language) {
    outlined_ = css_family(language);
}

void CodeDocument::set_language(const Language &language) {
    highlighter_.set_language(language, text_);
    outlined_ = css_family(language);
    rebuild_outline();
}

TextEdit CodeDocument::update(std::string_view text) {
    if (text.size() > DocumentHighlighter::maximum_text_bytes)
        throw std::length_error("The text is larger than the highlighter accepts.");
    const TextEdit edit = edit_between(text_, text);
    if (edit.removed == 0U && edit.inserted == 0U)
        return edit;
    std::string updated(text);
    static_cast<void>(highlighter_.apply_edit(updated, edit));
    text_ = std::move(updated);
    rules_.shift_for_edit(edit);
    diagnostics_.shift_for_edit(edit);
    rebuild_outline();
    run_search();
    return edit;
}

void CodeDocument::reset(std::string_view text) {
    if (text.size() > DocumentHighlighter::maximum_text_bytes)
        throw std::length_error("The text is larger than the highlighter accepts.");
    std::string replaced(text);
    highlighter_.reset(replaced);
    text_ = std::move(replaced);
    rules_.clear();
    diagnostics_.clear();
    rebuild_outline();
    run_search();
}

void CodeDocument::rebuild_outline() {
    if (outlined_) {
        outline_.build(text_, highlighter_);
    } else {
        outline_ = CssOutline{};
    }
}

void CodeDocument::set_rule_offsets(std::span<const ByteOffset> offsets) {
    building_.clear();
    if (outlined_)
        outline_.append_rule_decorations(offsets, DecorationKind::rule_match, building_);
    rules_.replace(building_);
}

void CodeDocument::set_diagnostics(std::span<const SourceDiagnostic> diagnostics, ColumnUnit unit) {
    building_.clear();
    append_diagnostic_decorations(text_, highlighter_, diagnostics, unit, building_);
    diagnostics_.replace(building_);
}

std::size_t CodeDocument::search(std::string_view needle, bool match_case) {
    needle_ = std::string(needle);
    match_case_ = match_case;
    run_search();
    return matches_.items().size();
}

void CodeDocument::run_search() {
    building_.clear();
    truncated_ = false;
    const std::size_t length = needle_.size();
    if (length != 0U && length <= text_.size()) {
        const std::size_t last_start = text_.size() - length;
        std::size_t at = 0U;
        while (at <= last_start) {
            if (!equal_at(text_, at, needle_, match_case_)) {
                ++at;
                continue;
            }
            if (building_.size() == maximum_search_matches) {
                truncated_ = true;
                break;
            }
            const ByteRange range{ByteOffset{at}, ByteOffset{at + length}};
            const Decoration match{range, DecorationKind::search_match,
                                   static_cast<std::uint32_t>(building_.size())};
            building_.push_back(match);
            at += length;
        }
    }
    matches_.replace(building_);
    // A current match past the new count is dropped; one within it keeps its
    // index (the owner moves it on as the user steps).
    if (current_index_ && *current_index_ >= matches_.items().size())
        current_index_.reset();
    set_current_match(current_index_);
}

void CodeDocument::set_current_match(std::optional<std::size_t> index) {
    if (index && *index >= matches_.items().size())
        throw std::out_of_range("No search match has that index.");
    current_index_ = index;
    if (!index) {
        current_.clear();
        return;
    }
    const Decoration &match = matches_.items()[*index];
    const std::array<Decoration, 1U> current{
        Decoration{match.range, DecorationKind::search_current, match.tag}};
    current_.replace(current);
}

const DecorationLayer &CodeDocument::layer(MarkerLayer which) const noexcept {
    switch (which) {
    case MarkerLayer::diagnostics:
        return diagnostics_;
    case MarkerLayer::rules:
        return rules_;
    case MarkerLayer::search:
        return matches_;
    }
    return matches_;
}

std::optional<std::size_t> CodeDocument::next_in(MarkerLayer which, ByteOffset offset) const noexcept {
    const std::optional<std::size_t> result = layer(which).next_after(offset);
    return result;
}

std::optional<std::size_t> CodeDocument::previous_in(MarkerLayer which,
                                                     ByteOffset offset) const noexcept {
    const std::optional<std::size_t> result = layer(which).previous_before(offset);
    return result;
}

void CodeDocument::style_runs(ByteRange window, std::vector<StyleRun> &runs) {
    const std::size_t size = text_.size();
    const ByteRange clamped{ByteOffset{std::min(window.start.value, size)},
                            ByteOffset{std::min(std::max(window.start.value, window.end.value), size)}};
    highlighter_.tokens_in(clamped, tokens_);
    const std::array<const DecorationLayer *, 4U> layers{&rules_, &diagnostics_, &matches_, &current_};
    composer_.compose(clamped, tokens_, layers, runs);
}

void CodeDocument::line_markers(std::vector<LineMarker> &markers) const {
    markers.clear();
    for (const Decoration &decoration : diagnostics_.items()) {
        const LineMarker marker{highlighter_.line_at(decoration.range.start), decoration.kind,
                                decoration.tag};
        markers.push_back(marker);
    }
    for (const Decoration &decoration : rules_.items()) {
        const LineMarker marker{highlighter_.line_at(decoration.range.start), decoration.kind,
                                decoration.tag};
        markers.push_back(marker);
    }
}

} // namespace swiftedit::highlight
