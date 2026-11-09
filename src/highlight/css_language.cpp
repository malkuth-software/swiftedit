#include "css_language.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace swiftedit::highlight {
namespace {

enum class Context : std::uint8_t {
    statement_start = 0U,  // nothing of the current statement read yet
    selector = 1U,         // a rule's selector, or a form object's header
    at_prelude = 2U,       // after an at-keyword, before its block or `;`
    property = 3U,         // a declaration's name, before its colon
    value = 4U,            // a declaration's value, before `;` or `}`
};

enum class Quote : std::uint8_t { none = 0U, double_quote = 1U, single_quote = 2U };

// The decoded form of a LexState. Bit layout of LexState::value:
//   0-5   depth (saturating at 63)
//   6-8   context
//   9     inside a comment
//   10-11 inside a string continued by a backslash, and its quote
//   12-15 parenthesis depth (saturating at 15)
//   16    the at-rule being read opens a block of rules (@media, @keyframes)
//   17    a form object's header has its type; the next name is its stable ID
//   18    the selector parentheses being read hold names (::part(face))
//   32-63 bit d set: the block at depth d + 1 holds rules, not declarations
struct CssState final {
    std::uint32_t depth{};
    Context context{Context::statement_start};
    bool in_comment{};
    Quote quote{Quote::none};
    std::uint32_t parentheses{};
    bool at_block_holds_rules{};
    bool header_named{};
    bool argument_names{};
    std::uint32_t rule_blocks{};
};

constexpr std::uint32_t maximum_depth = 63U;
constexpr std::uint32_t maximum_parentheses = 15U;

CssState decode(LexState state) noexcept {
    const std::uint64_t bits = state.value;
    CssState result{};
    result.depth = static_cast<std::uint32_t>(bits & 0x3FU);
    result.context = static_cast<Context>((bits >> 6U) & 0x7U);
    result.in_comment = ((bits >> 9U) & 0x1U) != 0U;
    result.quote = static_cast<Quote>((bits >> 10U) & 0x3U);
    result.parentheses = static_cast<std::uint32_t>((bits >> 12U) & 0xFU);
    result.at_block_holds_rules = ((bits >> 16U) & 0x1U) != 0U;
    result.header_named = ((bits >> 17U) & 0x1U) != 0U;
    result.argument_names = ((bits >> 18U) & 0x1U) != 0U;
    result.rule_blocks = static_cast<std::uint32_t>(bits >> 32U);
    return result;
}

LexState encode(const CssState &state) noexcept {
    std::uint64_t bits = static_cast<std::uint64_t>(state.depth & 0x3FU);
    bits |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(state.context) & 0x7U) << 6U;
    bits |= static_cast<std::uint64_t>(state.in_comment ? 1U : 0U) << 9U;
    bits |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(state.quote) & 0x3U) << 10U;
    bits |= static_cast<std::uint64_t>(state.parentheses & 0xFU) << 12U;
    bits |= static_cast<std::uint64_t>(state.at_block_holds_rules ? 1U : 0U) << 16U;
    bits |= static_cast<std::uint64_t>(state.header_named ? 1U : 0U) << 17U;
    bits |= static_cast<std::uint64_t>(state.argument_names ? 1U : 0U) << 18U;
    bits |= static_cast<std::uint64_t>(state.rule_blocks) << 32U;
    const LexState result{bits};
    return result;
}

bool is_space(char character) noexcept {
    const bool result = character == ' ' || character == '\t' || character == '\f';
    return result;
}

bool is_digit(char character) noexcept {
    const bool result = character >= '0' && character <= '9';
    return result;
}

bool is_hex_digit(char character) noexcept {
    const bool result = is_digit(character) || (character >= 'a' && character <= 'f') ||
                        (character >= 'A' && character <= 'F');
    return result;
}

// A name starts with a letter, an underscore, a non-ASCII byte or an escape.
bool is_name_start(char character) noexcept {
    const unsigned char byte = static_cast<unsigned char>(character);
    const bool result = (character >= 'a' && character <= 'z') ||
                        (character >= 'A' && character <= 'Z') || character == '_' ||
                        character == '\\' || byte >= 0x80U;
    return result;
}

bool is_name_character(char character) noexcept {
    const bool result = is_name_start(character) || is_digit(character) || character == '-';
    return result;
}

char ascii_lower(char character) noexcept {
    const char result =
        character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
    return result;
}

bool equal_ignoring_ascii_case(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index]))
            return false;
    }
    return true;
}

// Value words that read as keywords rather than as plain identifiers: the
// role pair's `on`, CSS's global words, and the form binding options.
constexpr std::array<std::string_view, 14U> value_keywords{
    "on",    "none",   "inherit", "initial", "unset",  "auto",     "transparent",
    "true",  "false",  "normal",  "one_way", "two_way", "update", "on-change"};

// At-rules whose block holds rules rather than declarations.
constexpr std::array<std::string_view, 6U> rule_list_at_rules{
    "media", "supports", "layer", "keyframes", "document", "container"};

// Selector functions whose arguments are names, not selectors.
constexpr std::array<std::string_view, 4U> name_argument_functions{"part", "role", "family",
                                                                   "state"};

template <std::size_t Count>
bool listed_ignoring_case(const std::array<std::string_view, Count> &list,
                          std::string_view word) noexcept {
    for (const std::string_view entry : list) {
        if (equal_ignoring_ascii_case(entry, word))
            return true;
    }
    return false;
}

// Lexes one line. Owns nothing: borrows the line and the output for the call.
class LineLexer final {
public:
    LineLexer(CssDialect dialect, std::string_view line, CssState state,
              std::vector<LineToken> &tokens) noexcept
        : dialect_(dialect), line_(line), state_(state), tokens_(tokens) {}

    CssState run() {
        if (state_.in_comment && !continue_comment())
            return state_;
        if (state_.quote != Quote::none && !continue_string())
            return state_;
        while (index_ < line_.size()) {
            const char character = line_[index_];
            if (is_space(character)) {
                ++index_;
                continue;
            }
            if (character == '/' && peek(1U) == '*') {
                if (!comment())
                    return state_;
                continue;
            }
            if (character == '"' || character == '\'') {
                if (!quoted(index_, character))
                    return state_;
                continue;
            }
            if (character == '{') {
                emit(index_, index_ + 1U, TokenKind::punctuation);
                ++index_;
                open_block();
                continue;
            }
            if (character == '}') {
                close_block();
                continue;
            }
            if (character == ';') {
                emit(index_, index_ + 1U, TokenKind::punctuation);
                ++index_;
                end_statement();
                continue;
            }
            switch (state_.context) {
            case Context::statement_start:
                begin_statement();
                break;
            case Context::selector:
                selector_token();
                break;
            case Context::at_prelude:
                value_token(true);
                break;
            case Context::property:
                property_token();
                break;
            case Context::value:
                value_token(false);
                break;
            }
        }
        return state_;
    }

private:
    char peek(std::size_t ahead) const noexcept {
        const std::size_t position = index_ + ahead;
        const char result = position < line_.size() ? line_[position] : '\0';
        return result;
    }

    void emit(std::size_t start, std::size_t end, TokenKind kind) {
        if (end <= start)
            return;
        const LineToken token{static_cast<std::uint32_t>(start),
                              static_cast<std::uint32_t>(end - start), kind};
        tokens_.push_back(token);
    }

    // The end of the name starting at `start`; escapes take the byte after
    // the backslash. With `dotted`, dots continue the name (a form document's
    // stable IDs, as example.greeting).
    std::size_t name_end(std::size_t start, bool dotted) const noexcept {
        std::size_t position = start;
        while (position < line_.size()) {
            const char character = line_[position];
            if (character == '\\') {
                position = std::min(position + 2U, line_.size());
                continue;
            }
            const bool continues = is_name_character(character) || (dotted && character == '.');
            if (!continues)
                break;
            ++position;
        }
        return position;
    }

    bool identifier_starts_at(std::size_t position) const noexcept {
        if (position >= line_.size())
            return false;
        const char character = line_[position];
        if (is_name_start(character))
            return true;
        if (character != '-' || position + 1U >= line_.size())
            return false;
        const char next = line_[position + 1U];
        const bool result = next == '-' || is_name_start(next);
        return result;
    }

    bool number_starts_at(std::size_t position) const noexcept {
        if (position >= line_.size())
            return false;
        const char character = line_[position];
        if (is_digit(character))
            return true;
        const char next = position + 1U < line_.size() ? line_[position + 1U] : '\0';
        if (character == '.')
            return is_digit(next);
        if (character == '+' || character == '-') {
            const char after = position + 2U < line_.size() ? line_[position + 2U] : '\0';
            const bool result = is_digit(next) || (next == '.' && is_digit(after));
            return result;
        }
        return false;
    }

    // Returns false when the comment runs past the line.
    bool comment() {
        const std::size_t start = index_;
        const std::size_t close = line_.find("*/", index_ + 2U);
        if (close == std::string_view::npos) {
            emit(start, line_.size(), TokenKind::comment);
            index_ = line_.size();
            state_.in_comment = true;
            return false;
        }
        emit(start, close + 2U, TokenKind::comment);
        index_ = close + 2U;
        return true;
    }

    bool continue_comment() {
        const std::size_t close = line_.find("*/");
        if (close == std::string_view::npos) {
            emit(0U, line_.size(), TokenKind::comment);
            index_ = line_.size();
            return false;
        }
        emit(0U, close + 2U, TokenKind::comment);
        index_ = close + 2U;
        state_.in_comment = false;
        return true;
    }

    // Scans a string body from `body` to its closing quote. Returns false
    // when a final backslash continues the string onto the next line.
    bool string_body(std::size_t start, std::size_t body, char quote) {
        std::size_t position = body;
        while (position < line_.size()) {
            const char character = line_[position];
            if (character == '\\') {
                if (position + 1U == line_.size()) {
                    emit(start, line_.size(), TokenKind::string);
                    index_ = line_.size();
                    state_.quote = quote == '"' ? Quote::double_quote : Quote::single_quote;
                    return false;
                }
                position += 2U;
                continue;
            }
            if (character == quote) {
                emit(start, position + 1U, TokenKind::string);
                index_ = position + 1U;
                state_.quote = Quote::none;
                return true;
            }
            ++position;
        }
        // CSS ends an unescaped string at the line's end as a bad string.
        emit(start, line_.size(), TokenKind::invalid);
        index_ = line_.size();
        state_.quote = Quote::none;
        return true;
    }

    bool quoted(std::size_t start, char quote) { return string_body(start, start + 1U, quote); }

    bool continue_string() {
        const char quote = state_.quote == Quote::double_quote ? '"' : '\'';
        return string_body(0U, 0U, quote);
    }

    bool block_holds_rules(std::uint32_t depth) const noexcept {
        if (depth == 0U) {
            const bool result = dialect_ != CssDialect::inline_style;
            return result;
        }
        if (depth > CssLanguage::recorded_block_depth)
            return false;
        const bool result = ((state_.rule_blocks >> (depth - 1U)) & 1U) != 0U;
        return result;
    }

    // Whether a `{` comes before any `;` or `}` on the rest of the line,
    // skipping strings and comments.
    bool brace_before_statement_end() const noexcept {
        std::size_t position = index_;
        while (position < line_.size()) {
            const char character = line_[position];
            if (character == '{')
                return true;
            if (character == ';' || character == '}')
                return false;
            if (character == '"' || character == '\'') {
                std::size_t close = position + 1U;
                while (close < line_.size() && line_[close] != character)
                    close += line_[close] == '\\' ? 2U : 1U;
                position = close + 1U;
                continue;
            }
            if (character == '/' && position + 1U < line_.size() && line_[position + 1U] == '*') {
                const std::size_t close = line_.find("*/", position + 2U);
                if (close == std::string_view::npos)
                    return false;
                position = close + 2U;
                continue;
            }
            ++position;
        }
        return false;
    }

    void begin_statement() {
        if (line_[index_] == '@') {
            const std::size_t end = name_end(index_ + 1U, false);
            emit(index_, end, TokenKind::at_rule);
            const std::string_view keyword = line_.substr(index_ + 1U, end - index_ - 1U);
            state_.at_block_holds_rules = listed_ignoring_case(rule_list_at_rules, keyword);
            state_.context = Context::at_prelude;
            index_ = std::max(end, index_ + 1U);
            return;
        }
        const bool rules = block_holds_rules(state_.depth);
        const bool nested = !rules && brace_before_statement_end();
        state_.context = rules || nested ? Context::selector : Context::property;
        state_.header_named = false;
    }

    void end_statement() noexcept {
        state_.context = Context::statement_start;
        state_.parentheses = 0U;
        state_.at_block_holds_rules = false;
        state_.header_named = false;
        state_.argument_names = false;
    }

    void open_block() noexcept {
        const bool holds_rules =
            state_.context == Context::at_prelude && state_.at_block_holds_rules;
        if (state_.depth < CssLanguage::recorded_block_depth) {
            const std::uint32_t bit = 1U << state_.depth;
            state_.rule_blocks = holds_rules ? (state_.rule_blocks | bit) : (state_.rule_blocks & ~bit);
        }
        if (state_.depth < maximum_depth)
            ++state_.depth;
        end_statement();
    }

    void close_block() {
        if (state_.depth == 0U) {
            emit(index_, index_ + 1U, TokenKind::invalid);
        } else {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            --state_.depth;
        }
        ++index_;
        end_statement();
    }

    void open_parenthesis() {
        emit(index_, index_ + 1U, TokenKind::punctuation);
        ++index_;
        if (state_.parentheses < maximum_parentheses)
            ++state_.parentheses;
    }

    void close_parenthesis() {
        if (state_.parentheses == 0U) {
            emit(index_, index_ + 1U, TokenKind::invalid);
        } else {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            --state_.parentheses;
        }
        ++index_;
        if (state_.parentheses == 0U)
            state_.argument_names = false;
    }

    std::size_t number_end(std::size_t start) const noexcept {
        std::size_t position = start;
        if (line_[position] == '+' || line_[position] == '-')
            ++position;
        while (position < line_.size() && is_digit(line_[position]))
            ++position;
        if (position + 1U < line_.size() && line_[position] == '.' && is_digit(line_[position + 1U])) {
            ++position;
            while (position < line_.size() && is_digit(line_[position]))
                ++position;
        }
        if (position < line_.size() && line_[position] == '%')
            return position + 1U;
        if (identifier_starts_at(position))
            return name_end(position + 1U, false);
        return position;
    }

    void selector_token() {
        const char character = line_[index_];
        const bool form = dialect_ == CssDialect::form_document;
        if (character == '#') {
            const std::size_t end = name_end(index_ + 1U, form);
            const TokenKind kind = end > index_ + 1U ? TokenKind::identity_selector : TokenKind::invalid;
            emit(index_, std::max(end, index_ + 1U), kind);
            index_ = std::max(end, index_ + 1U);
            return;
        }
        if (character == '.' && is_name_start(peek(1U))) {
            const std::size_t end = name_end(index_ + 1U, false);
            emit(index_, end, TokenKind::class_selector);
            index_ = end;
            return;
        }
        if (character == ':') {
            const std::size_t name_start = peek(1U) == ':' ? index_ + 2U : index_ + 1U;
            const std::size_t end = name_end(name_start, false);
            emit(index_, end, end > name_start ? TokenKind::pseudo_selector : TokenKind::invalid);
            const std::string_view name = line_.substr(name_start, end - name_start);
            index_ = std::max(end, name_start);
            if (index_ < line_.size() && line_[index_] == '(') {
                state_.argument_names = listed_ignoring_case(name_argument_functions, name);
                open_parenthesis();
            }
            return;
        }
        if (character == '(') {
            open_parenthesis();
            return;
        }
        if (character == ')') {
            close_parenthesis();
            return;
        }
        if (character == ',') {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            ++index_;
            return;
        }
        if (character == '>' || character == '+' || character == '~' || character == '*') {
            emit(index_, index_ + 1U, TokenKind::operator_symbol);
            ++index_;
            return;
        }
        if (is_digit(character)) {
            const std::size_t end = number_end(index_);
            emit(index_, end, TokenKind::number);
            index_ = end;
            return;
        }
        if (identifier_starts_at(index_)) {
            if (state_.parentheses > 0U && state_.argument_names) {
                const std::size_t end = name_end(index_ + 1U, false);
                emit(index_, end, TokenKind::value_identifier);
                index_ = end;
                return;
            }
            if (form && state_.header_named) {
                const std::size_t end = name_end(index_ + 1U, true);
                emit(index_, end, TokenKind::identity_selector);
                index_ = end;
                return;
            }
            const std::size_t end = name_end(index_ + 1U, false);
            emit(index_, end, TokenKind::type_selector);
            index_ = end;
            state_.header_named = form;
            return;
        }
        emit(index_, index_ + 1U, TokenKind::invalid);
        ++index_;
    }

    void property_token() {
        const char character = line_[index_];
        if (character == ':') {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            ++index_;
            state_.context = Context::value;
            return;
        }
        if (identifier_starts_at(index_)) {
            const std::size_t end = name_end(index_ + 1U, false);
            const bool custom = character == '-' && peek(1U) == '-';
            emit(index_, end, custom ? TokenKind::custom_property : TokenKind::property);
            index_ = end;
            return;
        }
        emit(index_, index_ + 1U, TokenKind::invalid);
        ++index_;
    }

    // The next byte after `position` that is not a space, or '\0'.
    char next_significant(std::size_t position) const noexcept {
        while (position < line_.size() && is_space(line_[position]))
            ++position;
        const char result = position < line_.size() ? line_[position] : '\0';
        return result;
    }

    void value_token(bool prelude) {
        const char character = line_[index_];
        if (character == '#') {
            std::size_t end = index_ + 1U;
            while (end < line_.size() && is_hex_digit(line_[end]))
                ++end;
            const std::size_t digits = end - index_ - 1U;
            const bool sized = digits == 3U || digits == 4U || digits == 6U || digits == 8U;
            const bool closed = end == line_.size() || !is_name_character(line_[end]);
            if (sized && closed) {
                emit(index_, end, TokenKind::color);
            } else {
                end = std::max(name_end(index_ + 1U, false), index_ + 1U);
                emit(index_, end, TokenKind::invalid);
            }
            index_ = end;
            return;
        }
        if (number_starts_at(index_)) {
            const std::size_t end = number_end(index_);
            emit(index_, end, TokenKind::number);
            index_ = end;
            return;
        }
        if (character == '!') {
            std::size_t word = index_ + 1U;
            while (word < line_.size() && is_space(line_[word]))
                ++word;
            const std::size_t end = name_end(word, false);
            const bool important =
                equal_ignoring_ascii_case(line_.substr(word, end - word), "important");
            emit(index_, important ? end : index_ + 1U,
                 important ? TokenKind::important : TokenKind::invalid);
            index_ = important ? end : index_ + 1U;
            return;
        }
        if (identifier_starts_at(index_)) {
            const std::size_t end = name_end(index_ + 1U, false);
            const std::string_view word = line_.substr(index_, end - index_);
            if (end < line_.size() && line_[end] == '(') {
                emit(index_, end, TokenKind::function);
                index_ = end;
                open_parenthesis();
                return;
            }
            TokenKind kind = TokenKind::value_identifier;
            if (word.size() > 2U && word[0] == '-' && word[1] == '-')
                kind = TokenKind::custom_property;
            else if (prelude && next_significant(end) == ':')
                kind = TokenKind::property;
            else if (listed_ignoring_case(value_keywords, word))
                kind = TokenKind::keyword;
            emit(index_, end, kind);
            index_ = end;
            return;
        }
        if (character == '(') {
            open_parenthesis();
            return;
        }
        if (character == ')') {
            close_parenthesis();
            return;
        }
        if (character == ',' || character == ':') {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            ++index_;
            return;
        }
        if (character == '/' || character == '*' || character == '+' || character == '-' ||
            character == '>' || character == '<' || character == '=') {
            emit(index_, index_ + 1U, TokenKind::operator_symbol);
            ++index_;
            return;
        }
        emit(index_, index_ + 1U, TokenKind::invalid);
        ++index_;
    }

    CssDialect dialect_{CssDialect::theme_sheet};
    std::string_view line_{};
    CssState state_{};
    std::vector<LineToken> &tokens_;
    std::size_t index_{};
};

} // namespace

std::string_view CssLanguage::name() const noexcept {
    switch (dialect_) {
    case CssDialect::theme_sheet:
        return "css";
    case CssDialect::form_document:
        return "form";
    case CssDialect::inline_style:
        return "css-inline";
    }
    return "css";
}

std::string_view CssLanguage::display_name() const noexcept {
    switch (dialect_) {
    case CssDialect::theme_sheet:
        return "CSS (theme sheet)";
    case CssDialect::form_document:
        return "Form document";
    case CssDialect::inline_style:
        return "CSS (inline style)";
    }
    return "CSS (theme sheet)";
}

LexState CssLanguage::initial_state() const noexcept {
    const CssState state{};
    return encode(state);
}

LexState CssLanguage::lex_line(std::string_view line, LexState entry,
                               std::vector<LineToken> &tokens) const {
    if (line.size() > UINT32_MAX)
        throw std::length_error("A highlighted line is longer than 4 GiB.");
    LineLexer lexer(dialect_, line, decode(entry), tokens);
    const CssState exit = lexer.run();
    return encode(exit);
}

const CssLanguage &css_theme_sheet_language() noexcept {
    static const CssLanguage language(CssDialect::theme_sheet);
    return language;
}

const CssLanguage &css_form_document_language() noexcept {
    static const CssLanguage language(CssDialect::form_document);
    return language;
}

const CssLanguage &css_inline_style_language() noexcept {
    static const CssLanguage language(CssDialect::inline_style);
    return language;
}

} // namespace swiftedit::highlight
