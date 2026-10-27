#include "character_picker.hpp"
#include <algorithm>
#include <stdexcept>

namespace notepad {
CharacterPicker::CharacterPicker(gf::StableId id, bool controls)
    : Control(std::move(id)), controls_(controls) {}
void CharacterPicker::initialize_control_tree() {
    heading_ = gf::make_control<gf::Label>(
        gf::StableId("characters.heading"),
        controls_ ? "Control characters — select one to see its purpose. Preview labels are inert."
                  : "Unicode characters — browse pages or enter a code point such as U+1F600.");
    (*heading_).set_text_wrapping(gf::TextWrapping::word);
    codepoint_ = gf::make_control<gf::TextBox>(gf::StableId("characters.codepoint"), "U+0020");
    (*codepoint_).set_maximum_length(8);
    (*codepoint_).set_accessible_name("Unicode code point in hexadecimal");
    list_ = gf::make_control<gf::ListBox>(gf::StableId("characters.list"));
    (*list_).set_accessible_name(controls_ ? "Control characters" : "Unicode character page");
    glyph_ = gf::make_control<gf::Label>(gf::StableId("characters.glyph"));
    (*glyph_).set_font({gf::FontRole::content, 24, 400, false});
    (*glyph_).set_use_mnemonic(false);
    detail_ = gf::make_control<gf::Label>(gf::StableId("characters.detail"));
    (*detail_).set_text_wrapping(gf::TextWrapping::word);
    (*detail_).set_use_mnemonic(false);
    previous_ = gf::make_control<gf::Button>(gf::StableId("characters.previous"), "Previous page");
    next_ = gf::make_control<gf::Button>(gf::StableId("characters.next"), "Next page");
    go_ = gf::make_control<gf::Button>(gf::StableId("characters.go"), "Go");
    insert_ = gf::make_control<gf::Button>(gf::StableId("characters.insert"), "Insert");
    copy_ = gf::make_control<gf::Button>(gf::StableId("characters.copy"), "Copy");
    close_ = gf::make_control<gf::Button>(gf::StableId("characters.close"), "Close");
    add_child(heading_);
    add_child(codepoint_);
    add_child(list_);
    add_child(glyph_);
    add_child(detail_);
    add_child(previous_);
    add_child(next_);
    add_child(go_);
    add_child(insert_);
    add_child(copy_);
    add_child(close_);
    const std::shared_ptr<CharacterPicker> self =
        std::static_pointer_cast<CharacterPicker>(shared_from_this());
    subscriptions_.push_back(
        (*previous_).clicked().subscribe(*this, Click{self, Action::previous}));
    subscriptions_.push_back((*next_).clicked().subscribe(*this, Click{self, Action::next}));
    subscriptions_.push_back((*go_).clicked().subscribe(*this, Click{self, Action::go}));
    subscriptions_.push_back((*insert_).clicked().subscribe(*this, Click{self, Action::insert}));
    subscriptions_.push_back((*copy_).clicked().subscribe(*this, Click{self, Action::copy}));
    subscriptions_.push_back((*close_).clicked().subscribe(*this, Click{self, Action::close}));
    subscriptions_.push_back((*list_).selection_changed().subscribe(*this, Selection{self}));
    load_page(0);
}
void CharacterPicker::on_dispose() noexcept {
    subscriptions_.clear();
    Control::on_dispose();
}
void CharacterPicker::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double width = std::max(0.0, bounds.width - 32);
    set_child_layout(heading_, {16, 12, width, 40});
    set_child_layout(codepoint_, {16, 58, 150, 30});
    set_child_layout(go_, {174, 58, 60, 30});
    set_child_layout(previous_, {250, 58, 130, 30});
    set_child_layout(next_, {388, 58, 130, 30});
    set_child_layout(list_, {16, 100, width, std::max(80.0, bounds.height - 290)});
    set_child_layout(glyph_, {16, bounds.height - 178, 130, 48});
    set_child_layout(detail_, {155, bounds.height - 180, std::max(0.0, width - 139), 120});
    set_child_layout(insert_, {bounds.width - 322, bounds.height - 46, 94, 30});
    set_child_layout(copy_, {bounds.width - 220, bounds.height - 46, 94, 30});
    set_child_layout(close_, {bounds.width - 118, bounds.height - 46, 94, 30});
}
void CharacterPicker::Click::operator()(gf::ButtonBase &) const {
    const std::shared_ptr<CharacterPicker> self = owner.lock();
    if (!self || !(*self).is_alive())
        return;
    try {
        (*self).action(action);
    } catch (const std::exception &failure) {
        (*(*self).detail_).set_text(failure.what());
    }
}
void CharacterPicker::Selection::operator()(const gf::ListSelectionChange &) const {
    const std::shared_ptr<CharacterPicker> self = owner.lock();
    if (self && (*self).is_alive())
        (*self).update_selection();
}
void CharacterPicker::load_page(char32_t first) {
    std::vector<swiftedit::CharacterInfo> rows =
        controls_ ? swiftedit::control_characters() : swiftedit::character_page(first, false);
    std::vector<std::string> labels{};
    labels.reserve(rows.size());
    for (const swiftedit::CharacterInfo &info : rows)
        labels.push_back(swiftedit::codepoint_label(info.scalar) + "  " + info.name);
    rows_ = std::move(rows);
    page_ = first;
    (*list_).set_items(std::move(labels));
    (*previous_).set_enabled(!controls_ && page_ > 0);
    (*next_).set_enabled(!controls_ && page_ < 0x10ff00);
    (*insert_).set_enabled(!rows_.empty());
    (*copy_).set_enabled(!rows_.empty());
    if (!rows_.empty())
        (*list_).select_index(0);
    else {
        (*glyph_).set_text("");
        (*detail_).set_text("No assigned ordinary characters on this page. Enter another code "
                            "point or browse the next page.");
    }
}
swiftedit::CharacterInfo CharacterPicker::selected_info() const {
    const std::optional<std::size_t> selected = (*list_).selected_index();
    if (!selected || *selected >= rows_.size())
        throw std::runtime_error("Select a character first.");
    return rows_[*selected];
}
void CharacterPicker::update_selection() {
    if (!(*list_).selected_index())
        return;
    const swiftedit::CharacterInfo info = selected_info();
    const std::string label = swiftedit::codepoint_label(info.scalar);
    (*codepoint_).set_text(label);
    (*glyph_).set_text(info.control ? "[" + label + "]" : swiftedit::character_utf8(info.scalar));
    (*detail_).set_text(label + " " + info.name + "\n" + info.explanation);
}
void CharacterPicker::select_codepoint(char32_t scalar) {
    const swiftedit::CharacterInfo info = swiftedit::character_info(scalar);
    if (!info.assigned || info.control != controls_)
        throw std::runtime_error(
            controls_ ? "Choose an assigned control character; ordinary text has a separate dialog."
                      : "Choose an assigned text character; controls have a separate dialog.");
    const char32_t first = scalar & ~char32_t(255);
    if (!controls_ && page_ != first)
        load_page(first);
    for (std::size_t index = 0; index < rows_.size(); ++index)
        if (rows_[index].scalar == scalar) {
            (*list_).select_index(index);
            update_selection();
            return;
        }
    throw std::runtime_error("Character is not available in this catalog page.");
}
void CharacterPicker::insert_selected() {
    const swiftedit::CharacterInfo info = selected_info();
    const std::string value = swiftedit::character_utf8(info.scalar);
    inserted_.emit(value);
}
void CharacterPicker::copy_selected() {
    if (!window() || !(*window()).host_services())
        throw std::runtime_error("Clipboard service is unavailable.");
    const swiftedit::CharacterInfo info = selected_info();
    const std::string value = swiftedit::character_utf8(info.scalar);
    if (value.find('\0') != value.npos)
        throw std::runtime_error(
            "The text clipboard cannot carry a NUL character. Use Insert to preserve it.");
    const gf::HostServiceStatus result = (*(*window()).host_services()).write_clipboard_text(value);
    if (!result.accepted())
        throw std::runtime_error("The character could not be copied.");
}
void CharacterPicker::action(Action requested) {
    switch (requested) {
    case Action::previous:
        if (!controls_ && page_ >= 256)
            load_page(page_ - 256);
        break;
    case Action::next:
        if (!controls_ && page_ < 0x10ff00)
            load_page(page_ + 256);
        break;
    case Action::go: {
        const char32_t scalar = swiftedit::parse_codepoint((*codepoint_).text());
        select_codepoint(scalar);
        break;
    }
    case Action::insert:
        insert_selected();
        break;
    case Action::copy:
        copy_selected();
        break;
    case Action::close:
        closed_.emit();
        break;
    }
}
} // namespace notepad
