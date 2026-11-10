#pragma once
// An overview strip beside a code editor's text: one mark per diagnostic and
// theme rule, placed at its line's share of the document's height, so marks
// stay visible whatever part of the text is scrolled into view. Clicking a
// mark raises `activated` with it; the editor moves the caret there.
//
// Kinds are told apart by shape as well as colour: an error spans the strip,
// a warning its right half, a theme rule a narrow band on the left.
#include "highlight/code_document.hpp"
#include <gui_forms/gui_forms.hpp>
#include <optional>
#include <span>
#include <vector>

namespace swiftedit {

class MarkerStrip final : public gui_forms::Control {
public:
    // The height of one mark and the distance within which a click finds it.
    static constexpr double mark_height = 4.0;
    static constexpr double hit_slop = 3.0;

    explicit MarkerStrip(gui_forms::StableId id);

    // Replaces the marks. `line_count` is the document's line count (at least
    // one is assumed); a mark past it is drawn at the bottom.
    void set_markers(std::span<const highlight::LineMarker> markers, std::size_t line_count);
    [[nodiscard]] std::span<const highlight::LineMarker> markers() const noexcept { return markers_; }
    [[nodiscard]] std::size_t line_count() const noexcept { return line_count_; }
    // The top of the band `marker` is drawn in, for a strip `height` tall.
    [[nodiscard]] double mark_top(const highlight::LineMarker &marker, double height) const noexcept;
    // The mark whose band is nearest `y` (within hit_slop), preferring the
    // first listed (diagnostics come before rules), for a strip `height` tall.
    [[nodiscard]] std::optional<std::size_t> marker_at(double y, double height) const noexcept;
    [[nodiscard]] gui_forms::Event<const highlight::LineMarker &> &activated() noexcept { return activated_; }

    void on_paint(gui_forms::Painter &painter, gui_forms::Rect damage) override;
    void on_pointer(gui_forms::PointerEvent &event) override;

private:
    std::vector<highlight::LineMarker> markers_{};
    std::size_t line_count_{1U};
    gui_forms::Event<const highlight::LineMarker &> activated_{};
};

} // namespace swiftedit
