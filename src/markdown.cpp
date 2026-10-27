#include "markdown.hpp"
#include "session.hpp"
#include <gui_forms/text.hpp>
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

    void begin(MarkdownKind kind, std::size_t level = 0) {
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
                   type == MD_BLOCK_HR || type == MD_BLOCK_LI || type == MD_BLOCK_TR)
            current.reset();
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
                parser.append(" ");
            else if (type == MD_TEXT_BR)
                parser.append("\n");
            else if (type == MD_TEXT_NULLCHAR)
                parser.append("\xef\xbf\xbd");
            else if (type == MD_TEXT_ENTITY) {
                const std::string decoded = entity_text(std::string_view(data, size));
                parser.append(decoded);
            } else
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
    std::vector<MarkdownBlock> blocks = std::move(state.blocks);
    return blocks;
}
} // namespace swiftedit
