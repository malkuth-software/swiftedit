#pragma once
#include "document.hpp"
#include "save_review.hpp"
#include "csv_view.hpp"
#include "markdown_view.hpp"
#include "query_field.hpp"
#include "character_picker.hpp"
#include <file_manager/document_picker_view.hpp>
#include <functional>
#include <gui_forms/application.hpp>
#include <gui_forms/gui_forms.hpp>
#include <map>

namespace notepad {
namespace gf = gui_forms;
class DialogLayout final : public gf::Control {
public:
    explicit DialogLayout(gf::StableId id) : Control(std::move(id)) {}
    void place(const Ptr &control, gf::Rect bounds) {
        add_child(control);
        items.push_back({control, bounds});
    }
    void arrange(gf::Rect bounds) override {
        arrange_self(bounds);
        for (const Placement &placement : items)
            set_child_layout(placement.child, placement.bounds);
    }

private:
    struct Placement {
        Ptr child{};
        gf::Rect bounds{};
    };
    std::vector<Placement> items{};
};
class Editor final : public gf::Control {
public:
    explicit Editor(gf::StableId id, std::function<void()> new_window = {});
    ~Editor() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter &, gf::Rect) override;
    void on_frame(gf::FrameTime) override;
    bool search_pending() const { return static_cast<bool>(search_); }
    std::vector<gf::ApplicationWindow> application_windows(const std::filesystem::path &initial);
    void execute(const std::string &command);
    // Same entrypoints are used by the shell and deterministic consumer tests.
    void open_file(const std::filesystem::path &source);
    std::shared_ptr<gf::TextBox> text_control() const { return text_; }
    const Document &document() const { return document_; }
    std::shared_ptr<CsvView> csv_control() const { return csv_; }
    std::shared_ptr<QueryField> query_control() const { return query_; }
    std::shared_ptr<gf::TextBox> replacement_control() const { return replacement_; }
    bool conflict_pending() const { return static_cast<bool>(conflict_review_); }
    bool save_choice_pending() const { return pending_save_.has_value(); }
    std::shared_ptr<CharacterPicker> character_picker(bool controls) const {
        const std::shared_ptr<CharacterPicker> result =
            controls ? controls_.root : characters_.root;
        return result;
    }

private:
    // Owns the launch callback; it must not retain the editor or its document.
    std::function<void()> new_window_{};
    struct Dialog;
    enum class Continuation { none, new_document, open_document, close_window };
    enum class WindowKind {
        main,
        open_picker,
        save_picker,
        find,
        font,
        characters,
        controls,
        save_choices,
        conflict
    };
    enum class ButtonAction {
        find_next,
        replace_one,
        replace_all,
        close_find,
        apply_font,
        close_font,
        save_as_is,
        save_normalized,
        cancel_save,
        conflict_over,
        conflict_copy,
        conflict_review,
        conflict_save,
        conflict_cancel
    };
    // All retained callbacks observe the editor. Invocation takes a temporary
    // strong reference; an expired owner makes queued work a no-op. Tokens own
    // subscriptions, not the editor. Destruction revokes tokens before controls.
    struct CommandListener {
        std::weak_ptr<Editor> owner{};
        std::string command{};
        void operator()(const gf::CommandInvocation &) const;
    };
    struct TextListener {
        std::weak_ptr<Editor> owner{};
        void operator()(const std::string &) const;
    };
    struct SelectionListener {
        std::weak_ptr<Editor> owner{};
        void operator()(const gf::TextSelection &) const;
    };
    struct CsvListener {
        std::weak_ptr<Editor> owner{};
        void operator()(const std::string &) const;
    };
    struct AcceleratorListener {
        std::weak_ptr<Editor> owner{};
        std::string command{};
        bool operator()() const;
    };
    struct ButtonListener {
        std::weak_ptr<Editor> owner{};
        ButtonAction action{ButtonAction::find_next};
        void operator()(gf::ButtonBase &) const;
    };
    struct PostedClose {
        std::weak_ptr<Editor> owner{};
        void operator()() const;
    };
    struct WindowReady {
        std::weak_ptr<Editor> owner{};
        WindowKind kind{WindowKind::main};
        std::filesystem::path initial{};
        void operator()(gf::Window &, gf::ApplicationWindowHandle) const;
    };
    struct WindowClosing {
        std::weak_ptr<Editor> owner{};
        WindowKind kind{WindowKind::main};
        void operator()(gf::HostCloseRequest &) const;
    };
    struct PickerListener {
        std::weak_ptr<Editor> owner{};
        bool save_as{};
        void operator()(const file_manager::DocumentPickerResult &) const;
    };
    struct CharacterInsert {
        std::weak_ptr<Editor> owner{};
        void operator()(const std::string &) const;
    };
    struct CharacterClose {
        std::weak_ptr<Editor> owner{};
        bool controls{};
        void operator()() const;
    };
    struct CharacterWindow {
        std::shared_ptr<CharacterPicker> root{};
        gf::Window *window{};
        gf::ApplicationWindowHandle handle{};
    };
    void insert_character(const std::string &);
    void add_character_window(std::vector<gf::ApplicationWindow> &, bool);
    std::weak_ptr<Editor> observe();
    static std::shared_ptr<Editor> lock_alive(const std::weak_ptr<Editor> &);
    void on_dispose() noexcept override;
    void continue_operation(Continuation);
    void button_action(ButtonAction);
    void place_find_label(const char *, const char *, gf::Rect);
    std::shared_ptr<gf::Button> place_find_button(const char *, const char *, gf::Rect,
                                                  ButtonAction);
    void add_dialog(std::vector<gf::ApplicationWindow> &, Dialog &, WindowKind, const char *,
                    const char *, gf::Size);
    struct Picker {
        std::unique_ptr<file_manager::DocumentPickerView> view{};
        gf::Window *window{};
        gf::ApplicationWindowHandle handle{};
    };
    struct Dialog {
        std::shared_ptr<DialogLayout> root{};
        gf::Window *window{};
        gf::ApplicationWindowHandle handle{};
    };
    gf::MenuItemSpec item(std::string id, std::string label, std::string shortcut = {});
    void ready(gf::Window &, gf::ApplicationWindowHandle, const std::filesystem::path &);
    void closing(gf::HostCloseRequest &);
    void refresh();
    void refresh_title(std::string_view content);
    void refresh_selection();
    void apply_csv_change(const std::string &);
    void show_csv(bool);
    void show_markdown(bool);
    void focus_text();
    void error(const std::string &);
    gf::HostDialogChoice message(std::string title, std::string text,
                                 gf::HostMessageButtons buttons = gf::HostMessageButtons::ok);
    void after_unsaved(Continuation continuation);
    void save(bool save_as, Continuation continuation = Continuation::none);
    bool save_to(const std::filesystem::path &, const FileSnapshot &);
    bool request_save_to(const std::filesystem::path &, const FileSnapshot &, Continuation);
    void finish_save_choice(bool normalize, bool cancel);
    void build_save_choices();
    void build_conflict();
    void begin_conflict(const std::filesystem::path &, Continuation);
    void conflict_action(ButtonAction);
    void cancel_conflict();
    swiftedit::DocumentStamp save_stamp() const;
    void show_picker(bool save_as);
    void picker_result(bool save_as, const file_manager::DocumentPickerResult &);
    void hide_picker(bool save_as);
    void show_find();
    void find_next();
    void advance_search();
    void advance_replacement();
    void cancel_search();
    void replace_one();
    void replace_every();
    void build_find();
    void build_font();
    void close_dialog(Dialog &);
    void shortcut(gf::Window &, std::uint32_t, gf::Modifier, const std::string &);
    Document document_{};
    std::shared_ptr<gf::TextBox> text_{};
    std::shared_ptr<CsvView> csv_{};
    bool csv_visible_{};
    std::shared_ptr<MarkdownView> markdown_{};
    bool markdown_visible_{};
    std::shared_ptr<gf::MenuStrip> menu_{};
    std::shared_ptr<gf::Label> status_{};
    std::string native_title_{};
    std::map<std::string, std::shared_ptr<gf::Command>> commands_{};
    std::vector<gf::SubscriptionToken> subscriptions_{};
    std::vector<gf::AcceleratorToken> accelerators_{};
    gf::ApplicationWindowHandle handle_{};
    Picker open_picker_{}, save_picker_{};
    Dialog find_{}, font_{};
    Dialog conflict_{};
    std::unique_ptr<swiftedit::SaveReview> conflict_review_{};
    // Allocates a unique GUI document identity until the shared Session migration.
    swiftedit::Session save_identity_{};
    Continuation conflict_next_{Continuation::none};
    std::shared_ptr<gf::TextBox> conflict_path_{};
    std::shared_ptr<gf::ComboBox> conflict_encoding_{}, conflict_endings_{};
    std::shared_ptr<gf::Label> conflict_status_{};
    std::shared_ptr<gf::Button> conflict_over_{}, conflict_copy_{}, conflict_check_{},
        conflict_save_{}, conflict_cancel_{};
    Dialog save_choices_{};
    std::shared_ptr<gf::Button> save_keep_{}, save_normalize_{}, save_cancel_{};
    struct PendingSave {
        std::filesystem::path path{};
        FileSnapshot expected{};
        std::uint64_t revision{};
        Continuation continuation{Continuation::none};
    };
    std::optional<PendingSave> pending_save_{};
    CharacterWindow characters_{}, controls_{};
    std::shared_ptr<QueryField> query_{};
    std::shared_ptr<gf::TextBox> replacement_{};
    std::shared_ptr<gf::CheckBox> match_case_{};
    std::shared_ptr<gf::Label> find_status_{};
    std::shared_ptr<gf::Button> find_next_button_{}, find_close_{}, font_apply_{}, font_close_{};
    std::shared_ptr<gf::ComboBox> font_role_{}, font_size_{};
    std::shared_ptr<gf::CheckBox> font_bold_{}, font_italic_{};
    bool show_status_{true}, picker_active_{}, active_save_picker_{}, close_authorized_{};
    std::uint64_t dialog_sequence_{1};
    Continuation after_save_{Continuation::none};
    // UI-thread-owned Unicode metadata, reused across selection-only refreshes.
    gf::TextStore counted_text_{};
    std::size_t character_count_{};
    std::string status_format_{};
    bool selection_metadata_current_{};
    struct FindWork {
        std::unique_ptr<swiftedit::PatternScan> scan{};
        std::unique_ptr<swiftedit::ReplacementScan> replace{};
        std::string replacement{};
        std::uint64_t source_revision{}, query_revision{};
        gf::TextSelection selection{};
        bool match_case{}, wrapped{};
        FindWork(std::string_view, swiftedit::SearchPattern, std::size_t, bool);
        FindWork(std::string_view, swiftedit::SearchPattern, std::string, bool);
    };
    std::unique_ptr<FindWork> search_{};
    gf::FrameRequestToken search_frame_{};
};
std::string path_utf8(const std::filesystem::path &);
} // namespace notepad
