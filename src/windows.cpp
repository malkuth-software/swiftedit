#include "editor.hpp"
#include "session.hpp"
#include "user_paths.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

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
    if (kind == WindowKind::open_picker || kind == WindowKind::save_picker) {
        Picker &picker =
            kind == WindowKind::save_picker ? editor.save_picker_ : editor.open_picker_;
        picker.window = &window;
        picker.handle = handle;
        (*picker.view).attach_dialog(window);
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
    if (kind == WindowKind::open_picker || kind == WindowKind::save_picker) {
        const bool save_as = kind == WindowKind::save_picker;
        Picker &picker = save_as ? editor.save_picker_ : editor.open_picker_;
        (*picker.view).cancel();
        editor.hide_picker(save_as);
        editor.after_save_ = Continuation::none;
        return;
    }
    if (kind == WindowKind::find)
        editor.cancel_search();
    editor.focus_text();
}
void Editor::PickerListener::operator()(const file_manager::DocumentPickerResult &result) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (self)
        (*self).picker_result(save_as, result);
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
    std::vector<gf::ApplicationWindow> result{};
    const std::shared_ptr<Editor> self = std::static_pointer_cast<Editor>(shared_from_this());
    gf::ApplicationWindow main{};
    main.stable_id = "notepad.main";
    main.model = std::make_unique<gf::Window>(self, gf::Size{940, 660});
    main.options.title = "Untitled - SwiftEdit";
    main.options.initial_size = {940, 660};
    main.options.minimum_size = {540, 320};
    main.options.ready = WindowReady{observe(), WindowKind::main, initial};
    main.options.closing = WindowClosing{observe(), WindowKind::main};
    result.push_back(std::move(main));
    const std::filesystem::path home = user_home_directory();
    const std::filesystem::path requested_start = initial.empty() ? home : initial.parent_path();
    // System directory aliases (for example macOS /var) identify the starting
    // directory. Grant its resolved location; do not resolve the document file.
    const std::filesystem::path start = std::filesystem::canonical(requested_start);
    for (bool save_as : {false, true}) {
        Picker &picker = save_as ? save_picker_ : open_picker_;
        file_manager::DocumentPickerRequest request{};
        request.profile = save_as ? file_manager::DocumentPickerProfile::save_as
                                  : file_manager::DocumentPickerProfile::open_file;
        request.protected_root = start.root_path();
        request.initial_location = start;
        request.owner_application_id = "org.malkuth.swiftedit";
        request.show_hidden = true;
        request.authority = file_manager::DocumentPickerAuthority::trusted_local_host;
        request.home_location = home;
#ifdef _WIN32
        const DWORD drives = GetLogicalDrives();
        for (unsigned i = 0; i < 26; ++i)
            if (drives & (1u << i)) {
                std::wstring root{static_cast<wchar_t>(L'A' + i), L':', L'\\'};
                const UINT kind = GetDriveTypeW(root.c_str());
                if (kind == DRIVE_FIXED || kind == DRIVE_REMOVABLE || kind == DRIVE_RAMDISK)
                    request.admitted_roots.emplace_back(root);
            }
#else
        request.admitted_roots.emplace_back(start.root_path());
#endif
        request.filters = {{"all", "All files", {}},
                           {"text", "Text documents", {"txt", "log", "ini", "cfg", "md"}}};
        request.active_filter_id = "all";
        request.suggested_name = "Untitled.txt";
        picker.view = std::make_unique<file_manager::DocumentPickerView>(std::move(request));
        subscriptions_.push_back(
            (*picker.view).completed().subscribe(*this, PickerListener{observe(), save_as}));
        gf::ApplicationWindow child{};
        child.stable_id = save_as ? "notepad.save-picker" : "notepad.open-picker";
        child.owner_id = "notepad.main";
        child.tool_window = true;
        child.model =
            std::make_unique<gf::Window>((*picker.view).root_control(), gf::Size{800, 600});
        child.options.title = save_as ? "Save As - SwiftEdit" : "Open - SwiftEdit";
        child.options.initial_size = {800, 600};
        child.options.minimum_size = {680, 520};
        child.options.initially_visible = false;
        child.options.hide_on_close = true;
        child.options.ready =
            WindowReady{observe(), save_as ? WindowKind::save_picker : WindowKind::open_picker, {}};
        child.options.closing =
            WindowClosing{observe(), save_as ? WindowKind::save_picker : WindowKind::open_picker};
        result.push_back(std::move(child));
    }
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
    if (picker_active_ || pending_save_ || conflict_review_)
        throw std::runtime_error("Finish the file dialog before inserting a character.");
    const gf::TextSelection selection = (*text_).selection();
    std::string candidate((*text_).text());
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
void Editor::show_picker(bool save_as) {
    Picker &picker = save_as ? save_picker_ : open_picker_;
    if (!picker.window || !picker.handle.active())
        throw std::runtime_error("The shared file picker is not ready.");
    const std::filesystem::path requested_start =
        document_.path.empty() ? user_home_directory() : document_.path.parent_path();
    const std::filesystem::path start = std::filesystem::canonical(requested_start);
    (*picker.view).set_authority_valid(true);
    if (save_as)
        static_cast<void>((*picker.view)
                              .controller()
                              .set_filename(document_.path.empty()
                                                ? swiftedit::suggested_name((*text_).text())
                                                : path_utf8(document_.path.filename())));
    (*picker.view).present(start);
    (*picker.view).attach_dialog(*picker.window);
    // Suppress every editor command during the owned selection session.
    static_cast<void>(find_.handle.hide());
    static_cast<void>(font_.handle.hide());
    static_cast<void>(characters_.handle.hide());
    static_cast<void>(controls_.handle.hide());
    picker_active_ = true;
    active_save_picker_ = save_as;
    set_enabled(false);
    const gf::HostServiceStatus status = picker.handle.show();
    if (!status.accepted()) {
        picker_active_ = false;
        set_enabled(true);
        after_save_ = Continuation::none;
        throw std::runtime_error("The file picker could not be shown.");
    }
}
void Editor::hide_picker(bool save_as) {
    Picker &picker = save_as ? save_picker_ : open_picker_;
    static_cast<void>(picker.handle.hide());
    picker_active_ = false;
    set_enabled(true);
    focus_text();
}
void Editor::picker_result(bool save_as, const file_manager::DocumentPickerResult &result) {
    try {
        Picker &picker = save_as ? save_picker_ : open_picker_;
        if (result.terminal ==
            file_manager::DocumentPickerTerminal::overwrite_confirmation_required) {
            const gf::HostDialogChoice choice =
                message("Replace existing file?", result.message, gf::HostMessageButtons::yes_no);
            if (choice == gf::HostDialogChoice::yes)
                (*picker.view).confirm_overwrite();
            return;
        }
        if (result.terminal == file_manager::DocumentPickerTerminal::cancelled) {
            hide_picker(save_as);
            after_save_ = Continuation::none;
            return;
        }
        if (!result.accepted() || result.selections.size() != 1) {
            error(result.message);
            return;
        }
        const std::filesystem::path path = result.selections.front().path;
        hide_picker(save_as);
        if (save_as) {
            // Host rechecks after the chooser and owns the final overwrite
            // consent against this exact byte/identity snapshot.
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
        } else
            open_file(path);
    } catch (const std::exception &e) {
        hide_picker(save_as);
        after_save_ = Continuation::none;
        error(e.what());
    }
}
} // namespace notepad
