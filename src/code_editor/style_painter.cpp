#include "code_editor/style_painter.hpp"

namespace swiftedit {
namespace gf = gui_forms;
namespace hl = swiftedit::highlight;

namespace {

StylePalette make_default_palette() noexcept {
    StylePalette palette{};
    const gf::Color comment = gf::Color::rgba(0x5c, 0x6b, 0x77);
    const gf::Color string = gf::Color::rgba(0x8a, 0x3b, 0x12);
    const gf::Color number = gf::Color::rgba(0x0b, 0x5d, 0x73);
    const gf::Color keyword = gf::Color::rgba(0x6a, 0x2c, 0x91);
    const gf::Color selector = gf::Color::rgba(0x19, 0x52, 0x8b);
    const gf::Color property = gf::Color::rgba(0x1d, 0x5e, 0x2f);
    const gf::Color function = gf::Color::rgba(0x7a, 0x4b, 0x00);
    const gf::Color invalid = gf::Color::rgba(0xa4, 0x16, 0x1a);
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::comment)] = comment;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::string)] = string;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::number)] = number;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::color)] = number;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::keyword)] = keyword;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::at_rule)] = keyword;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::important)] = keyword;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::type_selector)] = selector;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::identity_selector)] = selector;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::class_selector)] = selector;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::pseudo_selector)] = selector;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::property)] = property;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::custom_property)] = property;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::function)] = function;
    palette.syntax[static_cast<std::size_t>(hl::TokenKind::invalid)] = invalid;
    palette.rule_ground = gf::Color::rgba(0xe3, 0xee, 0xfa);
    palette.rule_focus_ground = gf::Color::rgba(0xc8, 0xdc, 0xf4);
    palette.search_ground = gf::Color::rgba(0xfb, 0xee, 0xb8);
    palette.search_current_ground = gf::Color::rgba(0xf4, 0xcf, 0x5a);
    palette.error_underline = invalid;
    palette.warning_underline = function;
    return palette;
}

} // namespace

const StylePalette &default_style_palette() noexcept {
    static const StylePalette palette = make_default_palette();
    return palette;
}

gf::TextDecorationStyle decoration_style(const hl::StyleRun &run, const StylePalette &palette) noexcept {
    gf::TextDecorationStyle style{};
    const std::size_t kind = static_cast<std::size_t>(run.syntax);
    if (kind < palette.syntax.size() && palette.syntax[kind])
        style.foreground = *palette.syntax[kind];
    const hl::DecorationMask mask = run.decorations;
    if (mask.has(hl::DecorationKind::search_current))
        style.background = palette.search_current_ground;
    else if (mask.has(hl::DecorationKind::search_match))
        style.background = palette.search_ground;
    else if (mask.has(hl::DecorationKind::rule_focus))
        style.background = palette.rule_focus_ground;
    else if (mask.has(hl::DecorationKind::rule_match))
        style.background = palette.rule_ground;
    if (mask.has(hl::DecorationKind::diagnostic_error)) {
        style.underline = gf::TextUnderline::squiggle;
        style.underline_color = palette.error_underline;
    } else if (mask.has(hl::DecorationKind::diagnostic_warning)) {
        style.underline = gf::TextUnderline::squiggle;
        style.underline_color = palette.warning_underline;
    }
    return style;
}

void StylePainter::paint(hl::CodeDocument &document, gf::TextBox &box) {
    paint(document, box, default_style_palette());
}

void StylePainter::paint(hl::CodeDocument &document, gf::TextBox &box, const StylePalette &palette) {
    const std::size_t length = document.text().size();
    const bool plain = document.language().name() == "plain";
    if (plain || length == 0U || length > maximum_painted_bytes) {
        box.clear_decorations(style_layer);
        return;
    }
    const hl::ByteRange whole{hl::ByteOffset{0U}, hl::ByteOffset{length}};
    document.style_runs(whole, runs_);
    decorations_.clear();
    for (const hl::StyleRun &run : runs_) {
        const gf::TextDecorationStyle style = decoration_style(run, palette);
        if (style.empty() || run.range.empty())
            continue;
        gf::TextDecoration decoration{};
        decoration.range = gf::Utf8Range{gf::Utf8Offset(run.range.start.value), gf::Utf8Offset(run.range.end.value)};
        decoration.style = style;
        decoration.tag = static_cast<std::uint32_t>(run.syntax);
        decorations_.push_back(decoration);
    }
    box.set_decorations(style_layer, decorations_);
}

} // namespace swiftedit
