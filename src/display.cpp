#include "display.hpp"
#include <algorithm>
#include <gui_forms/text.hpp>
#include <stdexcept>

namespace swiftedit {
namespace {
struct DisplayOffsetLess {
    bool operator()(const DisplayUnit &unit, std::size_t offset) const {
        const bool before = unit.display.offset < offset;
        return before;
    }
};
struct SourceOffsetLess {
    bool operator()(const DisplayUnit &unit, std::size_t offset) const {
        const bool before = unit.source.offset < offset;
        return before;
    }
};
std::size_t source_boundary(const std::vector<DisplayUnit> &units, std::size_t offset,
                            std::size_t display_size, std::size_t source_size) {
    if (offset == display_size)
        return source_size;
    const std::vector<DisplayUnit>::const_iterator found =
        std::lower_bound(units.begin(), units.end(), offset, DisplayOffsetLess{});
    if (found == units.end() || (*found).display.offset != offset)
        throw std::runtime_error("Select whole characters or control labels.");
    return (*found).source.offset;
}
bool visible_control(char32_t scalar) {
    const bool control = scalar < 32 || (scalar >= 0x7f && scalar <= 0x9f) || scalar == 0xad ||
                         scalar == 0x61c || scalar == 0x200b || scalar == 0x200e ||
                         scalar == 0x200f || (scalar >= 0x202a && scalar <= 0x202e) ||
                         (scalar >= 0x2060 && scalar <= 0x206f) || scalar == 0xfeff;
    return control;
}
std::string hex_label(char32_t value, const char *prefix, std::size_t digits) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result(prefix);
    for (std::size_t index = digits; index > 0; --index) {
        const std::size_t shift = (index - 1) * 4;
        result += hex[(value >> shift) & 15];
    }
    result += ']';
    return result;
}
} // namespace
DisplayPage::DisplayPage(std::string_view source) : source_size_(source.size()) {
    if (source.size() > maximum_page)
        throw std::runtime_error("Display page exceeds 65536 source bytes.");
    text_.reserve(source.size());
    units_.reserve(source.size());
    // Preserve byte offsets in the segmentation metadata. Illegal bytes become
    // single-byte control sentinels, which cannot join neighboring graphemes.
    std::string metadata(source);
    for (std::size_t index = 0; index < metadata.size();) {
        const std::size_t scalar_length = utf8_sequence_length(source, index);
        if (scalar_length)
            index += scalar_length;
        else {
            metadata[index] = '\x01';
            ++index;
        }
    }
    const gui_forms::TextStore boundaries(metadata);
    for (std::size_t offset = 0; offset < source.size();) {
        const std::size_t next = boundaries.next_grapheme_boundary(gui_forms::Utf8Offset(offset)).value();
        const std::size_t length = next - offset;
        const std::size_t scalar_length = utf8_sequence_length(source, offset);
        const unsigned char first = static_cast<unsigned char>(source[offset]);
        DisplayUnit unit{{offset, length}, {text_.size(), 0}, DisplayKind::text};
        if (!scalar_length) {
            unit.kind = DisplayKind::illegal_byte;
            const std::string label = hex_label(first, "[BYTE ", 2);
            text_.append(label);
        } else if (first == '\r' || first == '\n' || first == '\t') {
            // Grapheme segmentation keeps CRLF as one source selection unit.
            text_.append(source.substr(offset, length));
        } else {
            char32_t scalar = first;
            if (scalar_length > 1) {
                scalar = first & (scalar_length == 2 ? 31 : scalar_length == 3 ? 15 : 7);
                for (std::size_t index = 1; index < scalar_length; ++index) {
                    const unsigned char next = static_cast<unsigned char>(source[offset + index]);
                    scalar = (scalar << 6) | (next & 63);
                }
            }
            const bool control = visible_control(scalar);
            if (control) {
                unit.kind = DisplayKind::control;
                const std::string label = hex_label(scalar, "[U+", scalar > 0xffff ? 6 : 4);
                text_.append(label);
            } else
                text_.append(source.substr(offset, length));
        }
        unit.display.length = text_.size() - unit.display.offset;
        units_.push_back(unit);
        offset += length;
    }
}
SourceRange DisplayPage::source_range(SourceRange display) const {
    if (display.offset > text_.size() || display.length > text_.size() - display.offset)
        throw std::runtime_error("Display selection exceeds page.");
    const std::size_t end = display.offset + display.length;
    const std::size_t first = source_boundary(units_, display.offset, text_.size(), source_size_);
    const std::size_t last = source_boundary(units_, end, text_.size(), source_size_);
    const SourceRange result{first, last - first};
    return result;
}
std::size_t DisplayPage::display_offset(std::size_t source_offset) const {
    if (source_offset == source_size_) {
        const std::size_t end = text_.size();
        return end;
    }
    const std::vector<DisplayUnit>::const_iterator found =
        std::lower_bound(units_.begin(), units_.end(), source_offset, SourceOffsetLess{});
    if (found == units_.end() || (*found).source.offset != source_offset)
        throw std::runtime_error("Source position splits a display unit.");
    return (*found).display.offset;
}
} // namespace swiftedit
