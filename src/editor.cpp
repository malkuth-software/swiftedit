#include "editor.hpp"
#include "session.hpp"
#include "characters.hpp"
#include "date_time.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace notepad {
std::string path_utf8(const std::filesystem::path &path) {
    const std::u8string s = path.u8string();
    const std::string result(reinterpret_cast<const char *>(s.data()), s.size());
    return result;
}
Editor::Editor(gf::StableId id, std::function<void()> new_window)
    : Control(std::move(id)), new_window_(std::move(new_window)) {}

Editor::~Editor() {
    accelerators_.clear();
    subscriptions_.clear();
}
void Editor::on_dispose() noexcept {
    cancel_search();
    pending_save_.reset();
    conflict_review_.reset();
    after_save_ = Continuation::none;
    close_authorized_ = false;
    accelerators_.clear();
    subscriptions_.clear();
    gf::Control::on_dispose();
}
std::shared_ptr<Editor> Editor::lock_alive(const std::weak_ptr<Editor> &owner) {
    std::shared_ptr<Editor> editor = owner.lock();
    if (editor && !(*editor).is_alive())
        editor.reset();
    return editor;
}
std::weak_ptr<Editor> Editor::observe() {
    const std::shared_ptr<Editor> self = std::static_pointer_cast<Editor>(shared_from_this());
    const std::weak_ptr<Editor> observer = self;
    return observer;
}
void Editor::CommandListener::operator()(const gf::CommandInvocation &) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (self)
        (*self).execute(command);
}
void Editor::TextListener::operator()(const std::string &) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (self) {
        (*self).cancel_search();
        (*self).refresh();
    }
}
void Editor::SelectionListener::operator()(const gf::TextSelection &) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (self)
        (*self).refresh_selection();
}
void Editor::CsvListener::operator()(const std::string &source) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        throw std::runtime_error("Document is no longer available.");
    (*self).apply_csv_change(source);
}
bool Editor::AcceleratorListener::operator()() const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        return false;
    (*self).execute(command);
    return true;
}
void Editor::ButtonListener::operator()(gf::ButtonBase &) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        return;
    try {
        (*self).button_action(action);
    } catch (const std::exception &failure) {
        (*(*self).find_status_).set_text(failure.what());
    }
}
void Editor::PostedClose::operator()() const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (self)
        static_cast<void>((*self).handle_.request_close());
}
void Editor::continue_operation(Continuation next) {
    switch (next) {
    case Continuation::none:
        break;
    case Continuation::new_document:
        cancel_search();
        show_markdown(false);
        show_csv(false);
        save_identity_.reset();
        document_ = {};
        (*text_).set_text("");
        (*text_).set_newline_sequence(native_newline());
        refresh();
        focus_text();
        break;
    case Continuation::open_document:
        show_picker(false);
        break;
    case Continuation::close_window:
        close_authorized_ = true;
        if (window())
            static_cast<void>((*window()).begin_invoke(shared_from_this(), PostedClose{observe()}));
        break;
    }
}
void Editor::button_action(ButtonAction action) {
    switch (action) {
    case ButtonAction::conflict_over:
    case ButtonAction::conflict_copy:
    case ButtonAction::conflict_review:
    case ButtonAction::conflict_save:
    case ButtonAction::conflict_cancel:
        conflict_action(action);
        break;
    case ButtonAction::find_next:
        find_next();
        break;
    case ButtonAction::replace_one:
        replace_one();
        break;
    case ButtonAction::replace_all:
        replace_every();
        break;
    case ButtonAction::close_find:
        cancel_search();
        close_dialog(find_);
        break;
    case ButtonAction::close_font:
        close_dialog(font_);
        break;
    case ButtonAction::save_as_is:
        finish_save_choice(false, false);
        break;
    case ButtonAction::save_normalized:
        finish_save_choice(true, false);
        break;
    case ButtonAction::cancel_save:
        finish_save_choice(false, true);
        break;
    case ButtonAction::apply_font: {
        const gf::FontRole role =
            (*font_role_).selected_index() == 0 ? gf::FontRole::monospace : gf::FontRole::content;
        const std::string size_text((*font_size_).selected_text());
        const double size = std::stod(size_text);
        const std::uint16_t weight = (*font_bold_).checked() ? 700 : 400;
        const gf::FontSpec font{role, size, weight, (*font_italic_).checked()};
        (*text_).set_font(font);
        break;
    }
    }
}
void Editor::place_find_label(const char *id, const char *text, gf::Rect bounds) {
    const std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(gf::StableId(id), text);
    (*find_.root).place(label, bounds);
}
std::shared_ptr<gf::Button> Editor::place_find_button(const char *id, const char *text,
                                                      gf::Rect bounds, ButtonAction action) {
    const std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(gf::StableId(id), text);
    (*find_.root).place(button, bounds);
    gf::SubscriptionToken subscription =
        (*button).clicked().subscribe(*this, ButtonListener{observe(), action});
    subscriptions_.push_back(std::move(subscription));
    return button;
}

gf::MenuItemSpec Editor::item(std::string id, std::string label, std::string shortcut_text) {
#ifdef __APPLE__
    if (id == "exit") {
        label = "&Close Window";
        shortcut_text = "Cmd+W";
    } else if (shortcut_text.starts_with("Ctrl+"))
        shortcut_text.replace(0, 5, "Cmd+");
#endif
    std::shared_ptr<gf::Command> command = std::make_shared<gf::Command>(id, label);
    (*command).set_shortcut(shortcut_text);
    subscriptions_.push_back((*command).invoked().subscribe(*this, CommandListener{observe(), id}));
    commands_[id] = command;
    const bool toggle = id == "wrap" || id == "status" || id == "csv-view" || id == "markdown-view";
    const gf::MenuItemKind kind = toggle ? gf::MenuItemKind::check : gf::MenuItemKind::command;
    const gf::MenuItemSpec result{id, kind, command, label};
    return result;
}
void Editor::initialize_control_tree() {
    menu_ = gf::make_control<gf::MenuStrip>(gf::StableId("notepad.menus"));
    (*menu_).set_items(
        {{"file",
          "&File",
          {item("new", "&New", "Ctrl+N"),
           item("new-window", "New &Window", "Ctrl+Shift+N"),
           item("open", "&Open...", "Ctrl+O"),
           item("save", "&Save", "Ctrl+S"), item("save-as", "Save &As...", "Ctrl+Shift+S"),
           item("exit", "E&xit", "Alt+F4")}},
         {"edit",
          "&Edit",
          {item("undo", "&Undo", "Ctrl+Z"), item("redo", "&Redo", "Ctrl+Y"),
           item("cut", "Cu&t", "Ctrl+X"), item("copy", "&Copy", "Ctrl+C"),
           item("paste", "&Paste", "Ctrl+V"), item("delete", "&Delete", "Del"),
           item("find", "&Find...", "Ctrl+F"), item("find-next", "Find &Next", "F3"),
           item("replace", "&Replace...", "Ctrl+H"), item("select-all", "Select &All", "Ctrl+A")}},
         {"document",
          "&Document",
          {item("restore-opened", "Restore As &Opened..."),
           item("date-time", "Insert &Date and Time", "F5"), item("word-count", "&Word Count..."),
           item("inspect-characters", "Inspect &Characters..."),
           item("characters", "Insert &Unicode Character..."),
           item("controls", "Insert &Control Character..."),
           item("newline-lf", "Convert Line Endings to &LF"),
           item("newline-crlf", "Convert Line Endings to &CRLF")}},
         {"format", "F&ormat", {item("wrap", "&Word Wrap"), item("font", "&Font...")}},
         {"view",
          "&View",
          {item("status", "&Status Bar"), item("csv-view", "CSV &Table View"),
           item("markdown-view", "Markdown &Rendered View")}},
         {"csv",
          "&CSV",
          {item("csv-convert-value", "Convert to &Value"), item("csv-clear", "&Clear Cells")}},
         {"help", "&Help", {item("help", "View &Help", "F1"), item("about", "&About SwiftEdit")}}});
    add_child(menu_);
    text_ = gf::make_control<gf::TextBox>(gf::StableId("notepad.document"));
    (*text_).set_multiline(true);
    (*text_).set_auto_scroll(true);
    (*text_).set_word_wrap(false);
    (*text_).set_accepts_tab(true);
    (*text_).set_newline_sequence(native_newline());
    (*text_).set_maximum_length(gf::TextBox::maximum_multiline_bytes);
    (*text_).set_font({gf::FontRole::monospace, 14, 400, false});
    (*text_).set_accessible_name("Document text");
    add_child(text_);
    csv_ = gf::make_control<CsvView>(gf::StableId("swiftedit.csv"));
    (*csv_).set_visible(false);
    (*csv_).set_context_command(commands_.at("csv-convert-value"));
    subscriptions_.push_back((*csv_).changed().subscribe(*this, CsvListener{observe()}));
    add_child(csv_);
    markdown_ = gf::make_control<MarkdownView>(gf::StableId("swiftedit.markdown"));
    (*markdown_).set_visible(false);
    add_child(markdown_);
    status_ = gf::make_control<gf::Label>(gf::StableId("notepad.status"));
    (*status_).set_use_mnemonic(false);
    add_child(status_);
    subscriptions_.push_back((*text_).text_changed().subscribe(*this, TextListener{observe()}));
    subscriptions_.push_back(
        (*text_).selection_changed().subscribe(*this, SelectionListener{observe()}));
    build_find();
    build_font();
    build_save_choices();
    build_conflict();
    characters_.root =
        gf::make_control<CharacterPicker>(gf::StableId("swiftedit.characters"), false);
    controls_.root = gf::make_control<CharacterPicker>(gf::StableId("swiftedit.controls"), true);
    subscriptions_.push_back(
        (*characters_.root).inserted().subscribe(*this, CharacterInsert{observe()}));
    subscriptions_.push_back(
        (*controls_.root).inserted().subscribe(*this, CharacterInsert{observe()}));
    subscriptions_.push_back(
        (*characters_.root).closed().subscribe(*this, CharacterClose{observe(), false}));
    subscriptions_.push_back(
        (*controls_.root).closed().subscribe(*this, CharacterClose{observe(), true}));
    refresh();
}
void Editor::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    set_child_layout(menu_, {0, 0, bounds.width, 28});
    const double bottom = show_status_ ? 26 : 0;
    set_child_layout(text_, {0, 28, bounds.width, std::max(0.0, bounds.height - 28 - bottom)});
    set_child_layout(csv_, {0, 28, bounds.width, std::max(0.0, bounds.height - 28 - bottom)});
    set_child_layout(markdown_, {0, 28, bounds.width, std::max(0.0, bounds.height - 28 - bottom)});
    set_child_layout(status_, {8, std::max(28.0, bounds.height - bottom) + 4,
                               std::max(0.0, bounds.width - 16), std::max(0.0, bottom - 8)});
}
void Editor::on_paint(gf::Painter &painter, gf::Rect) {
    if (!show_status_)
        return;
    const gf::Rect bounds = committed_arranged_bounds();
    const double top = std::max(28.0, bounds.height - 26.0);
    const gf::BasicControlStyle &style = effective_theme().basic_style();
    painter.fill_rect({0, top, bounds.width, 26}, style.face);
    painter.draw_line({0, top + 0.5}, {bounds.width, top + 0.5}, style.border, 1);
    if (bounds.width < 10)
        return;
    const double left = 3.5;
    const double right = bounds.width - 3.5;
    const double inset_top = top + 3.5;
    const double inset_bottom = top + 22.5;
    painter.draw_line({left, inset_top}, {right, inset_top}, style.border, 1);
    painter.draw_line({left, inset_top}, {left, inset_bottom}, style.border, 1);
    painter.draw_line({left, inset_bottom}, {right, inset_bottom}, style.face_light, 1);
    painter.draw_line({right, inset_top}, {right, inset_bottom}, style.face_light, 1);
}
void Editor::refresh() {
    if (!text_ || !status_)
        return;
    selection_metadata_current_ = false;
    const std::string_view content = (*text_).text();
    if (csv_visible_)
        (*csv_).set_source(content);
    if (markdown_visible_)
        (*markdown_).set_source(content);
    (*commands_.at("csv-convert-value")).set_enabled(csv_visible_);
    (*commands_.at("csv-clear")).set_enabled(csv_visible_);
    (*commands_.at("csv-view")).set_checked(csv_visible_);
    (*commands_.at("markdown-view")).set_checked(markdown_visible_);
    if (content != counted_text_.utf8()) {
        counted_text_.set_text(content);
        character_count_ = counted_text_.grapheme_count().value();
    }
    status_format_ = encoding_name(document_.encoding) + " | " + newline_name(content);
    selection_metadata_current_ = true;
    refresh_selection();
    (*commands_.at("undo")).set_enabled((*text_).can_undo());
    (*commands_.at("redo")).set_enabled((*text_).can_redo());
    (*commands_.at("wrap")).set_checked((*text_).word_wrap());
    (*commands_.at("status")).set_checked(show_status_);
    refresh_title(content);
}
void Editor::refresh_title(const std::string_view content) {
    if (!handle_.active())
        return;
    std::string title = document_.dirty(content) ? "* " : "";
    title += document_.path.empty() ? "Untitled" : path_utf8(document_.path.filename());
    title += " - SwiftEdit";
    if (title == native_title_)
        return;
    const gf::HostServiceStatus changed = handle_.set_title(title);
    if (changed.accepted())
        native_title_ = std::move(title);
    else
        (*status_).set_text(title + " | Native title update unavailable (host error " +
                            std::to_string(static_cast<unsigned>(changed.error)) + ").");
}
void Editor::refresh_selection() {
    // The pinned TextBox publishes text changes before selection changes. A
    // text/save refresh prepares all content metadata; caret motion borrows it.
    // Failed preparation leaves it invalid, so later motion retries safely.
    if (!selection_metadata_current_) {
        refresh();
        return;
    }
    const std::size_t caret =
        std::min((*text_).selection().caret.value(), counted_text_.utf8_size().value());
    // Find the last indexed line start at or before the caret. Line starts
    // include zero even for an empty document; low is the one-based line number.
    std::size_t low = 0, high = counted_text_.line_count();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (counted_text_.line_start(gf::LineIndex(middle)).value() <= caret)
            low = middle + 1;
        else
            high = middle;
    }
    const std::size_t line = low;
    const gf::Utf8Offset line_start = counted_text_.line_start(gf::LineIndex(line - 1));
    const std::size_t line_character = counted_text_.grapheme_index(line_start).value();
    const std::size_t caret_character = counted_text_.grapheme_index(gf::Utf8Offset(caret)).value();
    const std::size_t column = caret_character - line_character + 1;
    const gf::TextSelection selection = (*text_).selection();
    const std::size_t first_character = counted_text_.grapheme_index(selection.start()).value();
    const std::size_t last_character = counted_text_.grapheme_index(selection.end()).value();
    const std::size_t selected = last_character - first_character;
    (*status_).set_text("Ln " + std::to_string(line) + ", Col " + std::to_string(column) + " | " +
                        std::to_string(character_count_) + " characters | " +
                        std::to_string(selected) + " selected | " + status_format_);
    for (const char *id : {"cut", "copy", "delete"})
        (*commands_.at(id)).set_enabled(csv_visible_ || !(*text_).selection().empty());
}
void Editor::focus_text() {
    if (window() && markdown_visible_)
        (*window()).request_focus(markdown_);
    else if (window())
        (*window()).request_focus(csv_visible_ ? std::static_pointer_cast<gf::Control>(csv_)
                                               : std::static_pointer_cast<gf::Control>(text_));
}
void Editor::apply_csv_change(const std::string &source) {
    if (source == (*text_).text())
        return;
    const gf::TextBox::MultilineValidation supported = gf::TextBox::validate_multiline_text(source);
    if (supported != gf::TextBox::MultilineValidation::valid)
        throw std::runtime_error(
            "CSV edit exceeds the current text widget limits; source is unchanged.");
    const gf::TextSelection previous = (*text_).selection();
    (*text_).select_all();
    const bool changed = (*text_).replace_selection(source);
    if (!changed) {
        (*text_).select(previous.anchor, previous.caret);
        throw std::runtime_error("CSV edit was not applied.");
    }
    refresh();
}
void Editor::show_csv(bool show) {
    if (show) {
        std::wstring extension = document_.path.extension().wstring();
        for (wchar_t &character : extension)
            if (character >= L'A' && character <= L'Z')
                character += 32;
        if (extension != L".csv")
            throw std::runtime_error("Table view is available only for .csv files.");
        (*csv_).set_source((*text_).text());
        show_markdown(false);
    }
    csv_visible_ = show;
    if (!show)
        (*csv_).cancel_calculations();
    (*text_).set_visible(!show);
    (*csv_).set_visible(show);
    refresh();
    focus_text();
}
void Editor::show_markdown(bool show) {
    if (show) {
        (*markdown_).set_source((*text_).text());
        if (csv_visible_)
            show_csv(false);
    }
    markdown_visible_ = show;
    if (!show)
        (*markdown_).cancel_preparation();
    (*text_).set_visible(!show && !csv_visible_);
    (*markdown_).set_visible(show);
    refresh();
    focus_text();
}
gf::HostDialogChoice Editor::message(std::string title, std::string text,
                                     gf::HostMessageButtons buttons) {
    gf::Window *owner = picker_active_
                            ? (active_save_picker_ ? save_picker_.window : open_picker_.window)
                            : window();
    if (!owner || !(*owner).host_services())
        throw std::runtime_error("No dialog service is attached.");
    gf::HostMessageDialogRequest payload{};
    payload.title = std::move(title);
    payload.message = std::move(text);
    payload.buttons = buttons;
    payload.default_choice = buttons == gf::HostMessageButtons::ok ? gf::HostDialogChoice::ok
                             : buttons == gf::HostMessageButtons::yes_no
                                 ? gf::HostDialogChoice::no
                                 : gf::HostDialogChoice::cancel;
    gf::HostDialogRequest request{};
    request.request_id = dialog_sequence_++;
    request.owner_id = picker_active_
                           ? (active_save_picker_ ? "notepad.save-picker" : "notepad.open-picker")
                           : "notepad.main";
    request.payload = std::move(payload);
    const gf::HostDialogResult result = (*(*owner).host_services()).show_dialog(request);
    if (!result.status.accepted())
        throw std::runtime_error("The dialog service could not complete the request.");
    const gf::HostMessageDialogResult &response =
        std::get<gf::HostMessageDialogResult>(result.payload);
    return response.choice;
}
void Editor::error(const std::string &text) {
    (*status_).set_text(text);
    (*status_).set_visible(true);
    try {
        message("SwiftEdit", text);
    } catch (...) { /* Keep the error visible if native services fail. */
    }
}
void Editor::after_unsaved(Continuation next) {
    if (!document_.dirty((*text_).text())) {
        continue_operation(next);
        return;
    }
    const gf::HostDialogChoice choice = message(
        "Save changes?",
        "Save changes to " +
            (document_.path.empty() ? "Untitled" : path_utf8(document_.path.filename())) + "?",
        gf::HostMessageButtons::yes_no_cancel);
    if (choice == gf::HostDialogChoice::no)
        continue_operation(next);
    else if (choice == gf::HostDialogChoice::yes)
        save(false, next);
}
void Editor::open_file(const std::filesystem::path &source) {
    if (pending_save_ || conflict_review_)
        throw std::runtime_error(
            "Finish or cancel the pending save before opening another document.");
    Document next{};
    next.open(source);
    const gf::TextBox::MultilineValidation supported =
        gf::TextBox::validate_multiline_text(next.saved_text);
    if (supported != gf::TextBox::MultilineValidation::valid)
        throw std::runtime_error(
            "This development editor supports up to 1 MiB of UTF-8 text and 4096 UTF-8 bytes per "
            "logical line. The current document was kept.");
    show_markdown(false);
    show_csv(false);
    (*text_).set_text(next.saved_text);
    cancel_search();
    save_identity_.reset();
    document_ = std::move(next);
    (*text_).set_newline_sequence(preferred_newline(document_.saved_text));
    (*text_).select(gf::Utf8Offset(0), gf::Utf8Offset(0));
    refresh();
    focus_text();
}
bool Editor::save_to(const std::filesystem::path &path, const FileSnapshot &expected) {
    try {
        document_.save(path, (*text_).text(), expected);
        (*text_).clear_undo_history();
        refresh();
        return true;
    } catch (const std::exception &e) {
        error(e.what());
        return false;
    }
}
void Editor::save(bool save_as, Continuation continuation) {
    if (save_as || document_.path.empty()) {
        after_save_ = continuation;
        show_picker(true);
    } else {
        static_cast<void>(request_save_to(document_.path, document_.snapshot, continuation));
    }
}
void Editor::execute(const std::string &id) {
    if (conflict_review_) {
        if (id == "conflict-over")
            conflict_action(ButtonAction::conflict_over);
        else if (id == "conflict-copy")
            conflict_action(ButtonAction::conflict_copy);
        else if (id == "conflict-review")
            conflict_action(ButtonAction::conflict_review);
        else if (id == "conflict-save")
            conflict_action(ButtonAction::conflict_save);
        else if (id == "cancel-save")
            cancel_conflict();
        return;
    }
    if (pending_save_) {
        if (id == "save-as-is")
            finish_save_choice(false, false);
        else if (id == "save-normalized")
            finish_save_choice(true, false);
        else if (id == "cancel-save")
            finish_save_choice(false, true);
        return;
    }
    if (picker_active_)
        return;
    try {
        const bool source_search = id == "find" || id == "replace" || id == "find-next" ||
                                   id == "replace-one" || id == "replace-all";
        if (source_search) {
            if (csv_visible_)
                show_csv(false);
            if (markdown_visible_)
                show_markdown(false);
        }
        if (markdown_visible_ &&
            (id == "paste" || id == "cut" || id == "delete" || id == "date-time"))
            show_markdown(false);
        if (id == "new")
            after_unsaved(Continuation::new_document);
        else if (id == "new-window") {
            if (!new_window_)
                throw std::runtime_error("New Window is unavailable in this host.");
            new_window_();
        }
        else if (id == "open")
            after_unsaved(Continuation::open_document);
        else if (id == "save")
            save(false);
        else if (id == "save-as")
            save(true);
        else if (id == "exit")
            static_cast<void>(handle_.request_close());
        else if (id == "csv-view")
            show_csv(!csv_visible_);
        else if (id == "markdown-view")
            show_markdown(!markdown_visible_);
        else if (id == "csv-convert-value")
            (*csv_).convert_to_value();
        else if (id == "csv-clear")
            (*csv_).clear_cells();
        else if (id == "undo")
            (*text_).undo();
        else if (id == "redo")
            (*text_).redo();
        else if (id == "cut" || id == "copy") {
            if (csv_visible_) {
                if (!window() || !(*window()).host_services())
                    throw std::runtime_error("Clipboard service is unavailable.");
                const std::string copied = (*csv_).copy_cells();
                const gf::HostServiceStatus status =
                    (*(*window()).host_services()).write_clipboard_text(copied);
                if (!status.accepted())
                    throw std::runtime_error("Clipboard copy failed; cells are unchanged.");
                if (id == "cut")
                    (*csv_).clear_cells();
            } else if (id == "cut")
                (*text_).cut();
            else
                (*text_).copy();
        } else if (id == "paste") {
            if (!window() || !(*window()).host_services())
                throw std::runtime_error("Clipboard service is unavailable.");
            const gf::HostClipboardTextResult clip =
                (*(*window()).host_services()).read_clipboard_text();
            if (!clip.status.accepted())
                throw std::runtime_error("Cannot read plain-text clipboard.");
            if (clip.text_utf8.size() > swiftedit::paste_confirmation_bytes) {
                const gf::HostDialogChoice choice = message(
                    "Large paste",
                    "Paste " + std::to_string(clip.text_utf8.size()) + " bytes of plain text?",
                    gf::HostMessageButtons::yes_no);
                if (choice != gf::HostDialogChoice::yes)
                    return;
            }
            if (csv_visible_) {
                (*csv_).commit_cell(clip.text_utf8);
                return;
            }
            std::string candidate = std::string((*text_).text());
            candidate.replace((*text_).selection().start().value(), (*text_).selection().length(),
                              clip.text_utf8);
            if (gf::TextBox::validate_multiline_text(candidate) !=
                gf::TextBox::MultilineValidation::valid)
                throw std::runtime_error("Paste exceeds this GUI's document/line limits or "
                                         "contains invalid UTF-8. No text was changed.");
            (*text_).replace_selection(clip.text_utf8);
        } else if (id == "characters" || id == "controls") {
            show_markdown(false);
            show_csv(false);
            CharacterWindow &dialog = id == "controls" ? controls_ : characters_;
            const gf::HostServiceStatus result = dialog.handle.show();
            if (!result.accepted())
                throw std::runtime_error("The character dialog is not ready.");
        } else if (id == "inspect-characters") {
            const gf::TextSelection selected = (*text_).selection();
            const std::string_view content = (*text_).text();
            std::size_t first = selected.start().value();
            std::size_t length = selected.length();
            if (!length && first < content.size()) {
                const gf::Utf8Offset end =
                    counted_text_.next_grapheme_boundary(gf::Utf8Offset(first));
                length = end.value() - first;
            }
            const std::string report = swiftedit::inspect_characters(content.substr(first, length));
            message("Character Inspector", report);
        } else if (id == "restore-opened") {
            const gf::HostDialogChoice choice =
                message("Restore as opened?",
                        "Replace the working text with the original opened text? The file on disk "
                        "will not change until you save.",
                        gf::HostMessageButtons::yes_no);
            if (choice == gf::HostDialogChoice::yes) {
                (*text_).select_all();
                (*text_).replace_selection(document_.opened_text);
            }
        } else if (id == "word-count") {
            const std::size_t words = word_count((*text_).text());
            std::string report = "Document: " + std::to_string(words) + " words";
            if (!(*text_).selection().empty()) {
                const std::string selected = (*text_).selected_text();
                const std::size_t selected_words = word_count(selected);
                report += "\nSelection: " + std::to_string(selected_words) + " words";
            }
            report += "\n\nWords are runs separated by whitespace. Punctuation stays with its "
                      "word; languages without spaces are not segmented.";
            message("Word Count", report);
        } else if (id == "date-time") {
            const std::string timestamp = current_date_time();
            if (csv_visible_)
                (*csv_).commit_cell(timestamp);
            else
                (*text_).replace_selection(timestamp);
        } else if (id == "newline-lf" || id == "newline-crlf") {
            const std::string ending = id == "newline-lf" ? "\n" : "\r\n";
            const std::string converted = swiftedit::normalize_newlines((*text_).text(), ending);
            if (gf::TextBox::validate_multiline_text(converted) !=
                gf::TextBox::MultilineValidation::valid)
                throw std::runtime_error("Converted text exceeds GUI limits.");
            (*text_).select_all();
            (*text_).replace_selection(converted);
            (*text_).set_newline_sequence(ending);
        } else if (id == "delete") {
            if (csv_visible_)
                (*csv_).clear_cells();
            else
                (*text_).delete_selection();
        } else if (id == "select-all") {
            if (csv_visible_)
                (*csv_).select_all();
            else
                (*text_).select_all();
        } else if (id == "replace-all")
            replace_every();
        else if (id == "replace-one")
            replace_one();
        else if (id == "find" || id == "replace")
            show_find();
        else if (id == "find-next") {
            if ((*query_).text().empty())
                show_find();
            else
                find_next();
        } else if (id == "wrap") {
            (*text_).set_word_wrap(!(*text_).word_wrap());
            refresh();
        } else if (id == "status") {
            show_status_ = !show_status_;
            (*status_).set_visible(show_status_);
            invalidate(gf::Dirty::layout);
            refresh();
        } else if (id == "font")
            static_cast<void>(font_.handle.show());
        else if (id == "help")
            message(
                "SwiftEdit help",
                "Use File > Open to edit a plain-text document. Ctrl+S saves. A star beside "
                "the filename means unsaved changes. File > New Window (Ctrl/Cmd+Shift+N) "
                "opens an independent blank document and leaves your current work open.\n\n"
                "Find/Replace keeps punctuation literal. "
                "Right-click a query character or press Ctrl+? to toggle a one-character "
                "wildcard, displayed as a dot. Match case "
                "off folds English A-Z only. Search reveals source text when a CSV table or "
                "Markdown preview is visible. Search wraps once. Replace All is one undo "
                "action.\n\nThis build preserves UTF-8 and BOM-marked UTF-16, including "
                "existing line endings. Malformed or unsupported encodings are refused. Files "
                "changed externally require a Save Over or New Copy choice, followed by "
                "review of the destination before saving.\n\nWrap and font affect display only. Editable text is limited to 1 "
                "MiB of UTF-8 and 4096 UTF-8 bytes per logical line. Settings are "
                "session-only. Save resets ordinary Undo. Document > Restore As Opened "
                "recovers the original session text. Document also offers explicit newline "
                "conversion and date/time insertion (F5). The separate command-session "
                "executable supports bounded large-file pages and CSV calculations.\n\n"
                "In rendered Markdown, arrows and Page Up/Down scroll; Home/End go to the "
                "beginning/end. Links remain inactive. In CSV table view, Page Up/Down move "
                "by a viewport; Home/End go to the first/last cell in the row. Ctrl/Cmd+Home/End "
                "go to the first/last cell in the table. Hold Shift to extend a cell rectangle.");
        else if (id == "about")
            message(
                "About SwiftEdit",
                "SwiftEdit " SWIFTEDIT_VERSION "\nA plain-text editor with Markdown and CSV views.\n\nDevelopment build.");
        refresh();
    } catch (const std::exception &e) {
        error(e.what());
    }
}
void Editor::shortcut(gf::Window &w, std::uint32_t key, gf::Modifier mods, const std::string &id) {
    accelerators_.push_back(
        w.register_accelerator(*this, {key, mods}, AcceleratorListener{observe(), id}));
}
void Editor::ready(gf::Window &w, gf::ApplicationWindowHandle handle,
                   const std::filesystem::path &initial) {
    handle_ = handle;
    native_title_.clear();
    refresh();
    using K = gf::PhysicalKey;
    using M = gf::Modifier;
    struct Shortcut {
        std::uint32_t key{};
        const char *command{};
    };
    const Shortcut shortcuts[] = {
        {K::n, "new"}, {K::o, "open"}, {K::s, "save"}, {K::f, "find"}, {K::h, "replace"}};
    for (const Shortcut &binding : shortcuts) {
        shortcut(w, binding.key, M::control, binding.command);
        shortcut(w, binding.key, M::meta, binding.command);
    }
    shortcut(w, K::s, M::control | M::shift, "save-as");
    shortcut(w, K::s, M::meta | M::shift, "save-as");
    shortcut(w, K::n, M::control | M::shift, "new-window");
    shortcut(w, K::n, M::meta | M::shift, "new-window");
#ifdef __APPLE__
    shortcut(w, K::w, M::meta, "exit");
#endif
    shortcut(w, K::f3, M::none, "find-next");
    shortcut(w, K::f1, M::none, "help");
    const Shortcut editing_shortcuts[] = {{K::c, "copy"}, {K::x, "cut"},  {K::v, "paste"},
                                          {K::z, "undo"}, {K::y, "redo"}, {K::a, "select-all"}};
    // Focused text controls handle their own editing first. A grid declines
    // these keys so the window can invoke the same commands as its menus.
    for (const Shortcut &binding : editing_shortcuts) {
        shortcut(w, binding.key, M::control, binding.command);
        shortcut(w, binding.key, M::meta, binding.command);
    }
    shortcut(w, K::z, M::control | M::shift, "redo");
    shortcut(w, K::z, M::meta | M::shift, "redo");
    shortcut(w, K::f5, M::none, "date-time");
    if (!initial.empty()) {
        try {
            open_file(initial);
        } catch (const std::exception &e) {
            error(e.what());
        }
    }
    focus_text();
}
void Editor::closing(gf::HostCloseRequest &request) {
    if (close_authorized_)
        return;
    if (picker_active_ || pending_save_ || conflict_review_) {
        request.cancel = true;
        return;
    }
    if (!document_.dirty((*text_).text()))
        return;
    request.cancel = true;
    try {
        after_unsaved(Continuation::close_window);
    } catch (const std::exception &e) {
        error(e.what());
    }
}
void Editor::build_find() {
    find_.root = gf::make_control<DialogLayout>(gf::StableId("notepad.find"));
    place_find_label("find.label", "Find what:", {16, 18, 105, 28});
    place_find_label("replace.label", "Replace with:", {16, 60, 105, 28});
    query_ = gf::make_control<QueryField>(gf::StableId("find.query"));
    (*find_.root).place(query_, {125, 16, 360, 30});
    replacement_ = gf::make_control<gf::TextBox>(gf::StableId("find.replacement"));
    (*replacement_).set_accessible_name("Replace with");
    (*replacement_).set_maximum_length(4096);
    (*find_.root).place(replacement_, {125, 58, 360, 30});
    match_case_ = gf::make_control<gf::CheckBox>(gf::StableId("find.case"), "Match case");
    (*match_case_).set_checked(true);
    (*find_.root).place(match_case_, {16, 102, 200, 28});
    find_next_button_ =
        place_find_button("find.next", "Find Next", {16, 144, 108, 32}, ButtonAction::find_next);
    place_find_button("find.replace", "Replace", {136, 144, 100, 32}, ButtonAction::replace_one);
    place_find_button("find.all", "Replace All", {248, 144, 108, 32}, ButtonAction::replace_all);
    find_close_ =
        place_find_button("find.close", "Close", {368, 144, 116, 32}, ButtonAction::close_find);
    find_status_ = gf::make_control<gf::Label>(
        gf::StableId("find.status"),
        "Right-click a character or press Ctrl+? to toggle one-character wildcard. Search wraps.");
    (*find_status_).set_use_mnemonic(false);
    (*find_.root).place(find_status_, {16, 190, 470, 50});
}
void Editor::show_find() {
    if (!(*text_).selection().empty() && (*text_).selection().length() < 4096) {
        const std::string selected = (*text_).selected_text();
        if (selected.find_first_of("\r\n") == std::string::npos)
            (*query_).set_text(selected);
    }
    const gf::HostServiceStatus shown = find_.handle.show();
    if (!shown.accepted())
        throw std::runtime_error("Cannot show the Find window (host error " +
                                 std::to_string(static_cast<unsigned>(shown.error)) + ").");
    if (find_.window)
        (*find_.window).request_focus(query_);
}
Editor::FindWork::FindWork(std::string_view source, swiftedit::SearchPattern pattern,
                           std::size_t start, bool sensitive)
    : scan(std::make_unique<swiftedit::PatternScan>(source, std::move(pattern), start, sensitive)),
      match_case(sensitive) {}
Editor::FindWork::FindWork(std::string_view source, swiftedit::SearchPattern pattern,
                           std::string inserted, bool sensitive)
    : replace(std::make_unique<swiftedit::ReplacementScan>(
          source, std::move(pattern), inserted, sensitive, gf::TextBox::maximum_multiline_bytes)),
      replacement(std::move(inserted)), match_case(sensitive) {}
void Editor::cancel_search() {
    search_frame_.disconnect();
    search_.reset();
}
void Editor::on_frame(gf::FrameTime) {
    if (!search_)
        return;
    try {
        advance_search();
    } catch (const std::exception &failure) {
        cancel_search();
        (*find_status_).set_text(failure.what());
    }
}
void Editor::find_next() {
    cancel_search();
    if ((*query_).text().empty()) {
        (*find_status_).set_text("Enter the text to find; flag unknown characters as wildcards.");
        return;
    }
    const swiftedit::SearchPattern pattern = (*query_).pattern();
    std::unique_ptr<FindWork> work = std::make_unique<FindWork>(
        (*text_).text(), pattern, (*text_).selection().end().value(), (*match_case_).checked());
    (*work).source_revision = counted_text_.revision();
    (*work).query_revision = (*query_).revision();
    (*work).selection = (*text_).selection();
    search_ = std::move(work);
    advance_search();
}
void Editor::advance_search() {
    if (!search_)
        return;
    FindWork &work = *search_;
    if (work.source_revision != counted_text_.revision() ||
        work.query_revision != (*query_).revision() || work.selection != (*text_).selection() ||
        work.match_case != (*match_case_).checked() ||
        (work.replace && work.replacement != (*replacement_).text())) {
        cancel_search();
        (*find_status_)
            .set_text("Search cancelled because the document, selection or query changed.");
        return;
    }
    if (work.replace) {
        advance_replacement();
        return;
    }
    swiftedit::SearchProgress progress = (*work.scan).step(4096);
    if (!progress.match && progress.complete && !work.wrapped) {
        (*work.scan).restart();
        work.wrapped = true;
        progress = (*work.scan).step(4096);
    }
    if (progress.match) {
        const swiftedit::SourceRange found = *progress.match;
        const bool wrapped = work.wrapped;
        cancel_search();
        (*text_).select(gf::Utf8Offset(found.offset), gf::Utf8Offset(found.offset + found.length));
        (*find_status_).set_text(wrapped ? "Found (wrapped to beginning)." : "Found.");
        refresh();
    } else if (progress.complete) {
        cancel_search();
        (*find_status_).set_text("Text not found.");
    } else if (window()) {
        (*find_status_).set_text("Searching... Close Find or change the query to cancel.");
        const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::milliseconds(1);
        search_frame_ = (*window()).schedule_paint(shared_from_this(), deadline);
    } else {
        cancel_search();
        (*find_status_).set_text("Search requires an attached window.");
    }
}
void Editor::replace_one() {
    if ((*query_).text().empty()) {
        (*find_status_).set_text("Enter the text to replace.");
        return;
    }
    const std::string selected = (*text_).selected_text();
    const gf::TextStore source(selected);
    const swiftedit::SearchPattern pattern = (*query_).pattern();
    const swiftedit::SearchProgress progress =
        swiftedit::search_slice(source, pattern, 0, 1, (*match_case_).checked());
    const std::optional<swiftedit::SourceRange> match = progress.match;
    if (match && (*match).offset == 0 && selected.size() == (*match).length)
        (*text_).replace_selection((*replacement_).text());
    find_next();
}
void Editor::replace_every() {
    cancel_search();
    if ((*query_).text().empty()) {
        (*find_status_).set_text("Enter the literal text to replace.");
        return;
    }
    const swiftedit::SearchPattern pattern = (*query_).pattern();
    std::unique_ptr<FindWork> work = std::make_unique<FindWork>(
        (*text_).text(), pattern, std::string((*replacement_).text()), (*match_case_).checked());
    (*work).source_revision = counted_text_.revision();
    (*work).query_revision = (*query_).revision();
    (*work).selection = (*text_).selection();
    search_ = std::move(work);
    advance_search();
}
void Editor::advance_replacement() {
    const bool complete = (*(*search_).replace).step(4096);
    if (!complete) {
        if (window()) {
            (*find_status_)
                .set_text("Preparing replacements... Close Find to cancel. Document unchanged.");
            const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::milliseconds(1);
            search_frame_ = (*window()).schedule_paint(shared_from_this(), deadline);
        } else {
            cancel_search();
            (*find_status_).set_text("Replacement requires an attached window.");
        }
        return;
    }
    const swiftedit::PatternReplacement result = (*(*search_).replace).take_result();
    const gf::TextSelection original_selection = (*search_).selection;
    cancel_search();
    if (gf::TextBox::validate_multiline_text(result.text) !=
        gf::TextBox::MultilineValidation::valid) {
        (*find_status_)
            .set_text("Replacement exceeds editor document or line limits. No changes made.");
        return;
    }
    if (result.count) {
        (*text_).select_all();
        const bool replaced = (*text_).replace_selection(result.text);
        if (!replaced) {
            (*text_).select(original_selection.anchor, original_selection.caret);
            (*find_status_).set_text("Replacement was not applied.");
            return;
        }
    }
    (*find_status_).set_text(std::to_string(result.count) + " replacement(s).");
    refresh();
}
void Editor::build_font() {
    font_.root = gf::make_control<DialogLayout>(gf::StableId("notepad.font"));
    std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(
        gf::StableId("font.label"), "Font role and size (display only)");
    (*font_.root).place(label, {16, 16, 380, 28});
    font_role_ = gf::make_control<gf::ComboBox>(gf::StableId("font.role"));
    (*font_role_).set_items({"Monospace", "Content"});
    (*font_role_).set_selected_index(0);
    (*font_role_).set_accessible_name("Font role");
    (*font_.root).place(font_role_, {16, 58, 220, 30});
    font_size_ = gf::make_control<gf::ComboBox>(gf::StableId("font.size"));
    (*font_size_).set_items({"10", "12", "14", "16", "18", "20", "24", "28", "32"});
    (*font_size_).set_selected_index(2);
    (*font_size_).set_accessible_name("Font size");
    (*font_.root).place(font_size_, {252, 58, 130, 30});
    font_bold_ = gf::make_control<gf::CheckBox>(gf::StableId("font.bold"), "Bold");
    (*font_.root).place(font_bold_, {16, 106, 160, 28});
    font_italic_ = gf::make_control<gf::CheckBox>(gf::StableId("font.italic"), "Italic");
    (*font_.root).place(font_italic_, {200, 106, 160, 28});
    font_apply_ = gf::make_control<gf::Button>(gf::StableId("font.apply"), "Apply");
    (*font_.root).place(font_apply_, {170, 156, 100, 32});
    font_close_ = gf::make_control<gf::Button>(gf::StableId("font.close"), "Close");
    (*font_.root).place(font_close_, {282, 156, 100, 32});
    subscriptions_.push_back(
        (*font_apply_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::apply_font}));
    subscriptions_.push_back(
        (*font_close_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::close_font}));
}
void Editor::close_dialog(Dialog &dialog) {
    static_cast<void>(dialog.handle.hide());
    focus_text();
}
void Editor::build_save_choices() {
    save_choices_.root = gf::make_control<DialogLayout>(gf::StableId("swiftedit.save-choices"));
    const std::shared_ptr<gf::Label> explanation = gf::make_control<gf::Label>(
        gf::StableId("save-choices.explanation"),
        "This document has mixed line endings. Save them exactly as they are, convert all line "
        "endings to the document's current default, or cancel the save.");
    (*explanation).set_text_wrapping(gf::TextWrapping::word);
    (*save_choices_.root).place(explanation, {16, 16, 568, 94});
    save_keep_ = gf::make_control<gf::Button>(gf::StableId("save-choices.keep"), "Save As-Is");
    save_normalize_ = gf::make_control<gf::Button>(gf::StableId("save-choices.normalize"),
                                                   "Convert to Document Default");
    save_cancel_ = gf::make_control<gf::Button>(gf::StableId("save-choices.cancel"), "Cancel");
    (*save_choices_.root).place(save_keep_, {16, 124, 140, 34});
    (*save_choices_.root).place(save_normalize_, {168, 124, 264, 34});
    (*save_choices_.root).place(save_cancel_, {444, 124, 140, 34});
    subscriptions_.push_back(
        (*save_keep_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::save_as_is}));
    subscriptions_.push_back(
        (*save_normalize_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::save_normalized}));
    subscriptions_.push_back(
        (*save_cancel_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::cancel_save}));
}
bool Editor::request_save_to(const std::filesystem::path &path, const FileSnapshot &expected,
                             Continuation next) {
    // Check known conflicts before asking about line endings. The writer still
    // verifies this exact snapshot again immediately before publication.
    const FileSnapshot current = read_file(path);
    if (current != expected) {
        begin_conflict(path, next);
        return false;
    }
    if (newline_name((*text_).text()) != "Mixed (preserved)") {
        const bool saved = save_to(path, expected);
        if (saved && next != Continuation::none)
            continue_operation(next);
        return saved;
    }
    PendingSave pending{path, expected, counted_text_.revision(), next};
    pending_save_ = std::move(pending);
    cancel_search();
    set_enabled(false);
    static_cast<void>(find_.handle.hide());
    static_cast<void>(font_.handle.hide());
    static_cast<void>(characters_.handle.hide());
    static_cast<void>(controls_.handle.hide());
    static_cast<void>(save_choices_.handle.show());
    return false;
}
void Editor::finish_save_choice(bool normalize, bool cancel) {
    if (!pending_save_)
        return;
    PendingSave pending = std::move(*pending_save_);
    pending_save_.reset();
    static_cast<void>(save_choices_.handle.hide());
    set_enabled(true);
    focus_text();
    if (cancel)
        return;
    bool published = false;
    try {
        if (pending.revision != counted_text_.revision())
            throw std::runtime_error("The document changed while save choices were open. Save "
                                     "again to review the current text.");
        bool saved = false;
        if (!normalize)
            saved = save_to(pending.path, pending.expected);
        else {
            const std::string converted =
                swiftedit::normalize_newlines((*text_).text(), (*text_).newline_sequence());
            if (gf::TextBox::validate_multiline_text(converted) !=
                gf::TextBox::MultilineValidation::valid)
                throw std::runtime_error(
                    "Converted text exceeds the current document or line limit. No changes made.");
            document_.save(pending.path, converted, pending.expected);
            published = true;
            // Disk publication succeeded; display the same saved content and
            // establish the requested save boundary only after that success.
            (*text_).set_text(converted);
            (*text_).clear_undo_history();
            refresh();
            saved = true;
        }
        if (saved && pending.continuation != Continuation::none)
            continue_operation(pending.continuation);
    } catch (const std::exception &failure) {
        if (published)
            error(std::string("The file was saved, but refreshing its display failed. The saved "
                              "snapshot is retained. ") +
                  failure.what());
        else
            error(failure.what());
    }
}
} // namespace notepad
