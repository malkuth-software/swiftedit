#include "code_editor.hpp"
#include "highlight/language.hpp"
#include <algorithm>
#include <array>

namespace swiftedit {
namespace gf = gui_forms;
namespace hl = swiftedit::highlight;

namespace {

bool command_modifier(gf::Modifier modifiers) noexcept {
    const bool control = modifiers == gf::Modifier::control;
    const bool meta = modifiers == gf::Modifier::meta;
    const bool result = control || meta;
    return result;
}

std::string line_column_text(hl::SourcePosition position) {
    const std::string result =
        "Line " + std::to_string(position.line) + ", Column " + std::to_string(position.column);
    return result;
}

} // namespace

CodeEditor::CodeEditor(gf::StableId id) : Control(std::move(id)), document_(hl::language_named("plain")) {}

CodeEditor::~CodeEditor() { subscriptions_.clear(); }

std::weak_ptr<CodeEditor> CodeEditor::observe() {
    const std::shared_ptr<CodeEditor> self = std::static_pointer_cast<CodeEditor>(shared_from_this());
    const std::weak_ptr<CodeEditor> result = self;
    return result;
}

std::shared_ptr<gf::Button> CodeEditor::make_bar_button(const char *id, const char *label, BarAction action) {
    std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(gf::StableId(std::string_view(id)));
    (*button).text = label;
    add_child(button);
    subscriptions_.push_back((*button).clicked().subscribe(*this, BarListener{observe(), action}));
    return button;
}

void CodeEditor::initialize_control_tree() {
    save_ = make_bar_button("swiftedit.code.save", "&Save", BarAction::save);
    undo_ = make_bar_button("swiftedit.code.undo", "&Undo", BarAction::undo);
    redo_ = make_bar_button("swiftedit.code.redo", "&Redo", BarAction::redo);
    find_ = gf::make_control<gf::TextBox>(gf::StableId("swiftedit.code.find"));
    (*find_).placeholder_text = "Find";
    (*find_).accepts_enter = true;
    (*find_).accepts_escape = true;
    (*find_).accessible_name = "Find in document";
    add_child(find_);
    match_case_ = gf::make_control<gf::CheckBox>(gf::StableId("swiftedit.code.match-case"));
    (*match_case_).text = "Match &case";
    add_child(match_case_);
    previous_ = make_bar_button("swiftedit.code.previous", "&Previous", BarAction::previous);
    next_ = make_bar_button("swiftedit.code.next", "&Next", BarAction::next);

    text_ = gf::make_control<gf::TextBox>(gf::StableId("swiftedit.code.text"));
    (*text_).multiline = true;
    (*text_).auto_scroll = true;
    (*text_).word_wrap = false;
    (*text_).accepts_tab = true;
    (*text_).maximum_length = gf::TextBox::maximum_multiline_bytes;
    (*text_).font = gf::FontSpec{gf::FontRole::monospace, 14, 400, false};
    (*text_).accessible_name = "Code";
    add_child(text_);
    markers_ = gf::make_control<MarkerStrip>(gf::StableId("swiftedit.code.markers"));
    (*markers_).accessible_name = "Diagnostics and rules overview";
    add_child(markers_);
    status_ = gf::make_control<gf::Label>(gf::StableId("swiftedit.code.status"));
    (*status_).use_mnemonic = false;
    (*status_).classes = std::vector<std::string>{"status-line"};
    add_child(status_);

    subscriptions_.push_back((*text_).text_changed().subscribe(*this, TextListener{observe()}));
    subscriptions_.push_back((*text_).selection_changed().subscribe(*this, SelectionListener{observe()}));
    subscriptions_.push_back((*find_).text_changed().subscribe(*this, FindListener{observe()}));
    subscriptions_.push_back((*find_).committed().subscribe(*this, FindCommitted{observe()}));
    subscriptions_.push_back((*match_case_).clicked().subscribe(*this, BarListener{observe(), BarAction::match_case}));
    subscriptions_.push_back((*markers_).activated().subscribe(*this, MarkerListener{observe()}));
    refresh_status();
}

void CodeEditor::TextListener::operator()(const std::string &text) const {
    const std::shared_ptr<CodeEditor> target = owner.lock();
    if (target)
        (*target).text_changed(text);
}

void CodeEditor::SelectionListener::operator()(const gf::TextSelection &) const {
    const std::shared_ptr<CodeEditor> target = owner.lock();
    if (target)
        (*target).selection_changed();
}

void CodeEditor::FindListener::operator()(const std::string &needle) const {
    const std::shared_ptr<CodeEditor> target = owner.lock();
    if (!target)
        return;
    static_cast<void>((*target).find(needle, (*(*target).match_case_).checked.read()));
}

void CodeEditor::FindCommitted::operator()(const std::string &) const {
    const std::shared_ptr<CodeEditor> target = owner.lock();
    if (target)
        static_cast<void>((*target).find_next());
}

void CodeEditor::BarListener::operator()(gf::ButtonBase &) const {
    const std::shared_ptr<CodeEditor> target = owner.lock();
    if (!target)
        return;
    CodeEditor &editor = *target;
    switch (action) {
    case BarAction::match_case:
        static_cast<void>(editor.find((*editor.find_).text.read(), (*editor.match_case_).checked.read()));
        break;
    case BarAction::save:
        editor.request_save();
        break;
    case BarAction::undo:
        static_cast<void>(editor.undo());
        break;
    case BarAction::redo:
        static_cast<void>(editor.redo());
        break;
    case BarAction::previous:
        static_cast<void>(editor.find_previous());
        break;
    case BarAction::next:
        static_cast<void>(editor.find_next());
        break;
    }
}

void CodeEditor::MarkerListener::operator()(const hl::LineMarker &marker) const {
    const std::shared_ptr<CodeEditor> target = owner.lock();
    if (!target)
        return;
    CodeEditor &editor = *target;
    const bool rule = marker.kind == hl::DecorationKind::rule_match || marker.kind == hl::DecorationKind::rule_focus;
    const hl::DecorationLayer &layer = rule ? editor.document_.rules() : editor.document_.diagnostics();
    for (const hl::Decoration &decoration : layer.items()) {
        if (decoration.tag != marker.tag)
            continue;
        editor.select(decoration.range);
        break;
    }
    if (editor.window())
        static_cast<void>((*editor.window()).request_focus(editor.text_));
}

void CodeEditor::load(std::string_view text, std::string_view file_name) {
    const std::string copy(text);
    document_.set_language(hl::language_for_file_name(file_name));
    document_.reset(copy);
    diagnostics_.clear();
    loading_ = true;
    (*text_).text = copy;
    loading_ = false;
    (*text_).clear_undo_history();
    (*text_).select(gf::Utf8Offset(0U), gf::Utf8Offset(0U));
    saved_text_ = copy;
    refresh_markers();
    refresh_status();
}

std::string CodeEditor::text() const {
    const std::string result((*text_).text.read());
    return result;
}

void CodeEditor::set_language(const hl::Language &language) {
    document_.set_language(language);
    refresh_styles();
    refresh_status();
}

void CodeEditor::set_command_bar_visible(bool visible) {
    if (command_bar_visible_ == visible)
        return;
    command_bar_visible_ = visible;
    const std::array<gf::Control::Ptr, 7U> bar{save_, undo_, redo_, find_, match_case_, previous_, next_};
    for (const gf::Control::Ptr &control : bar)
        (*control).visible = visible;
    invalidate(gf::Dirty::layout);
}

void CodeEditor::text_changed(const std::string &text) {
    if (loading_)
        return;
    static_cast<void>(document_.update(text));
    refresh_markers();
    refresh_status();
    changed_.emit();
}

void CodeEditor::selection_changed() {
    refresh_status();
    caret_moved_.emit(caret());
}

hl::ByteOffset CodeEditor::caret() const {
    const hl::ByteOffset result{(*text_).selection().end().value()};
    return result;
}

void CodeEditor::select(hl::ByteRange range) {
    (*text_).select(gf::Utf8Offset(range.start.value), gf::Utf8Offset(range.end.value));
}

void CodeEditor::set_rule_offsets(std::span<const hl::ByteOffset> offsets) {
    document_.set_rule_offsets(offsets);
    refresh_markers();
}

void CodeEditor::clear_rules() {
    document_.clear_rules();
    refresh_markers();
}

void CodeEditor::set_diagnostics(std::span<const CodeDiagnostic> diagnostics, hl::ColumnUnit unit) {
    std::vector<hl::SourceDiagnostic> places{};
    places.reserve(diagnostics.size());
    for (const CodeDiagnostic &diagnostic : diagnostics)
        places.push_back(diagnostic.place);
    std::vector<CodeDiagnostic> kept(diagnostics.begin(), diagnostics.end());
    document_.set_diagnostics(places, unit);
    diagnostics_ = std::move(kept);
    refresh_markers();
    refresh_status();
}

void CodeEditor::clear_diagnostics() {
    document_.clear_diagnostics();
    diagnostics_.clear();
    refresh_markers();
    refresh_status();
}

bool CodeEditor::go_to_next_diagnostic() {
    const std::optional<std::size_t> index = document_.next_in(hl::MarkerLayer::diagnostics, caret());
    if (!index)
        return false;
    select(document_.diagnostics().items()[*index].range);
    return true;
}

bool CodeEditor::go_to_previous_diagnostic() {
    const gf::TextSelection selection = (*text_).selection();
    const hl::ByteOffset from{std::min(selection.start().value(), selection.end().value())};
    const std::optional<std::size_t> index = document_.previous_in(hl::MarkerLayer::diagnostics, from);
    if (!index)
        return false;
    select(document_.diagnostics().items()[*index].range);
    return true;
}

bool CodeEditor::go_to_next_rule() {
    const std::optional<std::size_t> index = document_.next_in(hl::MarkerLayer::rules, caret());
    if (!index)
        return false;
    select(document_.rules().items()[*index].range);
    return true;
}

std::size_t CodeEditor::find(std::string_view needle, bool match_case) {
    const std::size_t count = document_.search(needle, match_case);
    refresh_styles();
    refresh_status();
    return count;
}

void CodeEditor::select_search_match(std::size_t index) {
    document_.set_current_match(index);
    refresh_styles();
    select(document_.matches().items()[index].range);
    refresh_status();
}

bool CodeEditor::find_next() {
    const std::optional<std::size_t> index = document_.next_in(hl::MarkerLayer::search, caret());
    if (!index)
        return false;
    select_search_match(*index);
    return true;
}

bool CodeEditor::find_previous() {
    const gf::TextSelection selection = (*text_).selection();
    const hl::ByteOffset from{std::min(selection.start().value(), selection.end().value())};
    const std::optional<std::size_t> index = document_.previous_in(hl::MarkerLayer::search, from);
    if (!index)
        return false;
    select_search_match(*index);
    return true;
}

void CodeEditor::focus_find() {
    if (!command_bar_visible_)
        set_command_bar_visible(true);
    if (window())
        static_cast<void>((*window()).request_focus(find_));
    (*find_).select_all();
}

void CodeEditor::request_save() { save_requested_.emit(); }

bool CodeEditor::undo() {
    const bool undone = (*text_).undo();
    return undone;
}

bool CodeEditor::redo() {
    const bool redone = (*text_).redo();
    return redone;
}

bool CodeEditor::modified() const {
    const bool result = (*text_).text.read() != saved_text_;
    return result;
}

void CodeEditor::mark_saved() {
    saved_text_ = std::string((*text_).text.read());
    refresh_status();
}

void CodeEditor::style_runs(hl::ByteRange window, std::vector<hl::StyleRun> &runs) {
    document_.style_runs(window, runs);
}

void CodeEditor::refresh_markers() {
    document_.line_markers(marker_scratch_);
    (*markers_).set_markers(marker_scratch_, document_.highlighter().line_count());
    refresh_styles();
}

void CodeEditor::refresh_styles() {
    if (text_)
        painter_.paint(document_, *text_);
}

// The status line: the caret's place, then the diagnostic under the caret (or
// the search's state), then the language and whether the text is modified.
void CodeEditor::refresh_status() {
    if (!status_)
        return;
    const hl::ByteOffset at = caret();
    std::string line = line_column_text(document_.highlighter().position_of(at));
    std::optional<std::size_t> under{};
    for (const hl::Decoration &decoration : document_.diagnostics().items()) {
        const bool holds = decoration.range.contains(at) || decoration.range.end == at;
        if (holds && decoration.tag < diagnostics_.size()) {
            under = decoration.tag;
            break;
        }
    }
    if (under) {
        const CodeDiagnostic &diagnostic = diagnostics_[*under];
        const char *severity =
            diagnostic.place.severity == hl::DiagnosticSeverity::error ? "Error: " : "Warning: ";
        line += "  |  ";
        line += severity;
        line += diagnostic.message;
    } else if (!document_.search_needle().empty()) {
        const std::size_t count = document_.matches().items().size();
        line += "  |  ";
        if (document_.current_match()) {
            line += "Match " + std::to_string(*document_.current_match() + 1U) + " of ";
        }
        line += std::to_string(count) + (count == 1U ? " match" : " matches");
        if (document_.search_truncated())
            line += " (stopped at the limit)";
    } else if (!diagnostics_.empty()) {
        line += "  |  " + std::to_string(diagnostics_.size()) +
                (diagnostics_.size() == 1U ? " finding (F8)" : " findings (F8)");
    }
    line += "  |  ";
    line += document_.language().display_name();
    if (modified())
        line += "  |  Modified";
    (*status_).text = line;
}

void CodeEditor::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double width = bounds.width;
    double top = 0.0;
    if (command_bar_visible_) {
        const double button_width = 76.0;
        const double gap = 4.0;
        double x = 4.0;
        const double button_top = 4.0;
        const double button_height = command_bar_height - 8.0;
        const std::array<std::shared_ptr<gf::Button>, 3U> leading{save_, undo_, redo_};
        for (const std::shared_ptr<gf::Button> &button : leading) {
            set_child_layout(button, {x, button_top, button_width, button_height});
            x += button_width + gap;
        }
        x += 8.0;
        const double trailing = 2.0 * (button_width + gap) + 110.0;
        const double find_width = std::max(80.0, width - x - trailing - 4.0);
        set_child_layout(find_, {x, button_top, find_width, button_height});
        x += find_width + gap;
        set_child_layout(match_case_, {x, button_top, 106.0, button_height});
        x += 110.0;
        set_child_layout(previous_, {x, button_top, button_width, button_height});
        x += button_width + gap;
        set_child_layout(next_, {x, button_top, button_width, button_height});
        top = command_bar_height;
    }
    const double text_height = std::max(0.0, bounds.height - top - status_height);
    const double text_width = std::max(0.0, width - marker_strip_width);
    set_child_layout(text_, {0.0, top, text_width, text_height});
    set_child_layout(markers_, {text_width, top, std::min(width, marker_strip_width), text_height});
    set_child_layout(status_, {6.0, top + text_height + 3.0, std::max(0.0, width - 12.0), status_height - 6.0});
}

void CodeEditor::on_paint(gf::Painter &painter, gf::Rect) {
    const gf::Rect bounds = committed_arranged_bounds();
    const gf::BasicControlStyle &style = effective_theme().basic_style();
    if (command_bar_visible_) {
        painter.fill_rect({0, 0, bounds.width, command_bar_height}, style.face);
        painter.draw_line({0, command_bar_height - 0.5}, {bounds.width, command_bar_height - 0.5}, style.border, 1);
    }
    const double status_top = std::max(0.0, bounds.height - status_height);
    painter.fill_rect({0, status_top, bounds.width, status_height}, style.face);
    painter.draw_line({0, status_top + 0.5}, {bounds.width, status_top + 0.5}, style.border, 1);
}

void CodeEditor::on_key_preview(gf::KeyEvent &event) {
    if (event.action != gf::KeyAction::down || event.handled)
        return;
    const std::uint32_t key = event.physical_key;
    const bool shift = event.modifiers == gf::Modifier::shift;
    const bool plain = event.modifiers == gf::Modifier::none;
    bool handled = true;
    if (command_modifier(event.modifiers) && key == gf::PhysicalKey::s)
        request_save();
    else if (command_modifier(event.modifiers) && key == gf::PhysicalKey::f)
        focus_find();
    else if (key == gf::PhysicalKey::f3 && plain)
        static_cast<void>(find_next());
    else if (key == gf::PhysicalKey::f3 && shift)
        static_cast<void>(find_previous());
    else if (key == gf::PhysicalKey::f8 && plain)
        static_cast<void>(go_to_next_diagnostic());
    else if (key == gf::PhysicalKey::f8 && shift)
        static_cast<void>(go_to_previous_diagnostic());
    else if (key == gf::PhysicalKey::escape && plain && window() &&
             (*window()).focused_control() == std::static_pointer_cast<gf::Control>(find_))
        static_cast<void>((*window()).request_focus(text_));
    else
        handled = false;
    event.handled = handled;
}

} // namespace swiftedit
