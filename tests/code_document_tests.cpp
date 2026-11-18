// The code editor's model without a window: edit discovery, layers that
// follow edits, search, navigation, style runs and overview markers.
#include "highlight/code_document.hpp"
#include "highlight/css_language.hpp"
#include "highlight/language.hpp"
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

bool same_edit(hl::TextEdit edit, std::size_t start, std::size_t removed, std::size_t inserted) {
    const bool result = edit.start.value == start && edit.removed == removed && edit.inserted == inserted;
    return result;
}

void edit_tests() {
    check(same_edit(hl::edit_between("abc", "abc"), 3U, 0U, 0U), "Equal texts give an empty edit at the end");
    check(same_edit(hl::edit_between("abc", "abXc"), 2U, 0U, 1U), "An insertion");
    check(same_edit(hl::edit_between("abXc", "abc"), 2U, 1U, 0U), "A deletion");
    check(same_edit(hl::edit_between("", "new"), 0U, 0U, 3U), "Into an empty text");
    check(same_edit(hl::edit_between("aaa", "aaaa"), 3U, 0U, 1U), "A repeated byte is found at the end");
    check(same_edit(hl::edit_between("abcabc", "abc"), 3U, 3U, 0U), "The suffix never reaches into the prefix");
    // U+00E9 is C3 A9 and U+00E8 is C3 A8: the prefix must not stop between
    // the lead and continuation bytes.
    const hl::TextEdit accent = hl::edit_between("x\xC3\xA9y", "x\xC3\xA8y");
    check(same_edit(accent, 1U, 2U, 2U), "An edit covers whole scalars");
    // U+00E9 against U+0129 (C4 A9): they share the continuation byte, so the
    // suffix must not start on it.
    const hl::TextEdit shared_tail = hl::edit_between("\xC3\xA9", "\xC4\xA9");
    check(same_edit(shared_tail, 0U, 2U, 2U), "A suffix starts on a lead byte");
}

void update_tests() {
    hl::CodeDocument document(hl::css_theme_sheet_language());
    check(document.has_outline(), "A CSS document has an outline");
    const std::string first = "a { b: c; }\nd { e: f; }\n";
    document.reset(first);
    check(document.outline().rules().size() == 2U, "Two rules after a reset");

    const std::array<hl::SourceDiagnostic, 1U> diagnostics{
        hl::SourceDiagnostic{hl::SourcePosition{2U, 5U}, hl::DiagnosticSeverity::error}};
    document.set_diagnostics(diagnostics, hl::ColumnUnit::utf8_byte);
    check(document.diagnostics().items().size() == 1U &&
              document.diagnostics().items()[0].range == range(16U, 17U),
          "The diagnostic marks the property on line 2");
    const std::array<hl::ByteOffset, 1U> offsets{hl::ByteOffset{14U}};
    document.set_rule_offsets(offsets);
    check(document.rules().items().size() == 1U && document.rules().items()[0].range == range(12U, 23U),
          "The reported rule is decorated over its extent");

    const std::string second = "/* x */ " + first;
    const hl::TextEdit edit = document.update(second);
    check(same_edit(edit, 0U, 0U, 8U), "The update finds the insertion");
    check(document.text() == second, "The mirror follows the text");
    check(document.diagnostics().items()[0].range == range(24U, 25U) &&
              document.rules().items()[0].range == range(20U, 31U),
          "Layers shift with the edit");
    check(document.outline().rules().size() == 2U &&
              document.outline().rules()[0].prelude.start.value == 8U,
          "The outline is rebuilt after the edit");

    const hl::TextEdit none = document.update(second);
    check(none.removed == 0U && none.inserted == 0U, "An unchanged text is no edit");
    document.reset("x");
    check(document.rules().empty() && document.diagnostics().empty(), "A reset clears the host's layers");

    document.set_language(hl::language_named("plain"));
    check(!document.has_outline() && document.outline().rules().empty(), "Plain text has no outline");
    const std::array<hl::ByteOffset, 1U> any{hl::ByteOffset{0U}};
    document.set_rule_offsets(any);
    check(document.rules().empty(), "Without an outline no rule is decorated");
}

void search_tests() {
    hl::CodeDocument document(hl::css_theme_sheet_language());
    document.reset("Color: red; color: blue; COLOR");
    check(document.search("color", true) == 1U, "A case-sensitive search");
    check(document.search("color", false) == 3U, "ASCII letters fold without match case");
    check(document.matches().items()[1].range == range(12U, 17U), "Matches are in order");
    document.set_current_match(1U);
    std::vector<hl::StyleRun> runs{};
    document.style_runs(range(12U, 17U), runs);
    check(!runs.empty() && runs[0].decorations.has(hl::DecorationKind::search_current) &&
              runs[0].decorations.has(hl::DecorationKind::search_match),
          "The current match is decorated over the match");
    check(document.next_in(hl::MarkerLayer::search, hl::ByteOffset{12U}) == 2U &&
              document.previous_in(hl::MarkerLayer::search, hl::ByteOffset{12U}) == 0U,
          "Navigation steps through the matches");
    check(document.next_in(hl::MarkerLayer::search, hl::ByteOffset{26U}) == 0U, "Navigation wraps");

    document.update("Color: red; COLOR");
    check(document.matches().items().size() == 2U && document.current_match() == 1U,
          "The search runs again after an edit and keeps a current match in range");
    document.update("Color");
    check(document.matches().items().size() == 1U && !document.current_match().has_value(),
          "A current match past the count is dropped");
    document.search("aa", true);
    document.update("aaaaa");
    check(document.matches().items().size() == 2U, "Matches do not overlap");
    check(document.search("", true) == 0U && document.matches().empty(), "An empty needle clears");
    bool refused = false;
    try {
        document.set_current_match(0U);
    } catch (const std::out_of_range &) {
        refused = true;
    }
    check(refused, "A current match needs a recorded match");
}

void marker_tests() {
    hl::CodeDocument document(hl::css_theme_sheet_language());
    document.reset("a { b: c; }\n\nd { e: f; }\n");
    const std::array<hl::SourceDiagnostic, 2U> diagnostics{
        hl::SourceDiagnostic{hl::SourcePosition{3U, 5U}, hl::DiagnosticSeverity::warning},
        hl::SourceDiagnostic{hl::SourcePosition{1U, 1U}, hl::DiagnosticSeverity::error}};
    document.set_diagnostics(diagnostics, hl::ColumnUnit::utf8_byte);
    const std::array<hl::ByteOffset, 1U> offsets{hl::ByteOffset{2U}};
    document.set_rule_offsets(offsets);
    std::vector<hl::LineMarker> markers{};
    document.line_markers(markers);
    check(markers.size() == 3U, "One marker per decoration");
    check(markers[0] == hl::LineMarker{hl::LineIndex{0U}, hl::DecorationKind::diagnostic_error, 1U} &&
              markers[1] == hl::LineMarker{hl::LineIndex{2U}, hl::DecorationKind::diagnostic_warning, 0U} &&
              markers[2] == hl::LineMarker{hl::LineIndex{0U}, hl::DecorationKind::rule_match, 0U},
          "Diagnostics first, in start order, then rules");
    check(document.next_in(hl::MarkerLayer::diagnostics, hl::ByteOffset{3U}) == 1U,
          "The next diagnostic after the caret");

    std::vector<hl::StyleRun> runs{};
    document.style_runs(range(0U, 1000U), runs);
    check(!runs.empty() && runs.back().range.end.value == document.text().size(),
          "A window past the end is clamped to the text");
    document.style_runs(range(5U, 2U), runs);
    check(runs.empty(), "A backwards window is empty");
}

} // namespace

int main() {
    try {
        edit_tests();
        update_tests();
        search_tests();
        marker_tests();
        std::cout << "Code document tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
