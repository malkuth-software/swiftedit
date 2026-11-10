#include "editor.hpp"
#include "session.hpp"
#include "user_paths.hpp"
#include <stdexcept>

namespace notepad {
void Editor::WindowReady::operator()(gf::Window &window, gf::ApplicationWindowHandle handle) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        return;
    Editor &editor = *self;
    if (kind == WindowKind::main) {
        editor.ready(window, handle, initial);
        return;
    }
    if (kind == WindowKind::characters || kind == WindowKind::controls) {
        CharacterWindow &dialog =
            kind == WindowKind::controls ? editor.controls_ : editor.characters_;
        dialog.window = &window;
        dialog.handle = handle;
        window.set_accept_button((*dialog.root).insert_button());
        window.set_cancel_button((*dialog.root).close_button());
        return;
    }
    if (kind == WindowKind::conflict) {
        editor.conflict_.window = &window;
        editor.conflict_.handle = handle;
        window.set_cancel_button(editor.conflict_cancel_);
        return;
    }
    if (kind == WindowKind::save_choices) {
        editor.save_choices_.window = &window;
        editor.save_choices_.handle = handle;
        window.set_cancel_button(editor.save_cancel_);
        return;
    }
    Dialog &dialog = kind == WindowKind::find ? editor.find_ : editor.font_;
    dialog.window = &window;
    dialog.handle = handle;
    const std::shared_ptr<gf::Button> accept =
        kind == WindowKind::find ? editor.find_next_button_ : editor.font_apply_;
    const std::shared_ptr<gf::Button> cancel =
        kind == WindowKind::find ? editor.find_close_ : editor.font_close_;
    window.set_accept_button(accept);
    window.set_cancel_button(cancel);
}
void Editor::WindowClosing::operator()(gf::HostCloseRequest &request) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        return;
    Editor &editor = *self;
    if (kind == WindowKind::conflict) {
        editor.cancel_conflict();
        return;
    }
    if (kind == WindowKind::save_choices) {
        editor.finish_save_choice(false, true);
        return;
    }
    if (kind == WindowKind::main) {
        editor.closing(request);
        return;
    }
    if (kind == WindowKind::find)
        editor.cancel_search();
    editor.focus_text();
}
void Editor::add_dialog(std::vector<gf::ApplicationWindow> &windows, Dialog &dialog,
                        WindowKind kind, const char *id, const char *title, gf::Size size) {
    gf::ApplicationWindow child{};
    child.stable_id = id;
    child.owner_id = "notepad.main";
    child.tool_window = true;
    child.model = std::make_unique<gf::Window>(dialog.root, size);
    child.options.title = title;
    child.options.initial_size = size;
    child.options.minimum_size = size;
    child.options.initially_visible = false;
    child.options.hide_on_close = true;
    child.options.ready = WindowReady{observe(), kind, {}};
    child.options.closing = WindowClosing{observe(), kind};
    windows.push_back(std::move(child));
}
std::vector<gf::ApplicationWindow>
Editor::application_windows(const std::filesystem::path &initial) {
    const std::shared_ptr<Editor> self = std::static_pointer_cast<Editor>(shared_from_this());
    std::unique_ptr<gf::Window> window = std::make_unique<gf::Window>(self, gf::Size{940, 660});
    std::vector<gf::ApplicationWindow> result = application_windows(std::move(window), initial);
    return result;
}
std::vector<gf::ApplicationWindow>
Editor::application_windows(std::unique_ptr<gf::Window> window, const std::filesystem::path &initial) {
    if (!window || (*window).root().get() != this)
        throw std::invalid_argument("The main window must show this editor.");
    std::vector<gf::ApplicationWindow> result{};
    gf::ApplicationWindow main{};
    main.stable_id = "notepad.main";
    main.model = std::move(window);
    main.options.title = "Untitled - SwiftEdit";
    main.options.initial_size = {940, 660};
    main.options.minimum_size = {540, 320};
    main.options.ready = WindowReady{observe(), WindowKind::main, initial};
    main.options.closing = WindowClosing{observe(), WindowKind::main};
    result.push_back(std::move(main));
    add_dialog(result, find_, WindowKind::find, "notepad.find-window",
               "Find and Replace - SwiftEdit", {510, 260});
    add_dialog(result, font_, WindowKind::font, "notepad.font-window", "Font - SwiftEdit",
               {410, 220});
    add_character_window(result, false);
    add_character_window(result, true);
    add_dialog(result, save_choices_, WindowKind::save_choices, "swiftedit.save-choices-window",
               "Mixed Line Endings - SwiftEdit", {600, 180});
    add_dialog(result, conflict_, WindowKind::conflict, "swiftedit.conflict-window",
               "Review Save - SwiftEdit", {680, 420});
    return result;
}
void Editor::add_character_window(std::vector<gf::ApplicationWindow> &windows, bool controls) {
    CharacterWindow &dialog = controls ? controls_ : characters_;
    gf::ApplicationWindow child{};
    child.stable_id = controls ? "swiftedit.controls-window" : "swiftedit.characters-window";
    child.owner_id = "notepad.main";
    child.tool_window = true;
    child.model = std::make_unique<gf::Window>(dialog.root, gf::Size{680, 600});
    child.options.title =
        controls ? "Control Characters - SwiftEdit" : "Unicode Characters - SwiftEdit";
    child.options.initial_size = {680, 600};
    child.options.minimum_size = {580, 460};
    child.options.initially_visible = false;
    child.options.hide_on_close = true;
    const WindowKind kind = controls ? WindowKind::controls : WindowKind::characters;
    child.options.ready = WindowReady{observe(), kind, {}};
    child.options.closing = WindowClosing{observe(), kind};
    windows.push_back(std::move(child));
}
void Editor::CharacterInsert::operator()(const std::string &text) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (self)
        (*self).insert_character(text);
}
void Editor::CharacterClose::operator()() const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        return;
    CharacterWindow &dialog = controls ? (*self).controls_ : (*self).characters_;
    static_cast<void>(dialog.handle.hide());
    (*self).focus_text();
}
void Editor::insert_character(const std::string &value) {
    if (file_dialog_active_ || pending_save_ || conflict_review_)
        throw std::runtime_error("Finish the file dialog before inserting a character.");
    const gf::TextSelection selection = (*text_).selection();
    std::string candidate((*text_).text.read());
    candidate.replace(selection.start().value(), selection.length(), value);
    if (gf::TextBox::validate_multiline_text(candidate) != gf::TextBox::MultilineValidation::valid)
        throw std::runtime_error("Character insertion exceeds the current document or line limit.");
    show_markdown(false);
    show_csv(false);
    const bool inserted = (*text_).replace_selection(value);
    if (!inserted)
        throw std::runtime_error("The character was not inserted.");
    refresh();
}
// The file dialogs are GUI.Forms' host dialogs, shown modally over the main
// window: the host draws them (GetOpenFileNameW/GetSaveFileNameW, NSOpenPanel/
// NSSavePanel, GUI.Forms controls on X11). The editor refuses every command
// and close while one is showing.
void Editor::choose_file(bool save_as) {
    gf::Window *owner = window();
    if (!owner || !(*owner).host_services())
        throw std::runtime_error("No dialog service is attached.");
    const std::filesystem::path requested_start =
        document_.path.empty() ? user_home_directory() : document_.path.parent_path();
    // System directory aliases (for example macOS /var) name the start; the
    // dialog is given its resolved location.
    const std::filesystem::path start = std::filesystem::canonical(requested_start);
    // Every other editor window is put away for the modal choice.
    static_cast<void>(find_.handle.hide());
    static_cast<void>(font_.handle.hide());
    static_cast<void>(characters_.handle.hide());
    static_cast<void>(controls_.handle.hide());
    gf::DialogResult answer = gf::DialogResult::none;
    std::string chosen{};
    file_dialog_active_ = true;
    try {
        if (save_as) {
            if (!save_dialog_)
                save_dialog_ = std::make_unique<gf::SaveFileDialog>();
            gf::SaveFileDialog &dialog = *save_dialog_;
            dialog.title = "Save As - SwiftEdit";
            dialog.filter = std::string(file_dialog_filter);
            dialog.initial_directory = path_utf8(start);
            dialog.default_ext = "txt";
            // The editor asks itself, against the exact bytes it will replace.
            dialog.overwrite_prompt = false;
            dialog.file_name = document_.path.empty() ? swiftedit::suggested_name((*text_).text.read())
                                                      : path_utf8(document_.path.filename());
            answer = dialog.show_dialog(*owner);
            if (answer == gf::DialogResult::ok)
                chosen = dialog.file_name.read();
        } else {
            if (!open_dialog_)
                open_dialog_ = std::make_unique<gf::OpenFileDialog>();
            gf::OpenFileDialog &dialog = *open_dialog_;
            dialog.title = "Open - SwiftEdit";
            dialog.filter = std::string(file_dialog_filter);
            dialog.initial_directory = path_utf8(start);
            dialog.multiselect = false;
            answer = dialog.show_dialog(*owner);
            if (answer == gf::DialogResult::ok && !dialog.file_names().empty())
                chosen = dialog.file_names().front();
        }
    } catch (...) {
        file_dialog_active_ = false;
        after_save_ = Continuation::none;
        throw;
    }
    file_dialog_active_ = false;
    if (answer == gf::DialogResult::none) {
        after_save_ = Continuation::none;
        throw std::runtime_error("The file dialog could not be shown.");
    }
    if (answer != gf::DialogResult::ok || chosen.empty()) {
        after_save_ = Continuation::none;
        focus_text();
        return;
    }
    const std::u8string chosen_utf8(reinterpret_cast<const char8_t *>(chosen.data()), chosen.size());
    const std::filesystem::path path(chosen_utf8);
    file_chosen(save_as, path);
}
void Editor::file_chosen(bool save_as, const std::filesystem::path &path) {
    try {
        if (save_as) {
            // The editor rechecks after the dialog and owns the final
            // overwrite consent against this exact byte/identity snapshot.
            const FileSnapshot expected = read_file(path);
            if (expected.exists) {
                const gf::HostDialogChoice choice = message(
                    "Confirm Save As", "Replace " + path_utf8(path) + " with this document?",
                    gf::HostMessageButtons::yes_no);
                if (choice != gf::HostDialogChoice::yes) {
                    after_save_ = Continuation::none;
                    return;
                }
            }
            const Continuation next = after_save_;
            after_save_ = Continuation::none;
            static_cast<void>(request_save_to(path, expected, next));
        } else {
            open_file(path);
        }
    } catch (const std::exception &e) {
        after_save_ = Continuation::none;
        error(e.what());
    }
    focus_text();
}
} // namespace notepad
