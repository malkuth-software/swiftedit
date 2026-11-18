#include "xml_language.hpp"
#include <algorithm>
#include <stdexcept>

namespace swiftedit::highlight {
namespace {

// Where the lexer stands at a byte.
enum class Mode : std::uint8_t {
    text = 0U,            // between tags
    comment = 1U,         // inside <!-- ... -->
    tag = 2U,             // inside a tag, between attributes
    attribute_named = 3U, // after an attribute's name, before its `=`
    value_expected = 4U,  // after an attribute's `=`, before its quote
    value_double = 5U,    // inside a "..." value
    value_single = 6U,    // inside a '...' value
};

// The decoded form of a LexState. Bit layout of LexState::value:
//   0-2   mode
//   3     the attribute being read is `id` (its value is the stable ID)
//   4     the tag is the XML declaration (it ends with ?>)
struct XmlState final {
    Mode mode{Mode::text};
    bool id_value{};
    bool declaration{};
};

XmlState decode(LexState state) noexcept {
    const std::uint64_t bits = state.value;
    XmlState result{};
    const std::uint64_t mode = bits & 0x7U;
    result.mode = mode <= static_cast<std::uint64_t>(Mode::value_single) ? static_cast<Mode>(mode) : Mode::text;
    result.id_value = ((bits >> 3U) & 0x1U) != 0U;
    result.declaration = ((bits >> 4U) & 0x1U) != 0U;
    return result;
}

LexState encode(const XmlState &state) noexcept {
    std::uint64_t bits = static_cast<std::uint64_t>(static_cast<std::uint8_t>(state.mode) & 0x7U);
    bits |= static_cast<std::uint64_t>(state.id_value ? 1U : 0U) << 3U;
    bits |= static_cast<std::uint64_t>(state.declaration ? 1U : 0U) << 4U;
    const LexState result{bits};
    return result;
}

bool is_space(char character) noexcept {
    const bool result = character == ' ' || character == '\t' || character == '\r' || character == '\n';
    return result;
}

bool is_name_start(char character) noexcept {
    const unsigned char byte = static_cast<unsigned char>(character);
    // Bytes from 0x80 on begin or continue a UTF-8 name character.
    const bool result = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                        character == '_' || character == ':' || byte >= 0x80U;
    return result;
}

bool is_name_character(char character) noexcept {
    const bool result = is_name_start(character) || (character >= '0' && character <= '9') || character == '-' ||
                        character == '.';
    return result;
}

// The longest entity or character reference: "&#x10FFFF;" (10 bytes).
constexpr std::size_t maximum_reference = 10U;

class LineLexer final {
public:
    LineLexer(std::string_view line, XmlState state, std::vector<LineToken> &tokens) noexcept
        : line_(line), state_(state), tokens_(tokens) {}

    XmlState run() {
        while (index_ < line_.size()) {
            switch (state_.mode) {
            case Mode::text:
                text_token();
                break;
            case Mode::comment:
                comment_token(false);
                break;
            case Mode::tag:
                tag_token();
                break;
            case Mode::attribute_named:
                equals_token();
                break;
            case Mode::value_expected:
                quote_token();
                break;
            case Mode::value_double:
                value_token('"', false);
                break;
            case Mode::value_single:
                value_token('\'', false);
                break;
            default:
                state_.mode = Mode::text;
                break;
            }
        }
        return state_;
    }

private:
    void emit(std::size_t start, std::size_t end, TokenKind kind) {
        if (end <= start)
            return;
        const LineToken token{static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end - start), kind};
        tokens_.push_back(token);
    }

    [[nodiscard]] bool starts_with(std::size_t position, std::string_view prefix) const noexcept {
        const bool result = line_.substr(position, prefix.size()) == prefix;
        return result;
    }

    [[nodiscard]] std::size_t name_end(std::size_t start) const noexcept {
        std::size_t position = start;
        while (position < line_.size() && is_name_character(line_[position]))
            ++position;
        return position;
    }

    void skip_space() noexcept {
        while (index_ < line_.size() && is_space(line_[index_]))
            ++index_;
    }

    // Between tags: plain text up to the next `<` or `&`.
    void text_token() {
        const char character = line_[index_];
        if (character == '&') {
            reference_token();
            return;
        }
        if (character != '<') {
            ++index_;
            return;
        }
        if (starts_with(index_, "<!--")) {
            state_.mode = Mode::comment;
            comment_token(true);
            return;
        }
        const bool closing = starts_with(index_, "</");
        const bool declaration = starts_with(index_, "<?");
        const std::size_t name_start = closing || declaration ? index_ + 2U : index_ + 1U;
        if (name_start >= line_.size() || !is_name_start(line_[name_start])) {
            emit(index_, name_start, TokenKind::invalid);
            index_ = name_start;
            return;
        }
        const std::size_t end = name_end(name_start);
        if (declaration) {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            emit(index_ + 1U, end, TokenKind::at_rule);
        } else {
            emit(index_, name_start, TokenKind::punctuation);
            emit(name_start, end, TokenKind::type_selector);
        }
        index_ = end;
        state_.mode = Mode::tag;
        state_.declaration = declaration;
        state_.id_value = false;
    }

    // `&name;` or `&#...;`, as one identifier; a lone `&` is invalid.
    void reference_token() {
        std::size_t position = index_ + 1U;
        const std::size_t limit = std::min(line_.size(), index_ + maximum_reference);
        while (position < limit && line_[position] != ';' && !is_space(line_[position]) && line_[position] != '<' &&
               line_[position] != '&' && line_[position] != '"' && line_[position] != '\'')
            ++position;
        if (position < limit && line_[position] == ';' && position > index_ + 1U) {
            emit(index_, position + 1U, TokenKind::value_identifier);
            index_ = position + 1U;
            return;
        }
        emit(index_, index_ + 1U, TokenKind::invalid);
        ++index_;
    }

    // From the comment's opening `<!--` (`opening`), or from the line's start
    // inside one, to the closing `-->`, or to the line's end.
    void comment_token(bool opening) {
        const std::size_t start = index_;
        const std::size_t search = opening ? index_ + 4U : index_;
        const std::size_t close = line_.find("-->", search);
        if (close == std::string_view::npos) {
            emit(start, line_.size(), TokenKind::comment);
            index_ = line_.size();
            return;
        }
        emit(start, close + 3U, TokenKind::comment);
        index_ = close + 3U;
        state_.mode = Mode::text;
    }

    // Inside a tag: an attribute's name, or the tag's end.
    void tag_token() {
        skip_space();
        if (index_ >= line_.size())
            return;
        const char character = line_[index_];
        if (character == '>' && !state_.declaration) {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            ++index_;
            state_.mode = Mode::text;
            return;
        }
        if (character == '/' && !state_.declaration && starts_with(index_, "/>")) {
            emit(index_, index_ + 2U, TokenKind::punctuation);
            index_ += 2U;
            state_.mode = Mode::text;
            return;
        }
        if (character == '?' && state_.declaration && starts_with(index_, "?>")) {
            emit(index_, index_ + 2U, TokenKind::punctuation);
            index_ += 2U;
            state_.mode = Mode::text;
            state_.declaration = false;
            return;
        }
        if (is_name_start(character)) {
            const std::size_t end = name_end(index_);
            emit(index_, end, TokenKind::property);
            state_.id_value = line_.substr(index_, end - index_) == "id";
            index_ = end;
            state_.mode = Mode::attribute_named;
            return;
        }
        emit(index_, index_ + 1U, TokenKind::invalid);
        ++index_;
    }

    // After an attribute's name: its `=`. Anything else is a malformed tag;
    // the lexer reads on as if between attributes.
    void equals_token() {
        skip_space();
        if (index_ >= line_.size())
            return;
        if (line_[index_] == '=') {
            emit(index_, index_ + 1U, TokenKind::punctuation);
            ++index_;
            state_.mode = Mode::value_expected;
            return;
        }
        state_.mode = Mode::tag;
        state_.id_value = false;
    }

    // After `=`: the value's opening quote.
    void quote_token() {
        skip_space();
        if (index_ >= line_.size())
            return;
        const char character = line_[index_];
        if (character == '"' || character == '\'') {
            state_.mode = character == '"' ? Mode::value_double : Mode::value_single;
            value_token(character, true);
            return;
        }
        emit(index_, index_ + 1U, TokenKind::invalid);
        ++index_;
        state_.mode = Mode::tag;
        state_.id_value = false;
    }

    // A quoted value from its opening quote (`opening`), or from the line's
    // start inside one, to its closing quote, or to the line's end.
    void value_token(char quote, bool opening) {
        const std::size_t start = index_;
        const std::size_t search = opening ? index_ + 1U : index_;
        const std::size_t close = line_.find(quote, search);
        const TokenKind kind = state_.id_value ? TokenKind::identity_selector : TokenKind::string;
        if (close == std::string_view::npos) {
            emit(start, line_.size(), kind);
            index_ = line_.size();
            return;
        }
        emit(start, close + 1U, kind);
        index_ = close + 1U;
        state_.mode = Mode::tag;
        state_.id_value = false;
    }

    std::string_view line_{};
    XmlState state_{};
    std::vector<LineToken> &tokens_;
    std::size_t index_{};
};

} // namespace

std::string_view XmlLanguage::name() const noexcept {
    switch (dialect_) {
    case XmlDialect::form_document:
        return "form";
    case XmlDialect::control_document:
        return "control";
    }
    return "form";
}

std::string_view XmlLanguage::display_name() const noexcept {
    switch (dialect_) {
    case XmlDialect::form_document:
        return "Form document (XML)";
    case XmlDialect::control_document:
        return "Composite control (XML)";
    }
    return "Form document (XML)";
}

LexState XmlLanguage::initial_state() const noexcept {
    const XmlState state{};
    return encode(state);
}

LexState XmlLanguage::lex_line(std::string_view line, LexState entry, std::vector<LineToken> &tokens) const {
    if (line.size() > UINT32_MAX)
        throw std::length_error("A highlighted line is longer than 4 GiB.");
    LineLexer lexer(line, decode(entry), tokens);
    const XmlState exit = lexer.run();
    return encode(exit);
}

const XmlLanguage &xml_form_document_language() noexcept {
    static const XmlLanguage language(XmlDialect::form_document);
    return language;
}

const XmlLanguage &xml_control_document_language() noexcept {
    static const XmlLanguage language(XmlDialect::control_document);
    return language;
}

} // namespace swiftedit::highlight
