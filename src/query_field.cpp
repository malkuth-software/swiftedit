#include "query_field.hpp"
#include "display.hpp"
#include <algorithm>
#include <map>

namespace notepad {
QueryField::QueryField(gf::StableId id) : Control(std::move(id)) {
    // A composite with an edit part keeps focusable false: traversal and
    // request_focus reach the edit part.
    focusable = false;
    accessible_name = "Find what; right-click or Control-question to toggle a wildcard";
}
void QueryField::initialize_control_tree() {
    edit_ = gf::make_control<gf::TextBox>(gf::StableId("query.editing"));
    (*edit_).maximum_length = 4096;
    // Visible, so it can hold focus, but arranged to an empty box: this
    // control paints the query and takes the pointer.
    (*edit_).set_requested_bounds({0, 0, 0, 0});
    add_child(edit_);
    (*edit_).set_edit_filter(gf::Delegate<gf::TextEdit &>::bind<QueryField, &QueryField::filter_edit>(*this));
    editor_text_ = (*edit_).text_changed().subscribe(
        *this, gf::Delegate<const std::string &>::bind<QueryField, &QueryField::editor_text_changed>(*this));
    editor_selection_ = (*edit_).selection_changed().subscribe(
        *this,
        gf::Delegate<const gf::TextSelection &>::bind<QueryField, &QueryField::editor_selection_changed>(*this));
    editor_focus_ = (*edit_).focus_observed().subscribe(
        *this, gf::Delegate<bool>::bind<QueryField, &QueryField::editor_focus_changed>(*this));
}
void QueryField::on_dispose() noexcept {
    editor_text_.disconnect();
    editor_selection_.disconnect();
    editor_focus_.disconnect();
    if (edit_)
        (*edit_).set_edit_filter(gf::Delegate<gf::TextEdit &>{});
    pending_.reset();
    Control::on_dispose();
}
gf::Control::Ptr QueryField::focus_target() const { return edit_; }
QueryField::Snapshot QueryField::snapshot() const {
    Snapshot result{};
    result.text = store_.utf8();
    result.selection = (*edit_).selection();
    for (const swiftedit::SearchSlot &slot : slots_)
        result.flags.push_back(slot.wildcard);
    return result;
}
void QueryField::changed() {
    ++revision_;
    hovered_.reset();
    layout_dirty_ = true;
    invalidate(gf::Dirty::paint);
}
void QueryField::restore(const Snapshot &value) {
    gf::TextStore next(value.text);
    std::vector<swiftedit::SearchSlot> slots{};
    if (!value.text.empty()) {
        const swiftedit::SearchPattern parsed(value.text);
        slots = parsed.slots();
    }
    for (std::size_t index = 0; index < slots.size() && index < value.flags.size(); ++index)
        slots[index].wildcard = value.flags[index];
    restoring_ = true;
    try {
        (*edit_).text = value.text;
    } catch (...) {
        restoring_ = false;
        throw;
    }
    restoring_ = false;
    pending_.reset();
    (*edit_).select(value.selection.anchor, value.selection.caret);
    (*edit_).clear_undo_history();
    store_ = std::move(next);
    slots_ = std::move(slots);
    changed();
}
void QueryField::remember(const Snapshot &before) {
    undo_.push_back(before);
    if (undo_.size() > 64)
        undo_.pop_front();
    redo_.clear();
}
bool QueryField::history(bool redo) {
    std::deque<Snapshot> &source = redo ? redo_ : undo_;
    std::deque<Snapshot> &destination = redo ? undo_ : redo_;
    if (source.empty())
        return false;
    const Snapshot current = snapshot();
    destination.push_back(current);
    try {
        restore(source.back());
    } catch (...) {
        destination.pop_back();
        throw;
    }
    source.pop_back();
    return true;
}
void QueryField::set_text(std::string value) {
    if (value.size() > 4096)
        throw std::runtime_error("Search text exceeds 4096 bytes.");
    Snapshot next{};
    next.text = std::move(value);
    restore(next);
    undo_.clear();
    redo_.clear();
}
std::string_view QueryField::text() const { return store_.utf8(); }
void QueryField::synchronize(const Snapshot &before, std::string_view value, std::size_t start,
                             std::size_t removed, std::size_t inserted) {
    if (value.size() > 4096) {
        restore(before);
        return;
    }
    gf::TextStore next(value);
    std::vector<swiftedit::SearchSlot> slots{};
    if (!value.empty()) {
        const swiftedit::SearchPattern parsed(value);
        slots = parsed.slots();
    }
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].wildcard)
            continue;
        const gf::Utf8Range range = store_.grapheme_range(gf::GraphemeIndex(index));
        const std::size_t begin = range.start.value();
        const std::size_t end = range.end.value();
        std::size_t mapped = begin;
        if (begin >= start + removed)
            mapped = begin - removed + inserted;
        else if (end > start)
            continue;
        if (!next.is_grapheme_boundary(gf::Utf8Offset(mapped)) ||
            !next.is_grapheme_boundary(gf::Utf8Offset(mapped + end - begin)))
            continue;
        const std::size_t target = next.grapheme_index(gf::Utf8Offset(mapped)).value();
        if (target < slots.size() && slots[target].literal == slots_[index].literal)
            slots[target].wildcard = true;
    }
    remember(before);
    store_ = std::move(next);
    slots_ = std::move(slots);
    changed();
}
swiftedit::SearchPattern QueryField::pattern() const {
    swiftedit::SearchPattern result(store_.utf8());
    for (std::size_t index = 0; index < slots_.size(); ++index)
        if (slots_[index].wildcard)
            result.toggle(index);
    return result;
}
void QueryField::select(gf::Utf8Offset anchor, gf::Utf8Offset caret) {
    (*edit_).select(anchor, caret);
    invalidate(gf::Dirty::paint);
}
void QueryField::toggle_slot(std::size_t slot) {
    if (slot >= slots_.size())
        return;
    const Snapshot before = snapshot();
    remember(before);
    slots_[slot].wildcard = !slots_[slot].wildcard;
    ++revision_;
    layout_dirty_ = true;
    invalidate(gf::Dirty::paint);
}
std::size_t QueryField::hit(double x) const {
    const double position = x - 6 + scroll_;
    const std::vector<double>::const_iterator edge =
        std::upper_bound(edges_.begin(), edges_.end(), position);
    const std::size_t index = static_cast<std::size_t>(edge - edges_.begin());
    const std::size_t slot = index ? index - 1 : 0;
    return std::min(slot, slots_.size());
}
void QueryField::on_paint(gf::Painter &painter, gf::Rect) {
    const gf::Rect bounds = arranged_bounds();
    const gf::BasicControlStyle &style = effective_theme().basic_style();
    const gf::FontSpec font{gf::FontRole::content, 14, 400, false};
    const gf::ResolvedTextLayout font_metrics = painter.resolve_text_layout_utf8("Mg", font);
    const double baseline = (bounds.height - font_metrics.ascent - font_metrics.descent) * 0.5 +
                            font_metrics.ascent;
    if (layout_dirty_) {
        std::vector<double> next{0};
        std::vector<std::string> labels{};
        next.reserve(slots_.size() + 1);
        labels.reserve(slots_.size());
        // Metrics belong to this painter/font and this rebuild only. Repeated
        // bullets or literals share measurement without retaining stale metrics.
        std::map<std::string, double> widths{};
        for (const swiftedit::SearchSlot &slot : slots_) {
            std::string label = "\xe2\x80\xa2";
            if (!slot.wildcard) {
                const swiftedit::DisplayPage display(slot.literal);
                label = display.text();
            }
            const std::map<std::string, double>::const_iterator found = widths.find(label);
            double width = 0;
            if (found != widths.end())
                width = (*found).second;
            else {
                const gf::Size size = painter.measure_text_utf8(label, font);
                width = std::max(12.0, size.width + 4);
                widths.emplace(label, width);
            }
            next.push_back(next.back() + width);
            labels.push_back(std::move(label));
        }
        edges_ = std::move(next);
        labels_ = std::move(labels);
        layout_dirty_ = false;
    }
    const gf::TextSelection selection = (*edit_).selection();
    const std::size_t caret = store_.grapheme_index(selection.caret).value();
    const double width = std::max(1.0, bounds.width - 12);
    if (edges_[caret] < scroll_)
        scroll_ = edges_[caret];
    if (edges_[caret] > scroll_ + width)
        scroll_ = edges_[caret] - width;
    scroll_ = std::min(scroll_, std::max(0.0, edges_.back() - width));
    painter.fill_rect({0, 0, bounds.width, bounds.height}, style.paper);
    painter.stroke_rect({0, 0, bounds.width, bounds.height}, style.border, 1);
    painter.save();
    painter.clip_rect({2, 2, std::max(0.0, bounds.width - 4), std::max(0.0, bounds.height - 4)});
    for (std::size_t index = hit(0); index < slots_.size(); ++index) {
        const double x = 6 + edges_[index] - scroll_;
        if (x > bounds.width)
            break;
        const gf::Utf8Range range = store_.grapheme_range(gf::GraphemeIndex(index));
        const gf::Rect box{x, 3, edges_[index + 1] - edges_[index], bounds.height - 6};
        if (range.start.value() >= selection.start().value() &&
            range.end.value() <= selection.end().value())
            painter.fill_rect(box, style.face_light);
        if (hovered_ == index)
            painter.stroke_rect(box, style.border, 1);
        painter.draw_text_utf8({x + 2, baseline}, labels_[index], font,
                               slots_[index].wildcard ? style.link : style.text);
    }
    if (focused_) {
        const double x = 6 + edges_[caret] - scroll_;
        painter.draw_line({x, 4}, {x, bounds.height - 4}, style.text, 1);
    }
    painter.restore();
}
void QueryField::on_key_preview(gf::KeyEvent &event) {
    if (event.action != gf::KeyAction::down)
        return;
    // USB HID slash/question key, as normalized by the public host contract.
    if (event.physical_key == 0x38 && gf::has_modifier(event.modifiers, gf::Modifier::control) &&
        gf::has_modifier(event.modifiers, gf::Modifier::shift)) {
        const std::size_t slot = store_.grapheme_index((*edit_).selection().caret).value();
        toggle_slot(slot);
        event.handled = true;
        return;
    }
    const bool command = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                         gf::has_modifier(event.modifiers, gf::Modifier::meta);
    if (command && (event.physical_key == gf::PhysicalKey::z || event.physical_key == gf::PhysicalKey::y)) {
        const bool redo = event.physical_key == gf::PhysicalKey::y ||
                          gf::has_modifier(event.modifiers, gf::Modifier::shift);
        static_cast<void>(history(redo));
        event.handled = true;
    }
}
// Called by the edit part before it applies a user edit (typing, deletion,
// cut or paste). The edit is recorded with the state before it; the text
// change that follows moves the flags. A replacement by the same bytes
// changes no text and raises no text_changed, so it is applied here: it
// still turns the flags it covers literal.
void QueryField::filter_edit(gf::TextEdit &edit) {
    PendingEdit next{};
    next.before = snapshot();
    next.start = edit.start.value();
    next.removed = edit.end.value() - edit.start.value();
    next.inserted = edit.replacement.size();
    const std::string_view current(next.before.text);
    const bool same_bytes = next.removed > 0 && next.removed == next.inserted &&
                            current.substr(next.start, next.removed) == edit.replacement;
    if (same_bytes) {
        pending_.reset();
        synchronize(next.before, current, next.start, next.removed, next.inserted);
        return;
    }
    pending_ = std::move(next);
}
void QueryField::editor_text_changed(const std::string &value) {
    if (restoring_)
        return;
    const std::optional<PendingEdit> edit = std::move(pending_);
    pending_.reset();
    (*edit_).clear_undo_history();
    if (edit) {
        const PendingEdit &recorded = *edit;
        const std::string_view before(recorded.before.text);
        const bool sized = recorded.start + recorded.removed <= before.size() &&
                           value.size() + recorded.removed == before.size() + recorded.inserted;
        const bool matches = sized && value.compare(0, recorded.start, before, 0, recorded.start) == 0 &&
                             value.compare(recorded.start + recorded.inserted, std::string::npos, before,
                                           recorded.start + recorded.removed, std::string::npos) == 0;
        if (matches) {
            synchronize(recorded.before, value, recorded.start, recorded.removed, recorded.inserted);
            return;
        }
    }
    // A change the filter did not see (or one it saw differently): the
    // smallest edit between the two texts, starting at a grapheme boundary.
    const Snapshot before = snapshot();
    const std::string_view old_text(before.text);
    if (value == old_text)
        return;
    std::size_t prefix = 0;
    while (prefix < old_text.size() && prefix < value.size() && old_text[prefix] == value[prefix])
        ++prefix;
    while (prefix > 0 && !store_.is_grapheme_boundary(gf::Utf8Offset(prefix)))
        --prefix;
    std::size_t suffix = 0;
    while (suffix < old_text.size() - prefix && suffix < value.size() - prefix &&
           old_text[old_text.size() - 1 - suffix] == value[value.size() - 1 - suffix])
        ++suffix;
    synchronize(before, value, prefix, old_text.size() - prefix - suffix, value.size() - prefix - suffix);
}
void QueryField::editor_selection_changed(const gf::TextSelection &) { invalidate(gf::Dirty::paint); }
void QueryField::editor_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(gf::Dirty::paint);
}
void QueryField::on_mouse_down(gf::MouseEventArgs &arguments) {
    gf::PointerEvent event = gf::pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    gf::Control::on_mouse_down(arguments);
}
void QueryField::on_mouse_up(gf::MouseEventArgs &arguments) {
    gf::PointerEvent event = gf::pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    gf::Control::on_mouse_up(arguments);
}
void QueryField::on_mouse_move(gf::MouseEventArgs &arguments) {
    gf::PointerEvent event = gf::pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    gf::Control::on_mouse_move(arguments);
}
void QueryField::on_mouse_leave() {
    gf::PointerEvent event{};
    event.action = gf::PointerAction::leave;
    event.phase = gf::EventPhase::target;
    handle_pointer(event);
    gf::Control::on_mouse_leave();
}
void QueryField::handle_pointer(gf::PointerEvent &event) {
    if (event.phase != gf::EventPhase::target || layout_dirty_)
        return;
    const double x = event.position.x - absolute_bounds().x;
    const std::size_t slot = hit(x);
    const gf::Utf8Offset at = store_.utf8_offset(gf::GraphemeIndex(slot));
    if (event.action == gf::PointerAction::down) {
        if (window())
            (*window()).request_focus(shared_from_this());
        if (event.button == gf::PointerButton::secondary) {
            select(at, at);
            toggle_slot(slot);
        } else if (event.button == gf::PointerButton::primary) {
            const gf::Utf8Offset anchor = gf::has_modifier(event.modifiers, gf::Modifier::shift)
                                              ? (*edit_).selection().anchor
                                              : at;
            select(anchor, at);
            dragging_ = true;
            set_pointer_capture(true);
        }
        event.handled = true;
    } else if (event.action == gf::PointerAction::up) {
        dragging_ = false;
        set_pointer_capture(false);
    } else if (event.action == gf::PointerAction::move) {
        hovered_ = slot < slots_.size() ? std::optional<std::size_t>(slot) : std::nullopt;
        if (dragging_)
            select((*edit_).selection().anchor, at);
    } else if (event.action == gf::PointerAction::leave)
        hovered_.reset();
    invalidate(gf::Dirty::paint);
}
} // namespace notepad
