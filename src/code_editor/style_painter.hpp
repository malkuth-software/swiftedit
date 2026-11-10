#pragma once
// Paints a CodeDocument's style runs on a multiline TextBox through GUI.Forms'
// styled text runs (TextDecorations): syntax colours, the rules that style
// the inspected control, compile diagnostics as squiggles and search matches
// as grounds.
//
// The document composes its tokens and decoration layers into runs; the
// painter turns each styled run into one TextDecoration of the box's
// `style_layer` and replaces that layer. The box keeps the decorations
// through edits (they follow the text) until the next paint replaces them.
//
// Colours: GUI.Forms 0.5 names a closed set of colour roles and has no
// custom ones, so the syntax palette is held here, in light terms. Forced high contrast ignores authored
// foregrounds and grounds (the theme's colours stand), so the text stays
// legible there; squiggles keep their shape.
#include "highlight/code_document.hpp"
#include <gui_forms/gui_forms.hpp>
#include <array>
#include <cstddef>
#include <vector>

namespace swiftedit {

// The colours a painter gives each token kind and decoration kind.
struct StylePalette final {
    // Indexed by TokenKind's value; plain text has no foreground.
    std::array<std::optional<gui_forms::Color>, highlight::token_kind_count> syntax{};
    gui_forms::Color rule_ground{};
    gui_forms::Color rule_focus_ground{};
    gui_forms::Color search_ground{};
    gui_forms::Color search_current_ground{};
    gui_forms::Color error_underline{};
    gui_forms::Color warning_underline{};
};

// SwiftEdit's syntax palette, in light terms.
[[nodiscard]] const StylePalette &default_style_palette() noexcept;

// The look one style run gets: syntax foreground, then a ground for a rule
// or search decoration (current match over match over rule focus over rule),
// then a squiggle for a diagnostic (error over warning). An undecorated
// plain run has an empty style.
[[nodiscard]] gui_forms::TextDecorationStyle decoration_style(const highlight::StyleRun &run,
                                                              const StylePalette &palette) noexcept;

class StylePainter final {
public:
    // The TextBox layer the painter owns.
    static constexpr gui_forms::TextDecorationLayer style_layer{0U};
    // A text longer than this is not decorated (the layer is cleared): the
    // painter composes the whole text on every paint.
    static constexpr std::size_t maximum_painted_bytes = 4U * 1024U * 1024U;

    // Replaces `box`'s style layer with `document`'s runs over its whole
    // text (clears it for plain text, an empty text or one past the
    // maximum). `document` mirrors `box`'s text (CodeDocument::update before
    // each paint). Reuses the painter's storage.
    void paint(highlight::CodeDocument &document, gui_forms::TextBox &box);
    void paint(highlight::CodeDocument &document, gui_forms::TextBox &box, const StylePalette &palette);

private:
    std::vector<highlight::StyleRun> runs_{};
    std::vector<gui_forms::TextDecoration> decorations_{};
};

} // namespace swiftedit
