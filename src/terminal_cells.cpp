#include "terminal_cells.hpp"
#include "session.hpp"
#include <algorithm>
#include <stdexcept>
namespace swiftedit {
namespace {
struct Properties {
    char32_t first{}, last{};
    unsigned flags{};
};
constexpr Properties properties[] = {
#include "terminal_unicode.inc"
};
unsigned flags_for(char32_t scalar) {
    std::size_t first = 0;
    std::size_t last = sizeof(properties) / sizeof(properties[0]);
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (properties[middle].last < scalar)
            first = middle + 1;
        else
            last = middle;
    }
    if (first == sizeof(properties) / sizeof(properties[0]) || scalar < properties[first].first)
        return 0;
    return properties[first].flags;
}
void append_label(std::string &text, char32_t value, bool byte) {
    constexpr char digits[] = "0123456789ABCDEF";
    text += byte ? "[BYTE " : "[U+";
    const std::size_t count = byte ? 2 : value > 0xffff ? 6 : 4;
    for (std::size_t i = count; i > 0; --i)
        text += digits[(value >> ((i - 1) * 4)) & 15];
    text += ']';
}
char32_t scalar_at(std::string_view source, std::size_t offset, std::size_t length) {
    const unsigned char first = static_cast<unsigned char>(source[offset]);
    char32_t scalar = first;
    if (length > 1) {
        scalar = first & (length == 2 ? 31 : length == 3 ? 15 : 7);
        for (std::size_t i = 1; i < length; ++i)
            scalar = (scalar << 6) | (static_cast<unsigned char>(source[offset + i]) & 63);
    }
    return scalar;
}
} // namespace
std::size_t terminal_ascii_cells(const unsigned char byte, const std::size_t column) {
    if (byte > 127)
        throw std::runtime_error("ASCII width requires a standalone ASCII source unit.");
    if (byte == '\t') {
        const std::size_t cells = 4 - column % 4;
        return cells;
    }
    if (byte < 32 || byte == 127)
        return 8; // [U+0000] through [U+007F], matching terminal_glyph labels.
    return 1;
}
TerminalGlyph terminal_glyph(std::string_view source, std::size_t column) {
    if (source.empty() || source.size() > maximum_page)
        throw std::runtime_error("Terminal glyph must contain 1..65536 source bytes.");
    TerminalGlyph result{};
    if (source == "\t") {
        result.cells = 4 - column % 4;
        result.text.assign(result.cells, ' ');
        return result;
    }
    // Inspect before copying: any non-joining control or malformed byte causes
    // the entire cluster to become an ASCII label, preventing invisible state.
    bool label = false;
    bool emoji = false;
    bool presentation = false;
    bool text_presentation = false;
    bool hangul = false;
    bool regional = false;
    bool joined = false;
    std::size_t sum = 0;
    for (std::size_t i = 0; i < source.size();) {
        const std::size_t length = utf8_sequence_length(source, i);
        if (!length) {
            label = true;
            ++i;
            continue;
        }
        const char32_t scalar = scalar_at(source, i, length);
        const unsigned flags = flags_for(scalar);
        const bool joiner = scalar == 0x200d || scalar == 0x200c;
        joined = joined || joiner;
        if ((flags & 1) && !joiner)
            label = true;
        if (flags & 8)
            emoji = true;
        presentation = presentation || scalar == 0xfe0f;
        text_presentation = text_presentation || scalar == 0xfe0e;
        hangul = hangul || (scalar >= 0x1100 && scalar <= 0x115f);
        regional = regional || (scalar >= 0x1f1e6 && scalar <= 0x1f1ff);
        if (!(flags & 2) && !joiner)
            sum += flags & 4 ? 2 : 1;
        i += length;
    }
    label = label || (joined && sum == 0);
    if (label) {
        result.text.reserve(source.size() * 12);
        for (std::size_t i = 0; i < source.size();) {
            const std::size_t length = utf8_sequence_length(source, i);
            if (!length) {
                append_label(result.text, static_cast<unsigned char>(source[i]), true);
                ++i;
            } else {
                const char32_t scalar = scalar_at(source, i, length);
                append_label(result.text, scalar, false);
                i += length;
            }
        }
        result.cells = result.text.size();
        result.label = true;
        return result;
    }
    result.text = source;
    if (presentation || regional || (emoji && !text_presentation) || hangul)
        result.cells = 2;
    else
        result.cells = sum;
    if (!result.cells) {
        // A leading combining-only cluster needs a visible host cell.
        result.text.insert(0, "\xe2\x97\x8c");
        result.cells = 1;
    }
    return result;
}
} // namespace swiftedit
