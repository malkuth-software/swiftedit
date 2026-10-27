#include "characters.hpp"
#include "session.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace swiftedit {
namespace {
struct CharacterRecord {
    char32_t first{}, last{};
    const char *category{}, *name{};
};
constexpr CharacterRecord records[] = {
#include "unicode_data.inc"
};
struct BeforeScalar {
    bool operator()(const CharacterRecord &record, char32_t scalar) const {
        const bool before = record.last < scalar;
        return before;
    }
};
std::string hex(char32_t scalar) {
    char buffer[8] = {};
    const std::to_chars_result written =
        std::to_chars(buffer, buffer + 8, static_cast<std::uint32_t>(scalar), 16);
    std::string result(buffer, written.ptr);
    for (char &digit : result)
        if (digit >= 'a' && digit <= 'f')
            digit -= 'a' - 'A';
    if (result.size() < 4)
        result.insert(0, 4 - result.size(), '0');
    return result;
}
std::string hangul_name(char32_t scalar) {
    constexpr const char *leading[] = {"G",  "GG", "N", "D",  "DD", "R", "M", "B", "BB", "S",
                                       "SS", "",   "J", "JJ", "C",  "K", "T", "P", "H"};
    constexpr const char *vowel[] = {"A",   "AE", "YA", "YAE", "EO", "E",  "YEO",
                                     "YE",  "O",  "WA", "WAE", "OE", "YO", "U",
                                     "WEO", "WE", "WI", "YU",  "EU", "YI", "I"};
    constexpr const char *trailing[] = {"",   "G",  "GG", "GS", "N",  "NJ", "NH", "D", "L",  "LG",
                                        "LM", "LB", "LS", "LT", "LP", "LH", "M",  "B", "BS", "S",
                                        "SS", "NG", "J",  "C",  "K",  "T",  "P",  "H"};
    const std::size_t index = static_cast<std::size_t>(scalar - 0xac00);
    std::string name = "HANGUL SYLLABLE ";
    name += leading[index / 588];
    name += vowel[(index % 588) / 28];
    name += trailing[index % 28];
    return name;
}
std::string control_explanation(char32_t scalar, std::string_view category) {
    switch (scalar) {
    case 0:
        return "Null character. Some external programs treat it as a string terminator.";
    case 9:
        return "Horizontal tab. Moves to a tab stop; it is not a sequence of spaces.";
    case 10:
        return "Line feed. A line ending, or the second character of a CRLF pair.";
    case 13:
        return "Carriage return. A line ending, or the first character of a CRLF pair.";
    case 27:
        return "Escape character. External terminals may interpret following text as commands; "
               "SwiftEdit displays it inertly.";
    case 0x85:
        return "Next-line control. Some text formats interpret it as a line boundary.";
    case 0xad:
        return "Soft hyphen. Marks an optional hyphenation opportunity.";
    case 0x200b:
        return "Zero-width space. An invisible word or line-break opportunity.";
    case 0x200c:
        return "Zero-width non-joiner. Prevents joining between neighboring characters in "
               "supporting scripts.";
    case 0x200d:
        return "Zero-width joiner. Participates in joining and emoji sequences; deleting it can "
               "change their appearance.";
    case 0x200e:
        return "Left-to-right mark. Influences the direction of neighboring text in supporting "
               "renderers.";
    case 0x200f:
        return "Right-to-left mark. Influences the direction of neighboring text in supporting "
               "renderers.";
    case 0x2028:
        return "Unicode line separator. A text line boundary.";
    case 0x2029:
        return "Unicode paragraph separator. A paragraph boundary.";
    case 0x2060:
        return "Word joiner. Prevents a line break at this position.";
    case 0xfeff:
        return "Byte order mark at the start of some encoded files; inside text it is an actual "
               "format character.";
    default:
        break;
    }
    if ((scalar >= 0x202a && scalar <= 0x202e) || (scalar >= 0x2066 && scalar <= 0x2069) ||
        scalar == 0x61c)
        return "Bidirectional formatting control. It can alter text direction in supporting "
               "renderers. Stored literally; previewed inertly here.";
    if (category == "Cf")
        return "Unicode format character. It carries formatting or language metadata rather than "
               "an ordinary visible glyph. Stored literally.";
    return "Unicode control character. Its interpretation depends on the receiving text format or "
           "program. Stored literally; not executed by this dialog.";
}
} // namespace
std::string codepoint_label(char32_t scalar) {
    const std::string result = "U+" + hex(scalar);
    return result;
}
CharacterInfo character_info(char32_t scalar) {
    CharacterInfo result{};
    result.scalar = scalar;
    if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
        result.name = "Not a Unicode scalar value";
        return result;
    }
    const CharacterRecord *found =
        std::lower_bound(std::begin(records), std::end(records), scalar, BeforeScalar{});
    if (found == std::end(records) || (*found).first > scalar) {
        result.name = "Unassigned or noncharacter (Unicode 17.0.0)";
        return result;
    }
    result.assigned = true;
    result.category = (*found).category;
    result.control = result.category == "Cc" || result.category == "Cf" ||
                     result.category == "Zl" || result.category == "Zp";
    result.name = (*found).name;
    if (scalar >= 0xac00 && scalar <= 0xd7a3)
        result.name = hangul_name(scalar);
    else if (result.name.starts_with("CJK Ideograph"))
        result.name = "CJK UNIFIED IDEOGRAPH-" + hex(scalar);
    else if (result.name.starts_with("Tangut Ideograph"))
        result.name = "TANGUT IDEOGRAPH-" + hex(scalar);
    if (result.control)
        result.explanation = control_explanation(scalar, result.category);
    else if (result.category == "Co")
        result.explanation = "Private-use character. Its meaning and appearance depend on the "
                             "chosen font or private agreement.";
    else if (result.category.starts_with('M'))
        result.explanation =
            "Combining mark. Usually modifies an adjacent character rather than displaying alone.";
    else
        result.explanation =
            "Ordinary Unicode text. Appearance depends on available font coverage.";
    return result;
}
char32_t parse_codepoint(std::string_view text) {
    if (text.starts_with("U+") || text.starts_with("u+"))
        text.remove_prefix(2);
    if (text.empty() || text.size() > 6)
        throw std::runtime_error("Enter one Unicode code point, such as U+00E9.");
    std::uint32_t value{};
    const std::from_chars_result parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
        throw std::runtime_error("Code point must be a Unicode scalar, excluding surrogates.");
    const char32_t scalar = static_cast<char32_t>(value);
    return scalar;
}
std::string character_utf8(char32_t scalar) {
    const CharacterInfo info = character_info(scalar);
    if (!info.assigned)
        throw std::runtime_error("Choose an assigned Unicode text or control character.");
    std::string result{};
    if (scalar < 0x80)
        result += static_cast<char>(scalar);
    else if (scalar < 0x800) {
        result += static_cast<char>(0xc0 | (scalar >> 6));
        result += static_cast<char>(0x80 | (scalar & 63));
    } else if (scalar < 0x10000) {
        result += static_cast<char>(0xe0 | (scalar >> 12));
        result += static_cast<char>(0x80 | ((scalar >> 6) & 63));
        result += static_cast<char>(0x80 | (scalar & 63));
    } else {
        result += static_cast<char>(0xf0 | (scalar >> 18));
        result += static_cast<char>(0x80 | ((scalar >> 12) & 63));
        result += static_cast<char>(0x80 | ((scalar >> 6) & 63));
        result += static_cast<char>(0x80 | (scalar & 63));
    }
    return result;
}
std::vector<CharacterInfo> character_page(char32_t first, bool controls) {
    if (first > 0x10ffff)
        throw std::runtime_error("Character page exceeds Unicode range.");
    std::vector<CharacterInfo> result{};
    result.reserve(256);
    const char32_t end = std::min<char32_t>(0x110000, first + 256);
    for (char32_t scalar = first; scalar < end; ++scalar) {
        CharacterInfo info = character_info(scalar);
        if (info.assigned && info.control == controls)
            result.push_back(std::move(info));
    }
    return result;
}
std::vector<CharacterInfo> control_characters() {
    std::vector<CharacterInfo> result{};
    for (const CharacterRecord &record : records) {
        const std::string_view category(record.category);
        if (category != "Cc" && category != "Cf" && category != "Zl" && category != "Zp")
            continue;
        for (char32_t scalar = record.first; scalar <= record.last; ++scalar) {
            if (result.size() >= 1024)
                throw std::runtime_error("Control catalog exceeds its display budget.");
            CharacterInfo info = character_info(scalar);
            result.push_back(std::move(info));
        }
    }
    return result;
}
std::string inspect_characters(std::string_view text) {
    std::string result = "Unicode 17.0.0; stored UTF-8 values\n";
    std::size_t offset = 0, shown = 0;
    while (offset < text.size() && shown < 32) {
        const std::size_t length = utf8_sequence_length(text, offset);
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        if (!length) {
            result += "Illegal UTF-8 byte " + hex(first) + "\n";
            ++offset;
        } else {
            char32_t scalar = first;
            if (length > 1) {
                scalar = first & (length == 2 ? 31 : length == 3 ? 15 : 7);
                for (std::size_t index = 1; index < length; ++index)
                    scalar =
                        (scalar << 6) | (static_cast<unsigned char>(text[offset + index]) & 63);
            }
            const CharacterInfo info = character_info(scalar);
            result += codepoint_label(scalar) + "  " + info.name + "  [" + info.category + "]\n";
            result += "UTF-8 bytes:";
            for (std::size_t index = 0; index < length; ++index) {
                const std::string bytes = hex(static_cast<unsigned char>(text[offset + index]));
                result += " " + bytes.substr(2);
            }
            result += "\n";
            offset += length;
        }
        ++shown;
    }
    if (offset < text.size())
        result += "Showing the first 32 scalar values/illegal bytes only.\n";
    if (text.empty())
        result += "No character at the caret.\n";
    return result;
}
} // namespace swiftedit
