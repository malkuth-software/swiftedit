#include "document.hpp"
#include "word_count.hpp"
#include <algorithm>
#include <gui_forms/text.hpp>
#include <stdexcept>

namespace notepad {
std::size_t word_count(std::string_view text) {
    WordCounter counter{};
    counter.append(text);
    const std::uint64_t words = counter.finish();
    // Every word consumes at least one byte of this size_t-sized view.
    const std::size_t result = static_cast<std::size_t>(words);
    return result;
}
namespace {
void check_text(std::string_view text, const TextControls controls = TextControls::legacy_gui) {
    if (controls != TextControls::legacy_gui && controls != TextControls::preserve)
        throw std::runtime_error("Unsupported text control policy.");
    if (text.size() > maximum_bytes)
        throw std::runtime_error("This build supports text up to 16 MiB.");
    const gui_forms::Utf8ValidationResult valid = gui_forms::validate_utf8(text);
    if (!valid.valid())
        throw std::runtime_error("Malformed UTF-8 at byte " +
                                 std::to_string(valid.error_offset.value()) +
                                 ". No text was replaced. Legacy encodings require explicit "
                                 "conversion in another tool.");
    if (controls == TextControls::preserve)
        return;
    for (unsigned char ch : text) {
        if (ch < 32 && ch != '\r' && ch != '\n' && ch != '\t')
            throw std::runtime_error("Binary-looking control characters are unsupported. The "
                                     "original file has not been changed.");
    }
}
void append_utf8(std::string &out, std::uint32_t cp) {
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
unsigned char fold(unsigned char c) {
    const unsigned char folded = c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c;
    return folded;
}
bool equal_folded(char a, char b) {
    const unsigned char left = fold(static_cast<unsigned char>(a));
    const unsigned char right = fold(static_cast<unsigned char>(b));
    const bool equal = left == right;
    return equal;
}
using ReadUnit = std::uint32_t (*)(std::string_view, std::size_t);
std::uint32_t read_little_unit(std::string_view bytes, std::size_t i) {
    const std::uint32_t low = static_cast<unsigned char>(bytes[i]);
    const std::uint32_t high = static_cast<unsigned char>(bytes[i + 1]);
    const std::uint32_t value = low | (high << 8);
    return value;
}
std::uint32_t read_big_unit(std::string_view bytes, std::size_t i) {
    const std::uint32_t high = static_cast<unsigned char>(bytes[i]);
    const std::uint32_t low = static_cast<unsigned char>(bytes[i + 1]);
    const std::uint32_t value = low | (high << 8);
    return value;
}
using EmitUnit = void (*)(std::string &, unsigned);
void emit_little_unit(std::string &output, unsigned unit) {
    output += static_cast<char>(unit & 255);
    output += static_cast<char>(unit >> 8);
}
void emit_big_unit(std::string &output, unsigned unit) {
    output += static_cast<char>(unit >> 8);
    output += static_cast<char>(unit & 255);
}

} // namespace
Decoded decode(std::string_view bytes, const TextControls controls) {
    if (bytes.size() > maximum_bytes)
        throw std::runtime_error("This build opens files up to 16 MiB.");
    Decoded d{};
    if (bytes.starts_with(std::string_view("\xff\xfe\0\0", 4)) ||
        bytes.starts_with(std::string_view("\0\0\xfe\xff", 4)))
        throw std::runtime_error("UTF-32 is not supported by this build. No file was changed.");
    if (bytes.starts_with("\xef\xbb\xbf")) {
        d.encoding = Encoding::utf8_bom;
        d.text = bytes.substr(3);
    } else if (bytes.starts_with("\xff\xfe") || bytes.starts_with("\xfe\xff")) {
        const bool little = bytes.starts_with("\xff\xfe");
        d.encoding = little ? Encoding::utf16_le : Encoding::utf16_be;
        if (bytes.size() % 2)
            throw std::runtime_error("Malformed UTF-16: odd byte count.");
        const ReadUnit unit = little ? read_little_unit : read_big_unit;
        d.text.reserve(bytes.size());
        for (std::size_t i = 2; i < bytes.size(); i += 2) {
            std::uint32_t cp = unit(bytes, i);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (i + 3 >= bytes.size())
                    throw std::runtime_error("Malformed UTF-16: missing low surrogate.");
                const std::uint32_t lo = unit(bytes, i + 2);
                if (lo < 0xdc00 || lo > 0xdfff)
                    throw std::runtime_error("Malformed UTF-16: invalid low surrogate.");
                cp = 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00;
                i += 2;
            } else if (cp >= 0xdc00 && cp <= 0xdfff)
                throw std::runtime_error("Malformed UTF-16: isolated low surrogate.");
            append_utf8(d.text, cp);
        }
    } else
        d.text = bytes;
    check_text(d.text, controls);
    return d;
}
std::string encode(std::string_view text, Encoding encoding, const TextControls controls) {
    if (encoding != Encoding::utf8 && encoding != Encoding::utf8_bom &&
        encoding != Encoding::utf16_le && encoding != Encoding::utf16_be)
        throw std::runtime_error("Unsupported text encoding.");
    check_text(text, controls);
    if (encoding == Encoding::utf8) {
        const std::string result(text);
        return result;
    }
    if (encoding == Encoding::utf8_bom) {
        if (text.size() + 3 > maximum_bytes)
            throw std::runtime_error("Encoded output exceeds the 16 MiB file limit.");
        const std::string result = "\xef\xbb\xbf" + std::string(text);
        return result;
    }
    std::string out = encoding == Encoding::utf16_le ? "\xff\xfe" : "\xfe\xff";
    const EmitUnit emit = encoding == Encoding::utf16_le ? emit_little_unit : emit_big_unit;
    out.reserve(text.size() * 2 + 2);
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i++]);
        unsigned cp = first;
        int more = 0;
        if (first >= 0xf0) {
            cp = first & 7;
            more = 3;
        } else if (first >= 0xe0) {
            cp = first & 15;
            more = 2;
        } else if (first >= 0xc0) {
            cp = first & 31;
            more = 1;
        }
        while (more > 0) {
            --more;
            cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 63);
            ++i;
        }
        if (cp < 0x10000)
            emit(out, cp);
        else {
            cp -= 0x10000;
            emit(out, 0xd800 + (cp >> 10));
            emit(out, 0xdc00 + (cp & 1023));
        }
    }
    if (out.size() > maximum_bytes)
        throw std::runtime_error("Encoded output exceeds the 16 MiB file limit.");
    return out;
}
std::string encoding_name(Encoding e) {
    switch (e) {
    case Encoding::utf8:
        return "UTF-8";
    case Encoding::utf8_bom:
        return "UTF-8 BOM";
    case Encoding::utf16_le:
        return "UTF-16 LE BOM";
    case Encoding::utf16_be:
        return "UTF-16 BE BOM";
    }
    return "Unknown";
}
std::optional<std::size_t> find_literal(std::string_view s, std::string_view q, std::size_t start,
                                        bool match_case) {
    if (q.empty() || start > s.size())
        return {};
    std::string_view::const_iterator it = s.end();
    const std::ptrdiff_t first = static_cast<std::ptrdiff_t>(start);
    if (match_case)
        it = std::search(s.begin() + first, s.end(), q.begin(), q.end());
    else
        it = std::search(s.begin() + first, s.end(), q.begin(), q.end(), equal_folded);
    if (it == s.end())
        return {};
    const std::size_t offset = static_cast<std::size_t>(it - s.begin());
    return offset;
}
Replacement replace_all(std::string_view s, std::string_view q, std::string_view r,
                        bool match_case) {
    Replacement result{};
    std::size_t cursor = 0;
    while (const std::optional<std::size_t> found = find_literal(s, q, cursor, match_case)) {
        if (result.text.size() + *found - cursor + r.size() > maximum_bytes)
            throw std::runtime_error("Replacement exceeds the 16 MiB text limit.");
        result.text.append(s.substr(cursor, *found - cursor));
        result.text += r;
        ++result.count;
        cursor = *found + q.size();
    }
    result.text.append(s.substr(cursor));
    check_text(result.text);
    return result;
}
void Document::open(const std::filesystem::path &source) {
    FileSnapshot observed = read_file(source);
    if (!observed.exists)
        throw std::runtime_error("The selected file no longer exists.");
    Decoded decoded = decode(observed.bytes);
    std::filesystem::path prepared_path = source;
    std::string prepared_opened = decoded.text;
    path = std::move(prepared_path);
    saved_text = std::move(decoded.text);
    opened_text = std::move(prepared_opened);
    encoding = decoded.encoding;
    snapshot = std::move(observed);
}
void Document::save(const std::filesystem::path &target, std::string_view text,
                    const FileSnapshot &expected) {
    save_encoded(target, text, expected, encoding);
}
void Document::save_encoded(const std::filesystem::path &target, std::string_view text,
                            const FileSnapshot &expected, Encoding requested) {
    const std::string bytes = encode(text, requested);
    std::filesystem::path prepared_path = target;
    std::string prepared_text(text);
    FileSnapshot written = write_file(target, bytes, expected);
    path = std::move(prepared_path);
    saved_text = std::move(prepared_text);
    snapshot = std::move(written);
    encoding = requested;
}
} // namespace notepad
