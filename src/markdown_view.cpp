#include "markdown_view.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace notepad {
namespace {
constexpr double ruler_height = 28, page_margin = 24;
struct WordMetrics {
    gf::Size size{};
    double ascent{}, descent{};
};
// One layout owns this cache. Font changes discard it; measurement invalidation
// discards the entire work owner. No Painter is retained between slices.
class MarkdownMeasurements final {
public:
    WordMetrics measure(gf::TextMetricsProvider &painter, const std::string &text, const gf::FontSpec font) {
        if (!font_ || !(*font_ == font)) {
            values_.clear();
            retained_bytes_ = 0;
            font_ = font;
        }
        const std::map<std::string, WordMetrics>::const_iterator found = values_.find(text);
        if (found != values_.end())
            return (*found).second;
        const gf::ResolvedTextLayout resolved = painter.resolve_text_layout_utf8(text, font);
        const WordMetrics result{resolved.logical_size, resolved.ascent, resolved.descent};
        if (text.size() <= 256 && values_.size() < 1024 && text.size() <= 65536 - retained_bytes_) {
            values_.emplace(text, result);
            retained_bytes_ += text.size();
        }
        return result;
    }
private:
    std::optional<gf::FontSpec> font_{};
    std::map<std::string, WordMetrics> values_{};
    std::size_t retained_bytes_{};
};
} // namespace
// UI-owned partial geometry. URL borrows remain valid until this work is reset,
// before replacement of blocks_. No Painter or host handle survives a slice.
struct MarkdownView::LayoutWork {
    MarkdownMeasurements measurements{};
    std::vector<Run> next{};
    std::size_t retained_bytes{}, block_index{}, span_index{}, offset{};
    double y{page_margin}, maximum_x{}, width{};
    bool block_started{};
    std::vector<double> x{}, row_y{}, line_height{};
};
MarkdownView::~MarkdownView() = default;
MarkdownView::MarkdownView(gf::StableId id) : Control(std::move(id)) { set_focusable(true); }
bool MarkdownView::RunOrder::operator()(const Run &left, const Run &right) const {
    const bool before = left.bounds.y < right.bounds.y;
    return before;
}
MarkdownView::VisibleRuns::VisibleRuns(const MarkdownView &owner, double top, double bottom)
    : owner_(owner), top_(top), bottom_(bottom) {
    if (!owner_.runs_.empty()) {
        pending_[0] = {1, 0, owner_.run_leaves_};
        count_ = 1;
    }
}
std::optional<std::size_t> MarkdownView::VisibleRuns::next() {
    while (count_) {
        const Branch branch = pending_[--count_];
        if (branch.first >= owner_.runs_.size() ||
            owner_.runs_[branch.first].bounds.y > bottom_ ||
            owner_.run_bottoms_[branch.node] < top_)
            continue;
        if (branch.last - branch.first == 1) {
            const std::optional<std::size_t> result = branch.first;
            return result;
        }
        const std::size_t middle = branch.first + (branch.last - branch.first) / 2;
        // Right first on the stack preserves the existing stable paint order.
        pending_[count_++] = {branch.node * 2 + 1, middle, branch.last};
        pending_[count_++] = {branch.node * 2, branch.first, middle};
    }
    return std::nullopt;
}
void MarkdownView::ScrollListener::operator()(const double &) const {
    const std::shared_ptr<MarkdownView> self = owner.lock();
    if (self && (*self).is_alive()) {
        (*self).clear_hover();
        (*self).invalidate(gf::Dirty::paint);
    }
}
void MarkdownView::initialize_control_tree() {
    vertical_ = gf::make_control<gf::VScrollBar>(gf::StableId("markdown.vertical"));
    horizontal_ = gf::make_control<gf::HScrollBar>(gf::StableId("markdown.horizontal"));
    add_child(vertical_);
    add_child(horizontal_);
    const std::shared_ptr<MarkdownView> self =
        std::static_pointer_cast<MarkdownView>(shared_from_this());
    const std::weak_ptr<MarkdownView> observer = self;
    subscriptions_.push_back(
        (*vertical_).value_changed().subscribe(*this, ScrollListener{observer}));
    subscriptions_.push_back(
        (*horizontal_).value_changed().subscribe(*this, ScrollListener{observer}));
    (*vertical_).set_small_change(32);
    (*horizontal_).set_small_change(32);
}
void MarkdownView::on_dispose() noexcept {
    cancel_preparation();
    preparation_.reset();
    subscriptions_.clear();
    tooltip_.reset();
    Control::on_dispose();
}
void MarkdownView::set_source(std::string_view source) {
    if (source == source_ && (source_prepared_ || preparation_pending_))
        return;
    std::string retained(source);
    if (!preparation_)
        preparation_ = std::make_unique<swiftedit::MarkdownPreparation>();
    (*preparation_).request(retained);
    preparation_frame_.disconnect();
    preparation_pending_ = true;
    source_prepared_ = false;
    layout_work_.reset();
    runs_.clear();
    run_bottoms_.clear();
    blocks_.clear();
    source_ = std::move(retained);
    layout_dirty_ = true;
    layout_error_.clear();
    clear_hover();
    schedule_preparation();
    invalidate(gf::Dirty::paint);
}
void MarkdownView::PreparationTick::operator()(const gf::FrameTime now) const {
    const std::shared_ptr<MarkdownView> target = owner.lock();
    if (target)
        (*target).on_frame(now);
}
void MarkdownView::schedule_preparation() {
    if ((preparation_pending_ || layout_pending()) && window() && !preparation_frame_.connected()) {
        const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::milliseconds(1);
        const std::shared_ptr<MarkdownView> self =
            std::static_pointer_cast<MarkdownView>(shared_from_this());
        preparation_frame_ = (*window()).schedule_ui_timer(
            *this, gf::minimum_ui_timer_interval, deadline, PreparationTick{self});
    }
}
void MarkdownView::cancel_preparation() {
    if (preparation_)
        (*preparation_).cancel();
    preparation_frame_.disconnect();
    if (layout_dirty_)
        source_prepared_ = false;
    layout_work_.reset();
    preparation_pending_ = false;
}
void MarkdownView::on_frame(gf::FrameTime) {
    preparation_frame_.disconnect();
    if (!preparation_pending_) {
        if (!layout_pending())
            return;
        // Read the current service only for this UI callback. Let pending
        // measure/arrange passes revoke geometry before doing more work.
        gf::TextMetricsProvider *provider = window() ? (*window()).text_metrics_provider() : nullptr;
        if (provider && !gf::has_dirty(dirty(), gf::Dirty::measure | gf::Dirty::arrange)) {
            advance_layout(*provider);
            if (layout_pending())
                schedule_preparation();
            else
                invalidate(gf::Dirty::paint);
        } else {
            invalidate(gf::Dirty::paint);
        }
        return;
    }
    try {
        std::optional<swiftedit::PreparedMarkdown> completed = (*preparation_).take();
        if (!completed) {
            schedule_preparation();
            return;
        }
        blocks_ = std::move((*completed).blocks);
        preparation_pending_ = false;
        source_prepared_ = true;
        layout_dirty_ = true;
    } catch (const std::exception &failure) {
        preparation_pending_ = false;
        source_prepared_ = false;
        layout_dirty_ = false;
        layout_error_ = failure.what();
    }
    invalidate(gf::Dirty::paint);
}
gf::Size MarkdownView::measure(const gf::Size available) {
    // Window provider/theme changes invalidate measurement even when width
    // stays fixed. Geometry must not retain metrics from the preceding pass.
    layout_work_.reset();
    layout_dirty_ = true;
    const gf::Size desired = Control::measure(available);
    return desired;
}
void MarkdownView::arrange(gf::Rect bounds) {
    clear_hover();
    arrange_self(bounds);
    set_child_layout(vertical_, {std::max(0.0, bounds.width - 18), ruler_height, 18,
                                 std::max(0.0, bounds.height - ruler_height - 18)});
    set_child_layout(horizontal_,
                     {0, std::max(0.0, bounds.height - 18), std::max(0.0, bounds.width - 18), 18});
    (*vertical_).set_large_change(std::max(32.0, bounds.height - ruler_height - 18));
    (*horizontal_).set_large_change(std::max(32.0, bounds.width - 18));
    if (layout_work_ && (*layout_work_).width != bounds.width)
        layout_work_.reset();
    if (layout_width_ != bounds.width)
        layout_dirty_ = true;
    if (!layout_dirty_)
        update_scroll_ranges();
    schedule_preparation();
}
void MarkdownView::layout(gf::TextMetricsProvider &painter, double width) {
    if (!layout_work_) {
        layout_painted_ = false;
        layout_work_ = std::make_unique<LayoutWork>();
        (*layout_work_).next.reserve(std::min<std::size_t>(source_.size() / 4 + 1, 250000));
        (*layout_work_).width = width;
        (*layout_work_).maximum_x = width - 18;
        layout_duration_ = std::chrono::nanoseconds::zero();
        longest_layout_slice_ = std::chrono::nanoseconds::zero();
    }
    LayoutWork &work = *layout_work_;
    std::vector<Run> &next = work.next;
    std::size_t &retained_bytes = work.retained_bytes;
    double &y = work.y;
    double &maximum_x = work.maximum_x;
    MarkdownMeasurements &measurements = work.measurements;
    constexpr std::size_t storage_limit = 32 * 1024 * 1024;
    const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::milliseconds(8);
    std::size_t steps = 0;
    for (; work.block_index < blocks_.size(); ++work.block_index) {
        if (steps >= 32768 || gf::FrameClock::now() >= deadline)
            return;
        ++steps;
        const swiftedit::MarkdownBlock &block = blocks_[work.block_index];
        if (!work.block_started &&
            (next.size() >= 250000 || block.columns > 250000 - next.size()))
            throw std::runtime_error("Markdown layout exceeds display run budget.");
        const double indent = static_cast<double>(std::min<std::size_t>(block.indent, 12)) * 20;
        const double start = page_margin + indent + (block.quoted ? 16 : 0);
        const double available = std::max(80.0, width - start - page_margin - 18);
        if (block.kind == swiftedit::MarkdownKind::rule) {
            Run rule{};
            rule.bounds = {start, y + 8, available, 1};
            rule.rule = true;
            next.push_back(std::move(rule));
            y += 24;
            continue;
        }
        const bool table = block.kind == swiftedit::MarkdownKind::table_row;
        const std::size_t columns = table ? std::max<std::size_t>(1, block.columns) : 1;
        const double column_width =
            table ? std::max(96.0, available / static_cast<double>(columns)) : available;
        std::vector<double> &x = work.x;
        std::vector<double> &row_y = work.row_y;
        std::vector<double> &line_height = work.line_height;
        if (!work.block_started) {
            x.assign(columns, 0);
            row_y.assign(columns, y);
            line_height.assign(columns, 20);
            for (std::size_t column = 0; column < columns; ++column)
                x[column] = start + static_cast<double>(column) * column_width + (table ? 6 : 0);
            work.block_started = true;
        }
        for (; work.span_index < block.spans.size(); ++work.span_index) {
            if (steps >= 32768 || gf::FrameClock::now() >= deadline)
                return;
            ++steps;
            const swiftedit::MarkdownSpan &span = block.spans[work.span_index];
            const std::size_t column = table ? std::min(span.column, columns - 1) : 0;
            const double left =
                start + static_cast<double>(column) * column_width + (table ? 6 : 0);
            const double right = left + column_width - (table ? 12 : 0);
            gf::FontSpec font{gf::FontRole::content, 14, 400, span.italic};
            if (span.bold || block.header || block.kind == swiftedit::MarkdownKind::heading)
                font.weight = 700;
            if (span.code || block.kind == swiftedit::MarkdownKind::code) {
                font.role = gf::FontRole::monospace;
                font.size = 13;
            }
            if (block.kind == swiftedit::MarkdownKind::heading)
                font.size = 30 - static_cast<double>(std::min<std::size_t>(block.level, 6)) * 2;
            const std::string &safe = span.text;
            std::size_t &offset = work.offset;
            for (; offset < safe.size();) {
                if (steps >= 32768 || gf::FrameClock::now() >= deadline)
                    return;
                ++steps;
                if (safe[offset] == '\n' || safe[offset] == '\r') {
                    x[column] = left;
                    row_y[column] += line_height[column];
                    line_height[column] = std::max(20.0, font.size * 1.5);
                    if (safe[offset] == '\r' && offset + 1 < safe.size() &&
                        safe[offset + 1] == '\n')
                        ++offset;
                    ++offset;
                    continue;
                }
                std::size_t end = offset + 1;
                if (safe[offset] != ' ' && safe[offset] != '\t') {
                    const std::size_t separator = safe.find_first_of(" \t\r\n", offset);
                    end = separator == safe.npos ? safe.size() : separator;
                }
                const std::string word =
                    safe[offset] == '\t' ? "    " : safe.substr(offset, end - offset);
                if (word.size() > storage_limit - retained_bytes)
                    throw std::runtime_error("Markdown layout exceeds display storage budget.");
                const std::size_t text_total = retained_bytes + word.size();
                if (next.size() >= 250000)
                    throw std::runtime_error("Markdown layout exceeds display run budget.");
                const WordMetrics metrics = measurements.measure(painter, word, font);
                const gf::Size measured = metrics.size;
                const double height = std::max(20.0, std::max(measured.height,
                    metrics.ascent + metrics.descent) + 4);
                if (block.kind != swiftedit::MarkdownKind::code && x[column] > left &&
                    x[column] + measured.width > right) {
                    x[column] = left;
                    row_y[column] += line_height[column];
                    line_height[column] = height;
                }
                line_height[column] = std::max(line_height[column], height);
                Run run{};
                run.bounds = {x[column], row_y[column], measured.width, height};
                run.font = font;
                run.baseline = 2 + metrics.ascent;
                run.underline = run.baseline + std::max(1.0, metrics.descent * 0.5);
                run.strike_y = run.baseline - metrics.ascent * 0.35;
                run.text = word;
                run.url = span.url;
                run.strike = span.strike;
                run.code = span.code || block.kind == swiftedit::MarkdownKind::code;
                next.push_back(std::move(run));
                retained_bytes = text_total;
                x[column] += measured.width;
                maximum_x = std::max(maximum_x, x[column] + page_margin);
                offset = end;
            }
            work.offset = 0;
        }
        double bottom = y + 20;
        for (std::size_t column = 0; column < columns; ++column)
            bottom = std::max(bottom, row_y[column] + line_height[column]);
        if (table) {
            if (columns > 250000 - next.size())
                throw std::runtime_error("Markdown layout exceeds display run budget.");
            for (std::size_t column = 0; column < columns; ++column) {
                Run border{};
                border.bounds = {start + static_cast<double>(column) * column_width, y,
                                 column_width, bottom - y + 4};
                border.border = true;
                next.push_back(std::move(border));
            }
            maximum_x = std::max(maximum_x,
                                 start + static_cast<double>(columns) * column_width + page_margin);
        }
        if (block.quoted) {
            if (next.size() >= 250000)
                throw std::runtime_error("Markdown layout exceeds display run budget.");
            Run quote{};
            quote.bounds = {start - 12, y, 2, bottom - y};
            quote.rule = true;
            next.push_back(std::move(quote));
        }
        y = bottom + (table ? 4 : 12);
        work.block_started = false;
        work.span_index = 0;
    }
    std::stable_sort(next.begin(), next.end(), RunOrder{});
    // Each subtree records its greatest bottom. Together with sorted starting
    // positions, this prunes off-screen text even beneath a very tall quote.
    std::size_t leaves = 1;
    while (leaves < next.size())
        leaves *= 2;
    std::vector<double> bottoms(leaves * 2, std::numeric_limits<double>::lowest());
    for (std::size_t index = 0; index < next.size(); ++index)
        bottoms[leaves + index] = next[index].bounds.y + next[index].bounds.height;
    for (std::size_t index = leaves - 1; index > 0; --index)
        bottoms[index] = std::max(bottoms[index * 2], bottoms[index * 2 + 1]);
    const gf::FontSpec ruler_font{gf::FontRole::content, 11, 400, false};
    const gf::ResolvedTextLayout ruler_metrics = painter.resolve_text_layout_utf8("0", ruler_font);
    runs_ = std::move(next);
    run_bottoms_ = std::move(bottoms);
    run_leaves_ = leaves;
    ruler_baseline_ = 3 + ruler_metrics.ascent;
    content_height_ = y + page_margin;
    content_width_ = maximum_x;
    layout_width_ = width;
    layout_dirty_ = false;
    layout_work_.reset();
    preparation_frame_.disconnect();
    update_scroll_ranges();
}
void MarkdownView::update_scroll_ranges() {
    const gf::Rect bounds = arranged_bounds();
    const double vertical_extent =
        std::max(0.0, content_height_ - bounds.height + ruler_height + 18);
    const double horizontal_extent = std::max(0.0, content_width_ - bounds.width + 18);
    (*vertical_).set_range(0, std::max(1.0, vertical_extent));
    (*horizontal_).set_range(0, std::max(1.0, horizontal_extent));
    (*vertical_).set_enabled(vertical_extent > 0);
    (*horizontal_).set_enabled(horizontal_extent > 0);
    if (!vertical_extent)
        (*vertical_).set_value(0);
    if (!horizontal_extent)
        (*horizontal_).set_value(0);
}
void MarkdownView::advance_layout(gf::TextMetricsProvider &provider) {
    const gf::FrameTime layout_start = gf::FrameClock::now();
    try {
        layout(provider, arranged_bounds().width);
        layout_error_.clear();
    } catch (const std::exception &failure) {
        layout_error_ = failure.what();
        layout_work_.reset();
        layout_dirty_ = false;
    }
    const std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        gf::FrameClock::now() - layout_start);
    layout_duration_ += elapsed;
    longest_layout_slice_ = std::max(longest_layout_slice_, elapsed);
}
void MarkdownView::on_paint(gf::Painter &painter, gf::Rect) {
    const gf::Rect bounds = arranged_bounds();
    const gf::BasicControlStyle &style = effective_theme().basic_style();
    painter.fill_rect({0, 0, bounds.width, bounds.height}, style.paper);
    if (preparation_pending_ || (!source_prepared_ && layout_error_.empty())) {
        schedule_preparation();
        const gf::FontSpec font{gf::FontRole::content, 14, 400, false};
        const std::string_view message = preparation_pending_
            ? "Preparing Markdown. Escape cancels." : "Markdown preparation cancelled.";
        painter.draw_text_utf8({24, 48}, message, font, style.text);
        return;
    }
    if (layout_pending())
        advance_layout(painter);
    if (layout_pending()) {
        schedule_preparation();
        const gf::FontSpec font{gf::FontRole::content, 14, 400, false};
        painter.draw_text_utf8({24, 48}, "Laying out Markdown. Escape cancels.", font, style.text);
        return;
    }
    if (!layout_error_.empty()) {
        const gf::FontSpec font{gf::FontRole::content, 14, 400, false};
        painter.draw_text_utf8({24, 48}, layout_error_, font, style.text);
        return;
    }
    const double top = (*vertical_).value(), left = (*horizontal_).value();
    painter.save();
    painter.clip_rect({0, ruler_height, std::max(0.0, bounds.width - 18),
                       std::max(0.0, bounds.height - ruler_height - 18)});
    painter.translate({-left, ruler_height - top});
    VisibleRuns visible(*this, top, top + std::max(0.0, bounds.height - ruler_height - 18));
    for (std::optional<std::size_t> index = visible.next(); index; index = visible.next()) {
        const Run &item = runs_[*index];
        if (item.border) {
            painter.stroke_rect(item.bounds, style.border, 1);
            continue;
        }
        if (item.rule) {
            painter.fill_rect(item.bounds, style.border);
            continue;
        }
        if (item.code)
            painter.fill_rect(item.bounds, style.face_light);
        const gf::Color color = item.url.empty() ? style.text : style.link;
        // Space-only runs still own geometry, code backgrounds and link/strike
        // decorations. They have no visible glyph ink to submit to the host.
        if (item.text.find_first_not_of(' ') != std::string::npos)
            painter.draw_text_utf8({item.bounds.x, item.bounds.y + item.baseline}, item.text, item.font, color);
        if (item.strike || !item.url.empty()) {
            const double baseline = item.bounds.y + (item.strike ? item.strike_y : item.underline);
            painter.draw_line({item.bounds.x, baseline},
                              {item.bounds.x + item.bounds.width, baseline}, color, 1);
        }
    }
    painter.restore();
    painter.fill_rect({0, 0, std::max(0.0, bounds.width - 18), ruler_height}, style.face_light);
    const gf::FontSpec ruler_font{gf::FontRole::content, 11, 400, false};
    const double first_tick = std::ceil(left / 48) * 48;
    for (double tick = first_tick; tick < left + bounds.width - 18; tick += 48) {
        const double position = tick - left;
        painter.draw_line({position, 20}, {position, ruler_height}, style.border, 1);
        const std::string label = std::to_string(static_cast<unsigned>(tick / 48));
        painter.draw_text_utf8({position + 3, ruler_baseline_}, label, ruler_font, style.disabled_text);
    }
    layout_painted_ = true;
}
void MarkdownView::clear_hover() {
    hovered_url_.clear();
    if (tooltip_)
        (*tooltip_).hide();
}
void MarkdownView::on_key(gf::KeyEvent &event) {
    if (event.phase == gf::EventPhase::target && event.action == gf::KeyAction::down &&
        event.physical_key == gf::PhysicalKey::escape && (preparation_pending_ || layout_pending())) {
        cancel_preparation();
        invalidate(gf::Dirty::paint);
        event.handled = true;
        return;
    }
    if (event.phase != gf::EventPhase::target || event.action != gf::KeyAction::down ||
        preparation_pending_ || !source_prepared_ || layout_dirty_ || !layout_error_.empty())
        return;
    const bool plain = event.modifiers == gf::Modifier::none;
    const bool document = event.modifiers == gf::Modifier::control || event.modifiers == gf::Modifier::meta;
    const gf::Rect bounds = arranged_bounds();
    const double page = std::max(32.0, bounds.height - ruler_height - 18);
    if (plain && event.physical_key == gf::PhysicalKey::up) {
        if ((*vertical_).enabled()) (*vertical_).increment(-32);
    } else if (plain && event.physical_key == gf::PhysicalKey::down) {
        if ((*vertical_).enabled()) (*vertical_).increment(32);
    } else if (plain && event.physical_key == gf::PhysicalKey::page_up) {
        if ((*vertical_).enabled()) (*vertical_).increment(-page);
    } else if (plain && event.physical_key == gf::PhysicalKey::page_down) {
        if ((*vertical_).enabled()) (*vertical_).increment(page);
    } else if ((plain || document) && event.physical_key == gf::PhysicalKey::home) {
        (*vertical_).set_value(0);
    } else if ((plain || document) && event.physical_key == gf::PhysicalKey::end) {
        if ((*vertical_).enabled())
            (*vertical_).set_value(std::max(0.0, content_height_ - bounds.height + ruler_height + 18));
    } else if (plain && event.physical_key == gf::PhysicalKey::left) {
        if ((*horizontal_).enabled()) (*horizontal_).increment(-32);
    } else if (plain && event.physical_key == gf::PhysicalKey::right) {
        if ((*horizontal_).enabled()) (*horizontal_).increment(32);
    } else {
        return;
    }
    event.handled = true;
}
void MarkdownView::on_pointer(gf::PointerEvent &event) {
    if (event.phase != gf::EventPhase::target)
        return;
    if (event.action == gf::PointerAction::wheel) {
        const gf::Rect bounds = arranged_bounds();
        const double extent = content_height_ - bounds.height + ruler_height + 18;
        if (!layout_dirty_ && layout_error_.empty() && extent > 0 && event.wheel_delta.y != 0)
            (*vertical_).increment(event.wheel_delta.y < 0 ? 64 : -64);
        event.handled = true;
        return;
    }
    if (event.action == gf::PointerAction::down) {
        if (window()) (*window()).request_focus(shared_from_this());
        event.handled = true; // Links are intentionally inert, including file: URLs.
        return;
    }
    if (event.action != gf::PointerAction::move && event.action != gf::PointerAction::leave)
        return;
    const gf::Rect bounds = absolute_bounds();
    const gf::Rect content{bounds.x, bounds.y + ruler_height,
                           std::max(0.0, bounds.width - 18),
                           std::max(0.0, bounds.height - ruler_height - 18)};
    const gf::Point point{event.position.x - bounds.x + (*horizontal_).value(),
                          event.position.y - bounds.y - ruler_height + (*vertical_).value()};
    std::string url{};
    if (event.action == gf::PointerAction::move && content.contains(event.position) &&
        !layout_dirty_ && layout_error_.empty()) {
        VisibleRuns visible(*this, point.y, point.y);
        for (std::optional<std::size_t> index = visible.next(); index; index = visible.next())
            if (!runs_[*index].url.empty() && runs_[*index].bounds.contains(point)) {
                url = runs_[*index].url;
                break;
            }
    }
    if (url == hovered_url_ || !window())
        return;
    hovered_url_ = url;
    if (!tooltip_)
        tooltip_ = std::make_unique<gf::ToolTip>(*window());
    (*tooltip_).set_tool_tip(shared_from_this(), url);
    if (url.empty())
        (*tooltip_).hide();
    else
        (*tooltip_).show(shared_from_this());
}
} // namespace notepad
