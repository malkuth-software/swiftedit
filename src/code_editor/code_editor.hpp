#pragma once
// SwiftEdit's embeddable code editor: a multiline text control with syntax
// highlighting (through CodeDocument), an overview strip of diagnostics and
// theme rules, a status line, and an optional small command bar (Save, Undo,
// Redo, a find field, Previous and Next). The theme editor embeds it; so may
// any program that edits a CSS-family document.
//
// The host owns what the document means: it hands over the rules that style
// the control it inspects (set_rule_offsets), the compiler's findings
// (set_diagnostics), and answers save_requested by writing text(). The editor
// owns the editing: undo and redo are the text control's, search is the
// document's, and the caret moves to whatever the user steps to.
//
// Keys, while focus is inside the editor: Ctrl/Cmd+S raises save_requested,
// Ctrl/Cmd+F focuses the find field, F3 and Shift+F3 step through matches,
// F8 and Shift+F8 through diagnostics, and Escape in the find field returns
// to the text.
//
// Syntax colours: the style runs (style_runs) are computed for any window of
// the text, but the stock TextBox paints one style, so they are not yet drawn
// in the text itself. They wait for a styled-run capability in GUI.Forms; the
// overview strip and the status line show diagnostics and rules meanwhile.
#include "code_editor/marker_strip.hpp"
#include "highlight/code_document.hpp"
#include <gui_forms/gui_forms.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace swiftedit {

// A finding the host hands over: where, how bad, and what it says.
struct CodeDiagnostic final {
    highlight::SourceDiagnostic place{};
    std::string message{};
};

class CodeEditor final : public gui_forms::Control {
public:
    static constexpr double command_bar_height = 32.0;
    static constexpr double status_height = 22.0;
    static constexpr double marker_strip_width = 12.0;
    static constexpr bool initialize_tree_after_construction = true;

    explicit CodeEditor(gui_forms::StableId id);
    ~CodeEditor() override;
    // make_control's step once the editor has an owner: builds the children.
    void initialize_control_tree();

    // Replaces the text (opening a document): the language follows
    // `file_name`'s extension, undo history and every host layer are cleared,
    // and the text counts as saved.
    void load(std::string_view text, std::string_view file_name);
    // The text as the control holds it.
    [[nodiscard]] std::string text() const;
    void set_language(const highlight::Language &language);
    [[nodiscard]] const highlight::CodeDocument &document() const noexcept { return document_; }
    [[nodiscard]] std::shared_ptr<gui_forms::TextBox> text_control() const noexcept { return text_; }
    [[nodiscard]] std::shared_ptr<gui_forms::TextBox> find_control() const noexcept { return find_; }
    [[nodiscard]] std::shared_ptr<MarkerStrip> marker_strip() const noexcept { return markers_; }
    [[nodiscard]] std::shared_ptr<gui_forms::Label> status_control() const noexcept { return status_; }

    // The command bar is shown unless the host turns it off (a host with its
    // own menus or ribbon may).
    void set_command_bar_visible(bool visible);
    [[nodiscard]] bool command_bar_visible() const noexcept { return command_bar_visible_; }

    // Theme rules, as byte offsets into the current text (see
    // CodeDocument::set_rule_offsets). Replaces the previous rules.
    void set_rule_offsets(std::span<const highlight::ByteOffset> offsets);
    void clear_rules();
    // Compiler findings, replacing the previous ones. Positions count columns
    // in `unit`.
    void set_diagnostics(std::span<const CodeDiagnostic> diagnostics, highlight::ColumnUnit unit);
    void clear_diagnostics();
    [[nodiscard]] std::span<const CodeDiagnostic> diagnostics() const noexcept { return diagnostics_; }
    // Selects the next (or previous) diagnostic after the caret, wrapping, and
    // shows its message. False when there is none.
    bool go_to_next_diagnostic();
    bool go_to_previous_diagnostic();
    // Selects the next highlighted rule after the caret, wrapping.
    bool go_to_next_rule();

    // Searches for `needle` (also what the find field does as it changes) and
    // returns the number of matches.
    std::size_t find(std::string_view needle, bool match_case);
    // Selects the next (or previous) match after the caret, wrapping.
    bool find_next();
    bool find_previous();
    void focus_find();

    // The command bar's actions, for a host's own menus.
    void request_save();
    bool undo();
    bool redo();
    // Whether the text differs from the text last loaded or marked saved.
    [[nodiscard]] bool modified() const;
    void mark_saved();

    // The style runs over `window` (see CodeDocument::style_runs).
    void style_runs(highlight::ByteRange window, std::vector<highlight::StyleRun> &runs);

    // Raised by Ctrl/Cmd+S and the Save button; the host writes text() and
    // calls mark_saved.
    [[nodiscard]] gui_forms::Event<> &save_requested() noexcept { return save_requested_; }
    // Raised after each change to the text, once the document is in step.
    [[nodiscard]] gui_forms::Event<> &changed() noexcept { return changed_; }
    // Raised when the caret moves, with its byte offset; a theme editor maps
    // it to the rule under the caret.
    [[nodiscard]] gui_forms::Event<highlight::ByteOffset> &caret_moved() noexcept { return caret_moved_; }

    void arrange(gui_forms::Rect bounds) override;
    void on_paint(gui_forms::Painter &painter, gui_forms::Rect damage) override;
    void on_key_preview(gui_forms::KeyEvent &event) override;

private:
    enum class BarAction { save, undo, redo, previous, next, match_case };
    // Retained callbacks observe the editor; an expired editor makes them
    // no-ops. The subscriptions are the editor's and end with it.
    struct TextListener {
        std::weak_ptr<CodeEditor> owner{};
        void operator()(const std::string &) const;
    };
    struct SelectionListener {
        std::weak_ptr<CodeEditor> owner{};
        void operator()(const gui_forms::TextSelection &) const;
    };
    struct FindListener {
        std::weak_ptr<CodeEditor> owner{};
        void operator()(const std::string &) const;
    };
    struct FindCommitted {
        std::weak_ptr<CodeEditor> owner{};
        void operator()(const std::string &) const;
    };
    struct BarListener {
        std::weak_ptr<CodeEditor> owner{};
        BarAction action{BarAction::save};
        void operator()(gui_forms::ButtonBase &) const;
    };
    struct MarkerListener {
        std::weak_ptr<CodeEditor> owner{};
        void operator()(const highlight::LineMarker &) const;
    };

    [[nodiscard]] std::weak_ptr<CodeEditor> observe();
    std::shared_ptr<gui_forms::Button> make_bar_button(const char *id, const char *label, BarAction action);
    void text_changed(const std::string &text);
    void selection_changed();
    void select(highlight::ByteRange range);
    void select_search_match(std::size_t index);
    [[nodiscard]] highlight::ByteOffset caret() const;
    void refresh_markers();
    void refresh_status();

    highlight::CodeDocument document_;
    std::vector<CodeDiagnostic> diagnostics_{};
    std::string saved_text_{};
    bool command_bar_visible_{true};
    // Set while the editor itself replaces the text, so the change is not
    // taken for the user's.
    bool loading_{};

    std::shared_ptr<gui_forms::Button> save_{};
    std::shared_ptr<gui_forms::Button> undo_{};
    std::shared_ptr<gui_forms::Button> redo_{};
    std::shared_ptr<gui_forms::TextBox> find_{};
    std::shared_ptr<gui_forms::CheckBox> match_case_{};
    std::shared_ptr<gui_forms::Button> previous_{};
    std::shared_ptr<gui_forms::Button> next_{};
    std::shared_ptr<gui_forms::TextBox> text_{};
    std::shared_ptr<MarkerStrip> markers_{};
    std::shared_ptr<gui_forms::Label> status_{};

    gui_forms::Event<> save_requested_{};
    gui_forms::Event<> changed_{};
    gui_forms::Event<highlight::ByteOffset> caret_moved_{};
    // Reused for the strip.
    std::vector<highlight::LineMarker> marker_scratch_{};
    // Declared last so they are revoked first, before the controls go.
    std::vector<gui_forms::SubscriptionToken> subscriptions_{};
};

} // namespace swiftedit
