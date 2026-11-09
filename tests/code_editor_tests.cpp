// The embeddable code editor in a test window: loading, edits reaching the
// document, the command bar's keys and buttons, search, diagnostics with
// their status messages, theme rules, the overview strip, and the theme
// engine bridge.
#include "code_editor/code_editor.hpp"
#include "code_editor/theme_bridge.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace gf = gui_forms;
namespace hl = swiftedit::highlight;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct SaveCounter {
    int *count{};
    void operator()() const { ++*count; }
};

gf::KeyEvent key_down(std::uint32_t key, gf::Modifier modifiers) {
    gf::KeyEvent event{};
    event.action = gf::KeyAction::down;
    event.physical_key = key;
    event.modifiers = modifiers;
    return event;
}

bool status_contains(const swiftedit::CodeEditor &editor, std::string_view words) {
    const std::string status((*editor.status_control()).text.read());
    const bool result = status.find(words) != std::string::npos;
    return result;
}

const char *const sheet = ":root { ink: #102030 on #fff; }\n"
                          "button { color: role(ink); }\n"
                          "label { colr: red; }\n";

void verify_loading_and_edits() {
    const std::shared_ptr<swiftedit::CodeEditor> editor =
        gf::make_control<swiftedit::CodeEditor>(gf::StableId("tests.code"));
    gf::Window window(editor, {720, 420});
    window.perform_layout();
    (*editor).load(sheet, "light.theme.css");
    check((*editor).document().language().name() == "css", "The file name chooses CSS");
    check((*editor).text() == sheet && !(*editor).modified(), "A loaded text counts as saved");
    check(!(*(*editor).text_control()).can_undo(), "Loading leaves no undo history");
    check((*editor).document().outline().rules().size() == 3U, "The document outlines the sheet");
    check(status_contains(*editor, "Line 1, Column 1") && status_contains(*editor, "CSS"),
          "The status line shows the caret and the language");

    gf::TextBox &text = *(*editor).text_control();
    text.select(gf::Utf8Offset(0U), gf::Utf8Offset(0U));
    text.replace_selection("/* light */\n");
    check((*editor).document().text() == text.text.read(), "An edit reaches the document");
    check((*editor).document().outline().rules().size() == 3U &&
              (*editor).document().outline().rules()[0].prelude.start.value == 12U,
          "The outline follows the edit");
    check((*editor).modified() && status_contains(*editor, "Modified"), "An edit marks the text modified");
    check((*editor).undo() && !(*editor).modified(), "Undo through the editor returns to the saved text");
    check((*editor).redo() && (*editor).modified(), "Redo reapplies the edit");
    (*editor).mark_saved();
    check(!(*editor).modified(), "Marking saved clears modified");

    (*editor).load("plain words", "notes.txt");
    check((*editor).document().language().name() == "plain" && !(*editor).document().has_outline(),
          "A text file is plain");
}

void verify_keys_and_search() {
    const std::shared_ptr<swiftedit::CodeEditor> editor =
        gf::make_control<swiftedit::CodeEditor>(gf::StableId("tests.code.keys"));
    gf::Window window(editor, {720, 420});
    window.perform_layout();
    (*editor).load(sheet, "light.theme.css");
    int saves = 0;
    gf::SubscriptionToken saved = (*editor).save_requested().subscribe(*editor, SaveCounter{&saves});
    window.request_focus((*editor).text_control());
    for (const gf::Modifier modifier : {gf::Modifier::control, gf::Modifier::meta}) {
        gf::KeyEvent save = key_down(gf::PhysicalKey::s, modifier);
        check(window.dispatch_key(save), "Ctrl/Cmd+S is handled");
    }
    check(saves == 2, "Ctrl/Cmd+S raises save_requested");

    gf::KeyEvent find = key_down(gf::PhysicalKey::f, gf::Modifier::control);
    check(window.dispatch_key(find) && window.focused_control() == (*editor).find_control(),
          "Ctrl+F focuses the find field");
    (*(*editor).find_control()).text = "ink";
    check((*editor).document().matches().items().size() == 2U, "Typing in the find field searches");
    check(status_contains(*editor, "2 matches"), "The status line counts the matches");
    gf::KeyEvent escape = key_down(gf::PhysicalKey::escape, gf::Modifier::none);
    check(window.dispatch_key(escape) && window.focused_control() == (*editor).text_control(),
          "Escape in the find field returns to the text");

    gf::TextBox &text = *(*editor).text_control();
    text.select(gf::Utf8Offset(0U), gf::Utf8Offset(0U));
    gf::KeyEvent next = key_down(gf::PhysicalKey::f3, gf::Modifier::none);
    check(window.dispatch_key(next) && text.selected_text() == "ink" &&
              (*editor).document().current_match() == 0U,
          "F3 selects the first match");
    check(window.dispatch_key(next) && (*editor).document().current_match() == 1U &&
              status_contains(*editor, "Match 2 of 2"),
          "F3 steps to the next match");
    check(window.dispatch_key(next) && (*editor).document().current_match() == 0U, "F3 wraps");
    gf::KeyEvent previous = key_down(gf::PhysicalKey::f3, gf::Modifier::shift);
    check(window.dispatch_key(previous) && (*editor).document().current_match() == 1U,
          "Shift+F3 steps back, wrapping");
    check((*editor).find("INK", true) == 0U && (*editor).find("INK", false) == 2U,
          "Match case decides ASCII case");

    (*editor).set_command_bar_visible(false);
    window.perform_layout();
    check(!(*(*editor).find_control()).visible.read(), "The command bar can be hidden");
    (*editor).focus_find();
    check((*editor).command_bar_visible(), "Find shows the bar again");
}

void verify_diagnostics_and_rules() {
    const std::shared_ptr<swiftedit::CodeEditor> editor =
        gf::make_control<swiftedit::CodeEditor>(gf::StableId("tests.code.diagnostics"));
    gf::Window window(editor, {720, 420});
    window.perform_layout();
    (*editor).load(sheet, "light.theme.css");
    window.request_focus((*editor).text_control());

    // As the theme compiler reports: one finding in this sheet, one in
    // another, one without a place.
    std::vector<gf::ThemeCompileDiagnostic> findings(3U);
    findings[0].code = "W204";
    findings[0].name = "unknown_property";
    findings[0].severity = gf::ThemeDiagnosticSeverity::warning;
    findings[0].file = "swiftedit/light.theme.css";
    findings[0].line = 3U;
    findings[0].column = 9U;
    findings[0].rule = "label";
    findings[0].property = "colr";
    findings[0].detail = "no property has this name";
    findings[1] = findings[0];
    findings[1].file = "swiftedit/dark.theme.css";
    findings[2] = findings[0];
    findings[2].line = 0U;
    std::vector<swiftedit::CodeDiagnostic> diagnostics{};
    swiftedit::append_code_diagnostics(findings, "light.theme.css", diagnostics);
    check(diagnostics.size() == 1U && diagnostics[0].place.position == hl::SourcePosition{3U, 9U} &&
              diagnostics[0].place.severity == hl::DiagnosticSeverity::warning &&
              diagnostics[0].message == "W204 unknown_property in label (colr): no property has this name",
          "The bridge keeps this sheet's placed findings");
    check(swiftedit::names_sheet("a/b.css", "b.css") && !swiftedit::names_sheet("ab.css", "b.css") &&
              swiftedit::names_sheet("anything", ""),
          "Sheet names match whole path components");

    (*editor).set_diagnostics(diagnostics, hl::ColumnUnit::unicode_scalar);
    check(status_contains(*editor, "1 finding (F8)"), "The status line counts findings");
    gf::KeyEvent next = key_down(gf::PhysicalKey::f8, gf::Modifier::none);
    check(window.dispatch_key(next) && (*(*editor).text_control()).selected_text() == "colr",
          "F8 selects the finding's token");
    check(status_contains(*editor, "Warning: W204"), "The status line shows the finding at the caret");
    check((*(*editor).marker_strip()).markers().size() == 1U &&
              (*(*editor).marker_strip()).markers()[0].line == hl::LineIndex{2U},
          "The overview strip marks the finding's line");

    const std::string text = (*editor).text();
    const std::array<hl::ByteOffset, 1U> rule{hl::ByteOffset{text.find("button")}};
    (*editor).set_rule_offsets(rule);
    check((*(*editor).marker_strip()).markers().size() == 2U, "A rule adds a mark after the findings");
    (*(*editor).text_control()).select(gf::Utf8Offset(0U), gf::Utf8Offset(0U));
    check((*editor).go_to_next_rule() &&
              (*(*editor).text_control()).selected_text() == "button { color: role(ink); }",
          "Stepping to a rule selects its extent");
    std::vector<hl::StyleRun> runs{};
    (*editor).style_runs(hl::ByteRange{hl::ByteOffset{rule[0].value}, hl::ByteOffset{rule[0].value + 6U}}, runs);
    check(runs.size() == 1U && runs[0].syntax == hl::TokenKind::type_selector &&
              runs[0].decorations.has(hl::DecorationKind::rule_match),
          "A rule's style runs carry the rule decoration over the syntax");

    const swiftedit::MarkerStrip &strip = *(*editor).marker_strip();
    const double height = 300.0;
    const double top = strip.mark_top(strip.markers()[0], height);
    check(strip.marker_at(top + 1.0, height) == 0U, "A click on a mark finds it");
    check(!strip.marker_at(top + 40.0, height).has_value(), "A click away from marks finds none");

    (*editor).clear_diagnostics();
    check((*editor).diagnostics().empty() && !(*editor).go_to_next_diagnostic(), "Clearing removes the findings");
}

} // namespace

void verify_styled_runs() {
    const std::shared_ptr<swiftedit::CodeEditor> editor =
        gf::make_control<swiftedit::CodeEditor>(gf::StableId("tests.code.styles"));
    gf::Window window(editor, {720, 420});
    window.perform_layout();
    (*editor).load(sheet, "light.theme.css");
    gf::TextBox &text = *(*editor).text_control();
    const swiftedit::StylePalette &palette = swiftedit::default_style_palette();
    std::vector<gf::StyledTextRun> runs{};
    // ":root" is a pseudo selector; "button" (line 2) a type selector.
    text.styled_runs(gf::Utf8Range{gf::Utf8Offset(0U), gf::Utf8Offset(5U)}, runs);
    const std::size_t pseudo = static_cast<std::size_t>(hl::TokenKind::pseudo_selector);
    check(!runs.empty() && runs[0].style.foreground == palette.syntax[pseudo],
          "Syntax colours reach the text box as styled runs");
    const std::size_t button = std::string_view(sheet).find("button");
    text.styled_runs(gf::Utf8Range{gf::Utf8Offset(button), gf::Utf8Offset(button + 6U)}, runs);
    const std::size_t type = static_cast<std::size_t>(hl::TokenKind::type_selector);
    check(runs.size() == 1U && runs[0].style.foreground == palette.syntax[type], "A type selector's colour");

    // Search matches get a ground, and the current match a stronger one.
    check((*editor).find("button", true) == 1U, "One match");
    text.styled_runs(gf::Utf8Range{gf::Utf8Offset(button), gf::Utf8Offset(button + 6U)}, runs);
    check(runs.size() == 1U && runs[0].style.background == palette.search_ground, "A match has a ground");
    check((*editor).find_next(), "The match is selected");
    text.styled_runs(gf::Utf8Range{gf::Utf8Offset(button), gf::Utf8Offset(button + 6U)}, runs);
    check(runs.size() == 1U && runs[0].style.background == palette.search_current_ground,
          "The current match has the stronger ground");
    static_cast<void>((*editor).find("", true));

    // Diagnostics are squiggles; edits keep the styles in step.
    std::vector<swiftedit::CodeDiagnostic> diagnostics(1U);
    diagnostics[0].place.position = hl::SourcePosition{3U, 9U};
    diagnostics[0].place.severity = hl::DiagnosticSeverity::error;
    diagnostics[0].message = "E1";
    (*editor).set_diagnostics(diagnostics, hl::ColumnUnit::unicode_scalar);
    const std::size_t colr = std::string_view(sheet).find("colr");
    text.styled_runs(gf::Utf8Range{gf::Utf8Offset(colr), gf::Utf8Offset(colr + 4U)}, runs);
    check(!runs.empty() && runs[0].style.underline == gf::TextUnderline::squiggle &&
              runs[0].style.underline_color == palette.error_underline,
          "An error is a squiggle");
    text.select(gf::Utf8Offset(0U), gf::Utf8Offset(0U));
    text.replace_selection("/* c */\n");
    text.styled_runs(gf::Utf8Range{gf::Utf8Offset(0U), gf::Utf8Offset(7U)}, runs);
    const std::size_t comment = static_cast<std::size_t>(hl::TokenKind::comment);
    check(runs.size() == 1U && runs[0].style.foreground == palette.syntax[comment], "An edit is highlighted");

    // Plain text carries no styles.
    (*editor).load("plain words", "notes.txt");
    check(text.decorations().empty(), "Plain text is not decorated");
}

int main() {
    try {
        verify_loading_and_edits();
        verify_keys_and_search();
        verify_diagnostics_and_rules();
        verify_styled_runs();
        std::cout << "Code editor tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
