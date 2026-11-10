#include "marker_strip.hpp"
#include <algorithm>
#include <cmath>

namespace swiftedit {
namespace gf = gui_forms;
namespace hl = swiftedit::highlight;

MarkerStrip::MarkerStrip(gf::StableId id) : Control(std::move(id)) {}

void MarkerStrip::set_markers(std::span<const hl::LineMarker> markers, std::size_t line_count) {
    markers_.assign(markers.begin(), markers.end());
    line_count_ = std::max<std::size_t>(line_count, 1U);
    invalidate(gf::Dirty::paint);
}

double MarkerStrip::mark_top(const hl::LineMarker &marker, double height) const noexcept {
    const double room = std::max(0.0, height - mark_height);
    const std::size_t line = std::min(marker.line.value, line_count_ - 1U);
    // One line's share; the last line's mark sits at the bottom.
    const double share = line_count_ > 1U ? static_cast<double>(line) / static_cast<double>(line_count_ - 1U)
                                          : 0.0;
    const double result = std::floor(room * share);
    return result;
}

std::optional<std::size_t> MarkerStrip::marker_at(double y, double height) const noexcept {
    std::optional<std::size_t> nearest{};
    double nearest_distance = hit_slop;
    for (std::size_t index = 0U; index < markers_.size(); ++index) {
        const double top = mark_top(markers_[index], height);
        const double bottom = top + mark_height;
        const double distance = y < top ? top - y : (y > bottom ? y - bottom : 0.0);
        // Strictly nearer only, so the first listed wins a tie.
        if (distance < nearest_distance || (!nearest && distance <= hit_slop)) {
            nearest = index;
            nearest_distance = distance;
        }
    }
    return nearest;
}

void MarkerStrip::on_paint(gf::Painter &painter, gf::Rect) {
    const gf::Rect bounds = committed_arranged_bounds();
    const gf::BasicControlStyle &style = effective_theme().basic_style();
    painter.fill_rect({0, 0, bounds.width, bounds.height}, style.face_light);
    painter.draw_line({0.5, 0}, {0.5, bounds.height}, style.border, 1);
    const double left = 2.0;
    const double width = std::max(0.0, bounds.width - 3.0);
    // Rules first, so a diagnostic on the same line is drawn over its rule.
    for (std::size_t pass = 0U; pass < 2U; ++pass) {
        const bool rules_pass = pass == 0U;
        for (const hl::LineMarker &marker : markers_) {
            const bool rule = marker.kind == hl::DecorationKind::rule_match ||
                              marker.kind == hl::DecorationKind::rule_focus;
            if (rule != rules_pass)
                continue;
            const double top = mark_top(marker, bounds.height);
            if (rule) {
                painter.fill_rect({left, top, std::max(1.0, width / 3.0), mark_height}, style.accent);
            } else if (marker.kind == hl::DecorationKind::diagnostic_warning) {
                painter.fill_rect({left + width / 2.0, top, width / 2.0, mark_height}, style.dark_border);
            } else {
                painter.fill_rect({left, top, width, mark_height}, style.text);
            }
        }
    }
}

void MarkerStrip::on_pointer(gf::PointerEvent &event) {
    if (event.phase != gf::EventPhase::target || event.action != gf::PointerAction::down ||
        event.button != gf::PointerButton::primary)
        return;
    const gf::Rect bounds = absolute_bounds();
    const std::optional<std::size_t> index = marker_at(event.position.y - bounds.y, bounds.height);
    event.handled = true;
    if (!index)
        return;
    // A copy: a subscriber may replace the marks while the event is raised.
    const hl::LineMarker marker = markers_[*index];
    activated_.emit(marker);
}

} // namespace swiftedit
