#include "csv_view.hpp"
#include "display.hpp"
#include <algorithm>

namespace notepad {
namespace {
constexpr double row_height = 28, column_width = 144, header_width = 56, grid_top = 68;
std::string cell_label(std::string_view value) {
    std::size_t end = 0;
    while (end < value.size() && end < 240) {
        const std::size_t length = swiftedit::utf8_sequence_length(value, end);
        end += length ? length : 1;
    }
    const swiftedit::DisplayPage display(value.substr(0, end));
    std::string result = display.text();
    for (char &character : result)
        if (character == '\r' || character == '\n' || character == '\t')
            character = ' ';
    if (end < value.size())
        result += "...";
    return result;
}
} // namespace
CsvView::CsvView(gf::StableId id) : Control(std::move(id)) { set_focusable(true); }
CsvView::~CsvView() = default;
std::weak_ptr<CsvView> CsvView::observe() {
    const std::shared_ptr<CsvView> self = std::static_pointer_cast<CsvView>(shared_from_this());
    const std::weak_ptr<CsvView> observer = self;
    return observer;
}
void CsvView::CommitListener::operator()(const std::string &value) const {
    const std::shared_ptr<CsvView> self = owner.lock();
    if (self && (*self).is_alive())
        (*self).commit_cell(value);
}
void CsvView::ScrollListener::operator()(const double &) const {
    const std::shared_ptr<CsvView> self = owner.lock();
    if (self && (*self).is_alive())
        (*self).prepare_view();
}
void CsvView::EntryListener::operator()() const {
    const std::shared_ptr<CsvView> self = owner.lock();
    if (self && (*self).is_alive() && (*self).conversion_) {
        (*self).cancel_conversion();
        (*self).invalidate(gf::Dirty::paint);
    }
}
void CsvView::EntryListener::operator()(const std::string &) const {
    operator()();
}
void CsvView::initialize_control_tree() {
    entry_ = gf::make_control<gf::TextBox>(gf::StableId("csv.entry"));
    (*entry_).set_accessible_name("Cell source or formula; Enter to apply");
    (*entry_).set_maximum_length(4096);
    add_child(entry_);
    vertical_ = gf::make_control<gf::VScrollBar>(gf::StableId("csv.vertical"));
    horizontal_ = gf::make_control<gf::HScrollBar>(gf::StableId("csv.horizontal"));
    add_child(vertical_);
    add_child(horizontal_);
    subscriptions_.push_back((*entry_).committed().subscribe(*this, CommitListener{observe()}));
    subscriptions_.push_back((*entry_).text_changed().subscribe(*this, EntryListener{observe()}));
    subscriptions_.push_back((*entry_).cancelled().subscribe(*this, EntryListener{observe()}));
    subscriptions_.push_back(
        (*vertical_).value_changed().subscribe(*this, ScrollListener{observe()}));
    subscriptions_.push_back(
        (*horizontal_).value_changed().subscribe(*this, ScrollListener{observe()}));
    context_ = std::make_unique<gf::ContextMenu>("csv.context");
}
void CsvView::on_dispose() noexcept {
    cancel_calculations();
    calculator_.reset();
    subscriptions_.clear();
    tooltip_.reset();
    if (context_)
        (*context_).close();
    Control::on_dispose();
}
void CsvView::cancel_calculations() {
    cancel_conversion();
    if (calculator_)
        (*calculator_).cancel();
    calculation_requested_ = false;
    calculation_frame_.disconnect();
    calculations_.clear();
    formula_sources_.clear();
    next_calculation_ = 0;
    cells_dirty_ = true;
}
void CsvView::set_context_command(const std::shared_ptr<gf::Command> &command) {
    (*context_).set_items(
        {{"csv.convert-value", gf::MenuItemKind::command, command, "Convert to Value"}});
}
void CsvView::set_source(std::string_view source) {
    if (table_ && source == source_) {
        prepare_view();
        return;
    }
    std::shared_ptr<const swiftedit::Csv> next = std::make_shared<const swiftedit::Csv>(source);
    std::string retained(source);
    std::size_t columns = 0;
    for (const std::vector<swiftedit::Cell> &row : (*next).rows())
        columns = std::max(columns, row.size());
    cancel_conversion();
    table_ = std::move(next);
    source_ = std::move(retained);
    columns_ = columns;
    cells_dirty_ = true;
    caret_.row = std::min(caret_.row, (*table_).rows().size() - 1);
    caret_.column = std::min(caret_.column, (*table_).rows()[caret_.row].size() - 1);
    anchor_ = caret_;
    all_selected_ = false;
    update_scrollbars();
    update_field();
    prepare_view();
}
void CsvView::arrange(gf::Rect bounds) {
    const std::size_t old_rows = visible_rows_, old_columns = whole_columns_;
    arrange_self(bounds);
    set_child_layout(entry_,
                     {header_width, 4, std::max(0.0, bounds.width - header_width - 20), 30});
    set_child_layout(vertical_, {std::max(0.0, bounds.width - 18), grid_top, 18,
                                 std::max(0.0, bounds.height - grid_top - 46)});
    set_child_layout(horizontal_, {header_width, std::max(0.0, bounds.height - 46),
                                   std::max(0.0, bounds.width - header_width - 18), 18});
    visible_rows_ = std::clamp(
        static_cast<std::size_t>(std::max(1.0, (bounds.height - grid_top - 46) / row_height)),
        std::size_t(1), std::size_t(128));
    visible_columns_ = std::clamp(
        static_cast<std::size_t>(std::max(1.0, (bounds.width - header_width - 18) / column_width)) +
            1,
        std::size_t(1), std::size_t(32));
    whole_columns_ = std::clamp(
        static_cast<std::size_t>(std::max(1.0, (bounds.width - header_width - 18) / column_width)),
        std::size_t(1), std::size_t(32));
    (*vertical_).set_large_change(static_cast<double>(visible_rows_));
    (*horizontal_).set_large_change(static_cast<double>(whole_columns_));
    update_scrollbars();
    if (old_rows != visible_rows_ || old_columns != whole_columns_)
        reveal_caret();
    prepare_view();
}
void CsvView::update_scrollbars() {
    if (!table_)
        return;
    const std::size_t row_count = (*table_).rows().size();
    const std::size_t last_top = row_count > visible_rows_ ? row_count - visible_rows_ : 0;
    const std::size_t last_left = columns_ > whole_columns_ ? columns_ - whole_columns_ : 0;
    // Scrollbar ranges need a nonzero extent even when disabled. Explicitly
    // reset fitting axes so an earlier narrow layout cannot retain blank space.
    (*vertical_).set_range(0, static_cast<double>(std::max<std::size_t>(1, last_top)));
    (*horizontal_).set_range(0, static_cast<double>(std::max<std::size_t>(1, last_left)));
    (*vertical_).set_enabled(last_top != 0);
    (*horizontal_).set_enabled(last_left != 0);
    if (!last_top)
        (*vertical_).set_value(0);
    if (!last_left)
        (*horizontal_).set_value(0);
}
void CsvView::reveal_caret() {
    if (!table_)
        return;
    if (caret_.row < top_)
        (*vertical_).set_value(static_cast<double>(caret_.row));
    else if (caret_.row >= top_ + visible_rows_)
        (*vertical_).set_value(static_cast<double>(caret_.row - visible_rows_ + 1));
    // Painting includes a partially exposed column, but navigation must use
    // only complete columns so the selected cell is not hidden at the edge.
    if (caret_.column < left_)
        (*horizontal_).set_value(static_cast<double>(caret_.column));
    else if (caret_.column >= left_ + whole_columns_)
        (*horizontal_).set_value(static_cast<double>(caret_.column - whole_columns_ + 1));
}
void CsvView::prepare_view() {
    if (!table_)
        return;
    const std::size_t top = static_cast<std::size_t>((*vertical_).value());
    const std::size_t left = static_cast<std::size_t>((*horizontal_).value());
    if (!cells_dirty_ && top == top_ && left == left_ && cached_rows_ == visible_rows_ &&
        cached_columns_ == visible_columns_)
        return;
    cells_dirty_ = true;
    top_ = top;
    left_ = left;
    cancel_conversion();
    if (calculator_)
        (*calculator_).cancel();
    calculation_requested_ = false;
    calculation_frame_.disconnect();
    calculations_.clear();
    formula_sources_.clear();
    next_calculation_ = 0;
    cells_.clear();
    clear_hover();
    const std::size_t row_end = std::min((*table_).rows().size(), top_ + visible_rows_);
    for (std::size_t row = top_; row < row_end; ++row) {
        const std::size_t column_end =
            std::min((*table_).rows()[row].size(), left_ + visible_columns_);
        for (std::size_t column = left_; column < column_end; ++column) {
            const swiftedit::CellAddress address{row, column};
            const swiftedit::Cell &cell = (*table_).cell(address);
            CellDisplay display{};
            display.formula = cell.value.starts_with('=');
            display.code_background = cell.value.find("```") != std::string::npos;
            display.text = display.formula ? "..." : cell_label(cell.value);
            if (display.formula) {
                display.detail = "Calculating...";
                calculations_.push_back(address);
            }
            cells_.emplace(std::pair<std::size_t, std::size_t>{row, column}, std::move(display));
        }
    }
    cached_rows_ = visible_rows_;
    cached_columns_ = visible_columns_;
    cells_dirty_ = false;
    advance_view();
    invalidate(gf::Dirty::paint);
}
void CsvView::on_frame(gf::FrameTime) {
    if (!calculations_pending())
        return;
    try {
        advance_view();
    } catch (const std::exception &failure) {
        cancel_conversion();
        status_ = failure.what();
        if (calculator_)
            (*calculator_).cancel();
        calculation_requested_ = false;
        calculation_frame_.disconnect();
        for (std::size_t index = next_calculation_; index < calculations_.size(); ++index) {
            const swiftedit::CellAddress address = calculations_[index];
            CellDisplay &display = cells_.at({address.row, address.column});
            display.text = "#ERROR";
            display.detail = failure.what();
            display.error = true;
        }
        calculations_.clear();
        formula_sources_.clear();
        next_calculation_ = 0;
        invalidate(gf::Dirty::paint);
    }
}
void CsvView::advance_view() {
    calculation_frame_.disconnect();
    const gf::FrameTime started = gf::FrameClock::now();
    bool updated = false;
    if (calculations_pending() && !calculation_requested_) {
        if (!calculator_)
            calculator_ = std::make_unique<swiftedit::CsvCalculator>();
        (*calculator_).request(table_, calculations_);
        calculation_requested_ = true;
        // Adopt new work on a later frame, even when the worker finishes now.
        // Input must retain its cancellation boundary before conversion commits.
        if (window()) {
            const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::milliseconds(1);
            calculation_frame_ = (*window()).schedule_paint(shared_from_this(), deadline);
        }
        return;
    }
    // Formula evaluation runs against immutable owned source on one worker.
    // Only pending work schedules completion checks; idle retains no timer.
    while (calculations_pending()) {
        const swiftedit::CellAddress address = calculations_[next_calculation_];
        std::optional<swiftedit::CsvCompletion> completion = (*calculator_).take();
        if (!completion)
            break;
        if (!((*completion).address == address))
            throw std::runtime_error("CSV calculation completion does not match its viewport.");
        updated = true;
        const swiftedit::CsvCalculated &calculated = *(*completion).result;
        const swiftedit::Cell &cell = (*table_).cell(address);
        CellDisplay &display = cells_.at({address.row, address.column});
        const std::map<std::string, swiftedit::CellAddress>::const_iterator found =
            formula_sources_.find(cell.value);
        if (found != formula_sources_.end()) {
            const swiftedit::CellAddress previous = (*found).second;
            display = cells_.at({previous.row, previous.column});
        } else {
            try {
                if (calculated.failed)
                    throw std::runtime_error(calculated.error.empty()
                        ? "Formula calculation failed." : calculated.error);
                const swiftedit::Calculation &value = calculated.value;
                display.text = cell_label(value.result);
                display.detail = display.formula ? cell.value + " | References:" : "";
                std::size_t shown = 0;
                for (const swiftedit::CellAddress reference : value.references) {
                    if (reference.row >= top_ && reference.row < top_ + visible_rows_ &&
                        reference.column >= left_ && reference.column < left_ + visible_columns_)
                        display.visible_references.emplace(reference.row, reference.column);
                    if (shown < 16) {
                        const std::string name = swiftedit::cell_name(reference);
                        display.detail += " " + name;
                    } else if (shown == 16)
                        display.detail += " ...";
                    ++shown;
                }
                display.exact_value = value.result;
            } catch (const std::exception &failure) {
                display.text = "#ERROR";
                display.detail = failure.what();
                display.error = true;
            }
            // Absolute identical formulas can share only a successful result;
            // errors stay local, and reaching an identical root would cycle.
            if (!display.error)
                formula_sources_.emplace(cell.value, address);
        }
        if (hovered_ && *hovered_ == std::pair<std::size_t, std::size_t>{address.row, address.column}) {
            status_ = display.detail;
            if (tooltip_)
                (*tooltip_).set_tool_tip(shared_from_this(), status_);
        }
        ++next_calculation_;
        if (gf::FrameClock::now() - started >= std::chrono::milliseconds(2))
            break;
    }
    if (calculations_pending() && window()) {
        const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::milliseconds(1);
        calculation_frame_ = (*window()).schedule_paint(shared_from_this(), deadline);
    } else if (!calculations_pending()) {
        calculation_requested_ = false;
        formula_sources_.clear();
        calculations_.clear();
        next_calculation_ = 0;
    }
    if (updated)
        invalidate(gf::Dirty::paint);
    finish_conversion();
}
void CsvView::update_field() {
    if (conversion_ && !(*conversion_ == caret_))
        cancel_conversion();
    if (!table_)
        return;
    (*entry_).set_text((*table_).cell(caret_).value);
}
void CsvView::select_cell(swiftedit::CellAddress address) {
    if (!table_)
        return;
    static_cast<void>((*table_).cell(address));
    caret_ = address;
    anchor_ = address;
    all_selected_ = false;
    reveal_caret();
    update_field();
    invalidate(gf::Dirty::paint);
}
void CsvView::select_all() {
    cancel_conversion();
    if (!table_)
        return;
    // Keep a valid active cell even when rows have different lengths. The
    // selection covers existing fields; it never synthesizes trailing cells.
    all_selected_ = true;
    invalidate(gf::Dirty::paint);
}
void CsvView::publish(std::string source) { changed_.emit(source); }
void CsvView::commit_cell(std::string_view value) {
    cancel_conversion();
    if (!table_)
        return;
    try {
        std::string source = (*table_).set(caret_, value);
        publish(std::move(source));
        status_ = "Cell stored. Formula source is preserved; the grid displays its result.";
    } catch (const std::exception &failure) {
        status_ = failure.what();
    }
    invalidate(gf::Dirty::paint);
}
void CsvView::convert_to_value() {
    if (!table_)
        return;
    try {
        cancel_conversion();
        if (!(*table_).cell(caret_).value.starts_with('='))
            throw std::runtime_error("The selected cell does not contain a formula.");
        reveal_caret();
        prepare_view();
        conversion_ = caret_;
        status_ = "Waiting for formula result. Escape cancels conversion.";
        finish_conversion();
    } catch (const std::exception &failure) {
        cancel_conversion();
        status_ = failure.what();
    }
    invalidate(gf::Dirty::paint);
}
void CsvView::cancel_conversion() noexcept {
    if (conversion_) {
        conversion_.reset();
        status_.clear();
    }
}
void CsvView::finish_conversion() {
    if (!conversion_ || cells_dirty_ || !table_)
        return;
    try {
        const swiftedit::CellAddress address = *conversion_;
        const std::map<std::pair<std::size_t, std::size_t>, CellDisplay>::const_iterator found =
            cells_.find({address.row, address.column});
        if (found == cells_.end())
            throw std::runtime_error("Conversion cancelled because the cell is no longer visible.");
        const CellDisplay &display = (*found).second;
        if (display.error)
            throw std::runtime_error(display.detail);
        if (!display.exact_value)
            return;
        // Cache and intent belong to the current immutable source/viewport.
        // Clear the intent before publishing: listeners may replace the source.
        std::string source = (*table_).set(address, *display.exact_value);
        conversion_.reset();
        publish(std::move(source));
        status_ = "Converted formula to value. Undo restores the formula.";
    } catch (const std::exception &failure) {
        cancel_conversion();
        status_ = failure.what();
    }
    invalidate(gf::Dirty::paint);
}
void CsvView::clear_cells() {
    cancel_conversion();
    if (!table_)
        return;
    try {
        const swiftedit::CellAddress first{std::min(anchor_.row, caret_.row),
                                           std::min(anchor_.column, caret_.column)};
        const swiftedit::CellAddress last{std::max(anchor_.row, caret_.row),
                                          std::max(anchor_.column, caret_.column)};
        std::string source{};
        if (all_selected_)
            source = (*table_).clear_all();
        else
            source = (*table_).clear(first, last);
        publish(std::move(source));
        status_ = "Cell contents cleared; rows and columns remain in place.";
    } catch (const std::exception &failure) {
        status_ = failure.what();
    }
    invalidate(gf::Dirty::paint);
}
std::string CsvView::copy_cells() const {
    std::string result{};
    if (!table_)
        return result;
    swiftedit::CellAddress first{std::min(anchor_.row, caret_.row),
                                 std::min(anchor_.column, caret_.column)};
    swiftedit::CellAddress last{std::max(anchor_.row, caret_.row),
                                std::max(anchor_.column, caret_.column)};
    if (all_selected_) {
        first = {0, 0};
        last.row = (*table_).rows().size() - 1;
    }
    for (std::size_t row = first.row; row <= last.row; ++row) {
        if (row != first.row)
            result += "\r\n";
        const std::size_t end_column =
            all_selected_ ? (*table_).rows()[row].size() - 1 : last.column;
        for (std::size_t column = first.column; column <= end_column; ++column) {
            if (column != first.column)
                result += '\t';
            const swiftedit::Cell &cell = (*table_).cell({row, column});
            result += cell.value;
        }
    }
    return result;
}
std::optional<swiftedit::CellAddress> CsvView::hit(gf::Point position) const {
    const gf::Rect bounds = absolute_bounds();
    const double x = position.x - bounds.x - header_width;
    const double y = position.y - bounds.y - grid_top;
    if (!table_ || x < 0 || y < 0 || x >= bounds.width - header_width - 18 ||
        y >= bounds.height - grid_top - 46)
        return std::nullopt;
    const swiftedit::CellAddress address{top_ + static_cast<std::size_t>(y / row_height),
                                         left_ + static_cast<std::size_t>(x / column_width)};
    if (address.row >= (*table_).rows().size() ||
        address.column >= (*table_).rows()[address.row].size())
        return std::nullopt;
    const std::optional<swiftedit::CellAddress> result = address;
    return result;
}
void CsvView::clear_hover() {
    const bool displayed = hovered_.has_value();
    hovered_.reset();
    if (tooltip_)
        (*tooltip_).hide();
    if (displayed) {
        status_.clear();
        invalidate(gf::Dirty::paint);
    }
}
void CsvView::on_pointer(gf::PointerEvent &event) {
    if (event.phase != gf::EventPhase::target)
        return;
    if (event.action == gf::PointerAction::leave) {
        clear_hover();
        return;
    }
    if (event.action == gf::PointerAction::wheel) {
        if (event.wheel_delta.y != 0 && table_ && (*table_).rows().size() > visible_rows_)
            (*vertical_).increment(event.wheel_delta.y < 0 ? 3 : -3);
        event.handled = true;
        return;
    }
    if (event.action == gf::PointerAction::up) {
        dragging_ = false;
        set_pointer_capture(false);
    }
    const std::optional<swiftedit::CellAddress> cell = hit(event.position);
    if (!cell) {
        if (event.action == gf::PointerAction::move)
            clear_hover();
        return;
    }
    if (event.action == gf::PointerAction::down) {
        select_cell(*cell);
        if (window())
            (*window()).request_focus(shared_from_this());
        if (event.button == gf::PointerButton::secondary)
            (*context_).show(shared_from_this(), event.position, gf::MenuOpenMode::pointer);
        else if (event.button == gf::PointerButton::primary) {
            dragging_ = true;
            set_pointer_capture(true);
            if (event.click_count == 2 && window())
                (*window()).request_focus(entry_);
        }
        event.handled = true;
    } else if (event.action == gf::PointerAction::move) {
        const bool selection_changed = dragging_ &&
            (caret_.row != (*cell).row || caret_.column != (*cell).column);
        if (selection_changed) {
            caret_ = *cell;
            update_field();
        }
        const std::pair<std::size_t, std::size_t> key{(*cell).row, (*cell).column};
        if (hovered_ == key && !selection_changed)
            return;
        const std::map<std::pair<std::size_t, std::size_t>, CellDisplay>::const_iterator found =
            cells_.find({(*cell).row, (*cell).column});
        if (found != cells_.end()) {
            status_ = (*found).second.detail;
            if (hovered_ != key && window()) {
                if (!tooltip_)
                    tooltip_ = std::make_unique<gf::ToolTip>(*window());
                (*tooltip_).set_tool_tip(shared_from_this(), status_);
                if (status_.empty())
                    (*tooltip_).hide();
                else
                    (*tooltip_).show(shared_from_this());
            }
            hovered_ = key;
        } else
            clear_hover();
        invalidate(gf::Dirty::paint);
    }
}
void CsvView::on_focus_changed(bool focused) {
    if (!focused && conversion_) {
        cancel_conversion();
        invalidate(gf::Dirty::paint);
    }
}
void CsvView::on_key(gf::KeyEvent &event) {
    if (event.phase != gf::EventPhase::target || event.action != gf::KeyAction::down || !table_)
        return;
    if (event.physical_key == gf::PhysicalKey::escape && conversion_) {
        cancel_conversion();
        status_ = "Conversion cancelled.";
        invalidate(gf::Dirty::paint);
        event.handled = true;
        return;
    }
    if (event.physical_key == gf::PhysicalKey::a &&
        (event.modifiers == gf::Modifier::control || event.modifiers == gf::Modifier::meta)) {
        select_all();
        event.handled = true;
    } else if (event.physical_key == gf::PhysicalKey::delete_forward) {
        clear_cells();
        event.handled = true;
    } else if (event.physical_key == gf::PhysicalKey::enter && window()) {
        cancel_conversion();
        (*window()).request_focus(entry_);
        (*entry_).select_all();
        event.handled = true;
    } else {
        swiftedit::CellAddress next = caret_;
        const bool document = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
            gf::has_modifier(event.modifiers, gf::Modifier::meta);
        if (event.physical_key == gf::PhysicalKey::left && next.column)
            --next.column;
        else if (event.physical_key == gf::PhysicalKey::right &&
                 next.column + 1 < (*table_).rows()[next.row].size())
            ++next.column;
        else if (event.physical_key == gf::PhysicalKey::up && next.row)
            --next.row;
        else if (event.physical_key == gf::PhysicalKey::down &&
                 next.row + 1 < (*table_).rows().size())
            ++next.row;
        else if (event.physical_key == gf::PhysicalKey::page_up)
            next.row -= std::min(next.row, visible_rows_);
        else if (event.physical_key == gf::PhysicalKey::page_down)
            next.row += std::min(visible_rows_, (*table_).rows().size() - 1 - next.row);
        else if (event.physical_key == gf::PhysicalKey::home) {
            if (document) next.row = 0;
            next.column = 0;
        } else if (event.physical_key == gf::PhysicalKey::end) {
            if (document) next.row = (*table_).rows().size() - 1;
            next.column = (*table_).rows()[next.row].size() - 1;
        }
        else
            return;
        next.column = std::min(next.column, (*table_).rows()[next.row].size() - 1);
        caret_ = next;
        all_selected_ = false;
        if (!gf::has_modifier(event.modifiers, gf::Modifier::shift))
            anchor_ = next;
        reveal_caret();
        update_field();
        invalidate(gf::Dirty::paint);
        event.handled = true;
    }
}
void CsvView::on_paint(gf::Painter &painter, gf::Rect) {
    const gf::Rect bounds = arranged_bounds();
    const gf::BasicControlStyle &style = effective_theme().basic_style();
    const gf::FontSpec font{gf::FontRole::content, 13, 400, false};
    const gf::ResolvedTextLayout font_metrics = painter.resolve_text_layout_utf8("Mg", font);
    const double text_height = font_metrics.ascent + font_metrics.descent;
    const double row_baseline = (row_height - text_height) * 0.5 + font_metrics.ascent;
    painter.fill_rect({0, 0, bounds.width, bounds.height}, style.paper);
    const std::string address = swiftedit::cell_name(caret_);
    painter.draw_text_utf8({4, 4 + row_baseline}, address, font, style.text);
    const CellDisplay *hover = nullptr;
    if (hovered_) {
        const std::map<std::pair<std::size_t, std::size_t>, CellDisplay>::const_iterator found =
            cells_.find(*hovered_);
        if (found != cells_.end())
            hover = &(*found).second;
    }
    for (const std::pair<const std::pair<std::size_t, std::size_t>, CellDisplay> &entry : cells_) {
        const std::size_t row = entry.first.first, column = entry.first.second;
        const gf::Rect rect{header_width + static_cast<double>(column - left_) * column_width,
                            grid_top + static_cast<double>(row - top_) * row_height, column_width,
                            row_height};
        const bool selected = all_selected_ || (row >= std::min(anchor_.row, caret_.row) &&
                                                row <= std::max(anchor_.row, caret_.row) &&
                                                column >= std::min(anchor_.column, caret_.column) &&
                                                column <= std::max(anchor_.column, caret_.column));
        if (selected)
            painter.fill_rect(rect, style.accent_light);
        else if (entry.second.code_background)
            painter.fill_rect(rect, style.face_light);
        painter.stroke_rect(rect, style.border, 1);
        if (hover && (*hover).visible_references.contains(entry.first))
            painter.stroke_rect(rect, style.accent, 2);
        painter.save();
        painter.clip_rect(rect);
        double text_x = rect.x + 5;
        if (entry.second.error) {
            // Draw the warning independently of font glyph coverage. Theme text
            // keeps it legible on ordinary, selected and high-contrast cells.
            const double x = rect.x + 5, y = rect.y + 5;
            painter.draw_line({x + 7, y}, {x + 14, y + 15}, style.text, 1.5);
            painter.draw_line({x + 14, y + 15}, {x, y + 15}, style.text, 1.5);
            painter.draw_line({x, y + 15}, {x + 7, y}, style.text, 1.5);
            painter.fill_rect({x + 6.25, y + 5, 1.5, 5}, style.text);
            painter.fill_rect({x + 6.25, y + 12, 1.5, 1.5}, style.text);
            text_x += 20;
        }
        painter.draw_text_utf8({text_x, rect.y + row_baseline}, entry.second.text, font, style.text);
        if (entry.second.formula)
            for (int stripe = 0; stripe < 5; ++stripe)
                painter.draw_line({rect.x + rect.width - 6 + stripe, rect.y + 1 + stripe},
                                  {rect.x + rect.width - 1, rect.y + 1 + stripe}, style.accent, 1);
        painter.restore();
    }
    for (std::size_t index = 0; index < visible_columns_ && left_ + index < columns_; ++index) {
        std::string column = swiftedit::cell_name({0, left_ + index});
        column.pop_back();
        painter.draw_text_utf8({header_width + static_cast<double>(index) * column_width + 5,
                               grid_top - row_height + row_baseline},
                               column, font, style.text);
    }
    if (table_)
        for (std::size_t index = 0; index < visible_rows_ && top_ + index < (*table_).rows().size();
             ++index) {
            const std::string row = std::to_string(top_ + index + 1);
            painter.draw_text_utf8({4, grid_top + static_cast<double>(index) * row_height + row_baseline}, row,
                                   font, style.text);
        }
    painter.draw_text_utf8({4, std::max(font_metrics.ascent, bounds.height - 6 - font_metrics.descent)}, status_, font, style.text);
}
} // namespace notepad
