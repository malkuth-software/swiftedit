#include "markdown.hpp"
#include "session.hpp"
#include <gui_forms/text.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <optional>
#include <stdexcept>
#include <md4c.h>
extern "C" {
#include <entity.h>
}

namespace swiftedit {
namespace {
void check_cancelled(const std::stop_token &cancellation) {
    if (cancellation.stop_requested())
        throw MarkdownCancelled();
}
void append_scalar(std::string &text, unsigned scalar) {
    if (!scalar || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        scalar = 0xfffd;
    if (scalar < 0x80)
        text += static_cast<char>(scalar);
    else if (scalar < 0x800) {
        text += static_cast<char>(0xc0 | (scalar >> 6));
        text += static_cast<char>(0x80 | (scalar & 63));
    } else if (scalar < 0x10000) {
        text += static_cast<char>(0xe0 | (scalar >> 12));
        text += static_cast<char>(0x80 | ((scalar >> 6) & 63));
        text += static_cast<char>(0x80 | (scalar & 63));
    } else {
        text += static_cast<char>(0xf0 | (scalar >> 18));
        text += static_cast<char>(0x80 | ((scalar >> 12) & 63));
        text += static_cast<char>(0x80 | ((scalar >> 6) & 63));
        text += static_cast<char>(0x80 | (scalar & 63));
    }
}
std::string entity_text(std::string_view source) {
    std::string text{};
    if (source.starts_with("&#") && source.ends_with(';')) {
        std::string_view digits = source.substr(2, source.size() - 3);
        int radix = 10;
        if (digits.starts_with('x') || digits.starts_with('X')) {
            digits.remove_prefix(1);
            radix = 16;
        }
        unsigned scalar = 0;
        const std::from_chars_result parsed =
            std::from_chars(digits.data(), digits.data() + digits.size(), scalar, radix);
        if (parsed.ec == std::errc() && parsed.ptr == digits.data() + digits.size())
            append_scalar(text, scalar);
        else
            text = source;
    } else {
        const ENTITY *entity = entity_lookup(source.data(), source.size());
        if (entity) {
            append_scalar(text, (*entity).codepoints[0]);
            if ((*entity).codepoints[1])
                append_scalar(text, (*entity).codepoints[1]);
        } else
            text = source;
    }
    return text;
}
struct ListState {
    bool ordered{};
    unsigned next{1};
};
// A character of literal source text that may be dialect markup: an
// unescaped [ ] { } outside code, or a soft line break ('\n'), located by its
// span within the current block and its byte offset in that span's text.
struct Mark {
    std::size_t span{}, offset{};
    char character{};
};
// The attributes one [text]{...} span gives its content (stx_rtf2md README).
struct SpanAttributes {
    bool underline{}, superscript{}, subscript{}, bold{}, italic{}, strike{};
    std::optional<MarkdownColour> colour{}, background{};
    int size_step{};
};
struct SpanPair {
    std::size_t open{}, close{}, end{};
    SpanAttributes attributes{};
};
struct OuterFirst {
    bool operator()(const SpanPair &left, const SpanPair &right) const {
        if (left.open != right.open)
            return left.open < right.open;
        return left.end > right.end;
    }
};
// The style each byte of a block takes from the spans around it.
struct ByteStyle {
    bool removed{}, underline{}, superscript{}, subscript{}, bold{}, italic{}, strike{};
    std::optional<MarkdownColour> colour{}, background{};
    int size_step{};
    bool operator==(const ByteStyle &other) const {
        const bool equal = removed == other.removed && underline == other.underline &&
                           superscript == other.superscript && subscript == other.subscript &&
                           bold == other.bold && italic == other.italic && strike == other.strike &&
                           colour == other.colour && background == other.background &&
                           size_step == other.size_step;
        return equal;
    }
};
unsigned hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<unsigned>(c - 'a') + 10;
    if (c >= 'A' && c <= 'F')
        return static_cast<unsigned>(c - 'A') + 10;
    return 16;
}
bool parse_colour(std::string_view text, MarkdownColour &colour) {
    if (text.size() != 7 || text[0] != '#')
        return false;
    std::array<unsigned, 6> digits{};
    for (std::size_t index = 0; index < 6; ++index) {
        digits[index] = hex_digit(text[index + 1]);
        if (digits[index] > 15)
            return false;
    }
    colour.red = static_cast<std::uint8_t>(digits[0] * 16 + digits[1]);
    colour.green = static_cast<std::uint8_t>(digits[2] * 16 + digits[3]);
    colour.blue = static_cast<std::uint8_t>(digits[4] * 16 + digits[5]);
    return true;
}
// The attribute list grammar of stx_rtf2md: items separated by spaces, each
// at most once, superscript and subscript not together.
bool parse_attributes(std::string_view list, SpanAttributes &attributes) {
    bool seen_underline = false, seen_position = false, seen_colour = false, seen_background = false;
    bool seen_size = false, seen_bold = false, seen_italic = false, seen_strike = false, any = false;
    std::size_t start = 0;
    while (start < list.size()) {
        if (list[start] == ' ') {
            ++start;
            continue;
        }
        std::size_t end = list.find(' ', start);
        if (end == std::string_view::npos)
            end = list.size();
        const std::string_view item = list.substr(start, end - start);
        start = end;
        any = true;
        if (item == "u" && !seen_underline) {
            seen_underline = true;
            attributes.underline = true;
        } else if ((item == "sup" || item == "sub") && !seen_position) {
            seen_position = true;
            attributes.superscript = item == "sup";
            attributes.subscript = item == "sub";
        } else if (item == "b" && !seen_bold) {
            seen_bold = true;
            attributes.bold = true;
        } else if (item == "i" && !seen_italic) {
            seen_italic = true;
            attributes.italic = true;
        } else if (item == "s" && !seen_strike) {
            seen_strike = true;
            attributes.strike = true;
        } else if (item.starts_with("color=") && !seen_colour) {
            MarkdownColour colour{};
            if (!parse_colour(item.substr(6), colour))
                return false;
            seen_colour = true;
            attributes.colour = colour;
        } else if (item.starts_with("bg=") && !seen_background) {
            MarkdownColour colour{};
            if (!parse_colour(item.substr(3), colour))
                return false;
            seen_background = true;
            attributes.background = colour;
        } else if (item.size() == 7 && item.starts_with("size=") && !seen_size) {
            const char sign = item[5], digit = item[6];
            const bool raise = sign == '+' && digit >= '1' && digit <= '4';
            const bool lower = sign == '-' && digit >= '1' && digit <= '2';
            if (!raise && !lower)
                return false;
            seen_size = true;
            attributes.size_step = raise ? digit - '0' : -(digit - '0');
        } else
            return false;
    }
    return any;
}
// MD4C owns the attribute slices for the duration of its callback. Copy the
// decoded destination into our model; never retain the borrowed slice pointers.
std::string attribute_text(const MD_ATTRIBUTE &attribute, const std::stop_token &cancellation) {
    std::string result{};
    std::size_t index = 0;
    MD_OFFSET offset = 0;
    while (offset < attribute.size) {
        check_cancelled(cancellation);
        const MD_OFFSET end = attribute.substr_offsets[index + 1];
        const std::string_view part(attribute.text + offset, end - offset);
        if (attribute.substr_types[index] == MD_TEXT_ENTITY) {
            const std::string decoded = entity_text(part);
            result += decoded;
        } else if (attribute.substr_types[index] == MD_TEXT_NULLCHAR)
            append_scalar(result, 0xfffd);
        else
            result += part;
        offset = end;
        ++index;
    }
    return result;
}
struct MarkdownParser {
    std::stop_token cancellation{};
    std::vector<MarkdownBlock> blocks{};
    std::optional<std::size_t> current{};
    std::vector<ListState> lists{};
    std::vector<std::string> links{};
    std::string prefix{};
    std::size_t quotes{}, strong{}, emphasis{}, code{}, strike{}, column{}, columns{1};
    std::size_t span_count{}, output_bytes{};
    std::exception_ptr failure{};
    // The source, for telling escaped markup characters from real ones.
    std::string_view source{};
    std::vector<Mark> marks{};
    // An attribute-only paragraph's alignment, for the next block.
    std::optional<MarkdownAlignment> pending_alignment{};

    void begin(MarkdownKind kind, std::size_t level = 0) {
        finish_current();
        if (blocks.size() >= 100000)
            throw std::runtime_error("Markdown exceeds 100000 display blocks.");
        MarkdownBlock block{};
        block.kind = kind;
        block.level = level;
        block.indent = lists.size();
        block.quoted = quotes != 0;
        block.columns = columns;
        blocks.push_back(std::move(block));
        current = blocks.size() - 1;
        if (!prefix.empty()) {
            std::string label = std::move(prefix);
            prefix.clear();
            append(label);
        }
    }
    void append(std::string_view text) {
        check_cancelled(cancellation);
        if (text.empty())
            return;
        if (!current)
            begin(MarkdownKind::paragraph);
        constexpr std::size_t storage_limit = 32 * 1024 * 1024;
        const std::size_t url_bytes = links.empty() ? 0 : links.back().size();
        if (span_count >= 250000 || text.size() > storage_limit - output_bytes)
            throw std::runtime_error("Markdown exceeds display storage budget.");
        const std::size_t text_total = output_bytes + text.size();
        if (url_bytes > storage_limit - text_total)
            throw std::runtime_error("Markdown exceeds display storage budget.");
        MarkdownSpan span{};
        span.text = text;
        span.bold = strong != 0;
        span.italic = emphasis != 0;
        span.code = code != 0;
        span.strike = strike != 0;
        span.column = column;
        if (!links.empty())
            span.url = links.back();
        blocks[*current].spans.push_back(std::move(span));
        ++span_count;
        output_bytes = text_total + url_bytes;
    }
    // Literal source text: records its markup characters for finish_current.
    void append_source(std::string_view text) {
        append(text);
        if (text.empty() || code != 0 || !current)
            return;
        const char *begin = source.data();
        const char *end = begin + source.size();
        if (text.data() < begin || text.data() >= end)
            return;
        const std::size_t span = blocks[*current].spans.size() - 1;
        const std::size_t base = static_cast<std::size_t>(text.data() - begin);
        for (std::size_t index = 0; index < text.size(); ++index) {
            const char character = text[index];
            if (character != '[' && character != ']' && character != '{' && character != '}')
                continue;
            std::size_t backslashes = 0;
            while (backslashes < base + index && source[base + index - backslashes - 1] == '\\')
                ++backslashes;
            if (backslashes % 2 == 0)
                marks.push_back({span, index, character});
        }
    }
    void soft_break() {
        append(" ");
        if (current)
            marks.push_back({blocks[*current].spans.size() - 1, 0, '\n'});
    }
    // Applies the dialect to the block that is ending: a leading block
    // attribute line sets its alignment (or, alone, the next block's), and
    // each [text]{attributes} span styles its text and loses its markup.
    void finish_current() {
        if (!current)
            return;
        MarkdownBlock &block = blocks[*current];
        if (pending_alignment) {
            if (block.kind == MarkdownKind::paragraph || block.kind == MarkdownKind::heading)
                block.alignment = *pending_alignment;
            pending_alignment.reset();
        }
        if (marks.empty())
            return;
        std::vector<std::size_t> starts(block.spans.size() + 1, 0);
        std::string flat{};
        for (std::size_t index = 0; index < block.spans.size(); ++index) {
            starts[index] = flat.size();
            flat += block.spans[index].text;
        }
        starts[block.spans.size()] = flat.size();
        std::vector<std::size_t> positions(marks.size());
        for (std::size_t index = 0; index < marks.size(); ++index)
            positions[index] = starts[marks[index].span] + marks[index].offset;
        std::vector<ByteStyle> styles(flat.size());
        bool changed = false;
        std::size_t first_mark = 0;
        if (marks.size() >= 2 && marks[0].character == '{' && positions[0] == 0 &&
            marks[1].character == '}') {
            const std::string_view line(flat.data(), positions[1] + 1);
            std::optional<MarkdownAlignment> alignment{};
            if (line == "{align=left}")
                alignment = MarkdownAlignment::left;
            else if (line == "{align=center}")
                alignment = MarkdownAlignment::center;
            else if (line == "{align=right}")
                alignment = MarkdownAlignment::right;
            const bool alone = positions[1] + 1 == flat.size();
            const bool line_ends = marks.size() >= 3 && marks[2].character == '\n' &&
                                   positions[2] == positions[1] + 1;
            if (alignment && alone) {
                blocks.pop_back();
                current.reset();
                marks.clear();
                pending_alignment = alignment;
                return;
            }
            if (alignment && line_ends) {
                block.alignment = *alignment;
                for (std::size_t index = 0; index <= positions[2]; ++index)
                    styles[index].removed = true;
                changed = true;
                first_mark = 3;
            }
        }
        std::vector<std::size_t> opens{};
        std::vector<SpanPair> pairs{};
        for (std::size_t index = first_mark; index < marks.size(); ++index) {
            const char character = marks[index].character;
            if (character == '[') {
                opens.push_back(positions[index]);
                continue;
            }
            if (character != ']' || opens.empty())
                continue;
            const std::size_t open = opens.back();
            opens.pop_back();
            const bool brace = index + 2 < marks.size() && marks[index + 1].character == '{' &&
                               positions[index + 1] == positions[index] + 1 &&
                               marks[index + 2].character == '}';
            if (!brace || open + 1 == positions[index])
                continue;
            const std::size_t list_start = positions[index] + 2;
            const std::string_view list(flat.data() + list_start, positions[index + 2] - list_start);
            SpanAttributes attributes{};
            if (!parse_attributes(list, attributes))
                continue;
            pairs.push_back({open, positions[index], positions[index + 2], attributes});
            index += 2;
        }
        std::sort(pairs.begin(), pairs.end(), OuterFirst{});
        for (const SpanPair &pair : pairs) {
            styles[pair.open].removed = true;
            for (std::size_t index = pair.close; index <= pair.end; ++index)
                styles[index].removed = true;
            for (std::size_t index = pair.open + 1; index < pair.close; ++index) {
                ByteStyle &style = styles[index];
                const SpanAttributes &own = pair.attributes;
                style.underline = style.underline || own.underline;
                style.bold = style.bold || own.bold;
                style.italic = style.italic || own.italic;
                style.strike = style.strike || own.strike;
                if (own.superscript || own.subscript) {
                    style.superscript = own.superscript;
                    style.subscript = own.subscript;
                }
                if (own.colour)
                    style.colour = own.colour;
                if (own.background)
                    style.background = own.background;
                if (own.size_step != 0)
                    style.size_step = own.size_step;
            }
            changed = true;
        }
        marks.clear();
        if (!changed)
            return;
        std::vector<MarkdownSpan> rebuilt{};
        for (std::size_t index = 0; index < block.spans.size(); ++index) {
            const MarkdownSpan &original = block.spans[index];
            std::optional<ByteStyle> open_style{};
            for (std::size_t byte = starts[index]; byte < starts[index + 1]; ++byte) {
                const ByteStyle &style = styles[byte];
                if (style.removed) {
                    open_style.reset();
                    continue;
                }
                if (!open_style || !(*open_style == style)) {
                    MarkdownSpan span = original;
                    span.text.clear();
                    span.bold = original.bold || style.bold;
                    span.italic = original.italic || style.italic;
                    span.strike = original.strike || style.strike;
                    span.underline = style.underline;
                    span.superscript = style.superscript;
                    span.subscript = style.subscript;
                    span.colour = style.colour;
                    span.background = style.background;
                    span.size_step = style.size_step;
                    rebuilt.push_back(std::move(span));
                    open_style = style;
                }
                rebuilt.back().text.push_back(flat[byte]);
            }
        }
        block.spans = std::move(rebuilt);
    }
    void enter_block(MD_BLOCKTYPE type, void *detail) {
        switch (type) {
        case MD_BLOCK_QUOTE:
            ++quotes;
            break;
        case MD_BLOCK_UL:
            lists.push_back({false, 1});
            break;
        case MD_BLOCK_OL: {
            const MD_BLOCK_OL_DETAIL &list = *static_cast<MD_BLOCK_OL_DETAIL *>(detail);
            lists.push_back({true, list.start});
            break;
        }
        case MD_BLOCK_LI: {
            finish_current();
            current.reset();
            const MD_BLOCK_LI_DETAIL &item = *static_cast<MD_BLOCK_LI_DETAIL *>(detail);
            if (item.is_task)
                prefix = item.task_mark == ' ' ? "[ ] " : "[x] ";
            else if (!lists.empty() && lists.back().ordered) {
                prefix = std::to_string(lists.back().next) + ". ";
                ++lists.back().next;
            } else
                prefix = "\xe2\x80\xa2 ";
            break;
        }
        case MD_BLOCK_H: {
            const MD_BLOCK_H_DETAIL &heading = *static_cast<MD_BLOCK_H_DETAIL *>(detail);
            begin(MarkdownKind::heading, heading.level);
            break;
        }
        case MD_BLOCK_P:
            begin(MarkdownKind::paragraph);
            break;
        case MD_BLOCK_CODE:
            begin(MarkdownKind::code);
            break;
        case MD_BLOCK_HR:
            begin(MarkdownKind::rule);
            break;
        case MD_BLOCK_TABLE: {
            const MD_BLOCK_TABLE_DETAIL &table = *static_cast<MD_BLOCK_TABLE_DETAIL *>(detail);
            columns = table.col_count;
            break;
        }
        case MD_BLOCK_TR:
            column = 0;
            begin(MarkdownKind::table_row);
            break;
        case MD_BLOCK_TH:
            if (current)
                blocks[*current].header = true;
            break;
        default:
            break;
        }
    }
    void leave_block(MD_BLOCKTYPE type) {
        if (type == MD_BLOCK_QUOTE)
            --quotes;
        else if (type == MD_BLOCK_UL || type == MD_BLOCK_OL)
            lists.pop_back();
        else if (type == MD_BLOCK_TD || type == MD_BLOCK_TH)
            ++column;
        else if (type == MD_BLOCK_TABLE) {
            columns = 1;
            column = 0;
        } else if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE ||
                   type == MD_BLOCK_HR || type == MD_BLOCK_LI || type == MD_BLOCK_TR) {
            finish_current();
            current.reset();
        }
    }
    void enter_span(MD_SPANTYPE type, void *detail) {
        if (type == MD_SPAN_STRONG)
            ++strong;
        else if (type == MD_SPAN_EM)
            ++emphasis;
        else if (type == MD_SPAN_CODE)
            ++code;
        else if (type == MD_SPAN_DEL)
            ++strike;
        else if (type == MD_SPAN_A) {
            const MD_SPAN_A_DETAIL &link = *static_cast<MD_SPAN_A_DETAIL *>(detail);
            std::string destination = attribute_text(link.href, cancellation);
            links.push_back(std::move(destination));
        } else if (type == MD_SPAN_IMG)
            append("[Image: ");
    }
    void leave_span(MD_SPANTYPE type) {
        if (type == MD_SPAN_STRONG)
            --strong;
        else if (type == MD_SPAN_EM)
            --emphasis;
        else if (type == MD_SPAN_CODE)
            --code;
        else if (type == MD_SPAN_DEL)
            --strike;
        else if (type == MD_SPAN_A)
            links.pop_back();
        else if (type == MD_SPAN_IMG)
            append("]");
    }
    static int block_start(MD_BLOCKTYPE type, void *detail, void *context) noexcept {
        MarkdownParser &parser = *static_cast<MarkdownParser *>(context);
        try {
            check_cancelled(parser.cancellation);
            parser.enter_block(type, detail);
            return 0;
        } catch (...) {
            parser.failure = std::current_exception();
            return 1;
        }
    }
    static int block_end(MD_BLOCKTYPE type, void *, void *context) noexcept {
        MarkdownParser &parser = *static_cast<MarkdownParser *>(context);
        try {
            check_cancelled(parser.cancellation);
            parser.leave_block(type);
            return 0;
        } catch (...) {
            parser.failure = std::current_exception();
            return 1;
        }
    }
    static int span_start(MD_SPANTYPE type, void *detail, void *context) noexcept {
        MarkdownParser &parser = *static_cast<MarkdownParser *>(context);
        try {
            check_cancelled(parser.cancellation);
            parser.enter_span(type, detail);
            return 0;
        } catch (...) {
            parser.failure = std::current_exception();
            return 1;
        }
    }
    static int span_end(MD_SPANTYPE type, void *, void *context) noexcept {
        MarkdownParser &parser = *static_cast<MarkdownParser *>(context);
        try {
            check_cancelled(parser.cancellation);
            parser.leave_span(type);
            return 0;
        } catch (...) {
            parser.failure = std::current_exception();
            return 1;
        }
    }
    static int text(MD_TEXTTYPE type, const MD_CHAR *data, MD_SIZE size, void *context) noexcept {
        MarkdownParser &parser = *static_cast<MarkdownParser *>(context);
        try {
            check_cancelled(parser.cancellation);
            if (type == MD_TEXT_SOFTBR)
                parser.soft_break();
            else if (type == MD_TEXT_BR)
                parser.append("\n");
            else if (type == MD_TEXT_NULLCHAR)
                parser.append("\xef\xbf\xbd");
            else if (type == MD_TEXT_ENTITY) {
                const std::string decoded = entity_text(std::string_view(data, size));
                parser.append(decoded);
            } else if (type == MD_TEXT_NORMAL)
                parser.append_source(std::string_view(data, size));
            else
                parser.append(std::string_view(data, size));
            return 0;
        } catch (...) {
            parser.failure = std::current_exception();
            return 1;
        }
    }
};
} // namespace
std::vector<MarkdownBlock> parse_markdown(std::string_view source, std::stop_token cancellation) {
    check_cancelled(cancellation);
    if (source.size() >= editable_limit)
        throw std::runtime_error("Markdown rendering requires an editable-size document.");
    const gui_forms::Utf8ValidationResult valid = gui_forms::validate_utf8(source);
    check_cancelled(cancellation);
    if (!valid.valid())
        throw std::runtime_error("Markdown rendering requires valid UTF-8 source.");
    MarkdownParser state{};
    state.cancellation = cancellation;
    state.source = source;
    MD_PARSER parser{};
    parser.flags = MD_FLAG_TABLES | MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH | MD_FLAG_NOHTML;
    parser.enter_block = MarkdownParser::block_start;
    parser.leave_block = MarkdownParser::block_end;
    parser.enter_span = MarkdownParser::span_start;
    parser.leave_span = MarkdownParser::span_end;
    parser.text = MarkdownParser::text;
    const int result =
        md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &state);
    check_cancelled(cancellation);
    if (state.failure)
        std::rethrow_exception(state.failure);
    if (result)
        throw std::runtime_error("Markdown parsing failed.");
    state.finish_current();
    std::vector<MarkdownBlock> blocks = std::move(state.blocks);
    return blocks;
}
} // namespace swiftedit
