#include "highlighter.hpp"
#include <algorithm>
#include <stdexcept>

namespace swiftedit::highlight {
namespace {

// Where one line's content ends and the next line starts. A line ends at LF,
// CR or CRLF; the last line has no terminator.
struct LineBreak final {
    std::size_t content_end{};
    std::size_t next_start{};
    bool terminated{};
};

LineBreak find_line_break(std::string_view text, std::size_t start) noexcept {
    const std::size_t terminator = text.find_first_of("\r\n", start);
    LineBreak result{};
    if (terminator == std::string_view::npos) {
        result.content_end = text.size();
        result.next_start = text.size();
        result.terminated = false;
        return result;
    }
    const bool crlf =
        text[terminator] == '\r' && terminator + 1U < text.size() && text[terminator + 1U] == '\n';
    result.content_end = terminator;
    result.next_start = terminator + (crlf ? 2U : 1U);
    result.terminated = true;
    return result;
}

// Orders line records by start, for the binary searches over line starts.
struct LineStartBefore final {
    template <typename Record> bool operator()(const Record &record, ByteOffset offset) const noexcept {
        return record.start < offset;
    }
};

struct OffsetBeforeLineStart final {
    template <typename Record> bool operator()(ByteOffset offset, const Record &record) const noexcept {
        return offset < record.start;
    }
};

// The length of the UTF-8 sequence a lead byte starts; 1 for a stray byte,
// so that malformed text still advances.
std::size_t utf8_sequence_length(unsigned char lead) noexcept {
    if (lead < 0xC0U)
        return 1U;
    if (lead < 0xE0U)
        return 2U;
    if (lead < 0xF0U)
        return 3U;
    return 4U;
}

} // namespace

DocumentHighlighter::DocumentHighlighter(const Language &language) : language_(&language) {
    reset(std::string_view{});
}

void DocumentHighlighter::reset(std::string_view text) {
    if (text.size() > maximum_text_bytes)
        throw std::length_error("The document is too large to highlight.");
    scratch_lines_.clear();
    scratch_tokens_.clear();
    LexState state = (*language_).initial_state();
    std::size_t start = 0U;
    std::uint64_t lexed = 0U;
    for (;;) {
        const LineBreak line_break = find_line_break(text, start);
        LineRecord record{};
        record.start = ByteOffset{start};
        record.length = line_break.content_end - start;
        record.entry = state;
        record.first_token = scratch_tokens_.size();
        state = (*language_).lex_line(text.substr(start, record.length), state, scratch_tokens_);
        record.token_count = scratch_tokens_.size() - record.first_token;
        scratch_lines_.push_back(record);
        ++lexed;
        if (!line_break.terminated)
            break;
        start = line_break.next_start;
    }
    lines_.swap(scratch_lines_);
    tokens_.swap(scratch_tokens_);
    text_size_ = text.size();
    ++counters_.resets;
    counters_.lines_lexed += lexed;
    counters_.last_update_lines = lexed;
}

void DocumentHighlighter::set_language(const Language &language, std::string_view text) {
    const Language *previous = language_;
    language_ = &language;
    try {
        reset(text);
    } catch (...) {
        language_ = previous;
        throw;
    }
}

ByteRange DocumentHighlighter::apply_edit(std::string_view text, TextEdit edit) {
    // Validate the description against the previous and new sizes first.
    const std::size_t old_size = text_size_;
    const bool removal_fits = edit.start.value <= old_size && edit.removed <= old_size - edit.start.value;
    const bool sizes_agree = removal_fits && text.size() == old_size - edit.removed + edit.inserted;
    const bool insertion_fits = sizes_agree && edit.inserted <= text.size() - edit.start.value;
    if (!insertion_fits)
        throw std::invalid_argument("The edit does not describe the change of the highlighted text.");
    if (text.size() > maximum_text_bytes)
        throw std::length_error("The document is too large to highlight.");

    // Start at the line holding the edit, or the line before when the edit
    // begins a line: a CR before the edit and an LF inserted at it make one
    // CRLF terminator, and a deletion there can join two lines.
    std::size_t first = line_at(edit.start).value;
    if (first > 0U && lines_[first].start == edit.start)
        --first;
    const std::size_t old_edit_end = edit.start.value + edit.removed;
    const std::size_t new_edit_end = edit.start.value + edit.inserted;

    scratch_lines_.clear();
    scratch_tokens_.clear();
    LexState state = lines_[first].entry;
    std::size_t start = lines_[first].start.value;
    std::size_t converged = lines_.size();
    std::uint64_t lexed = 0U;
    for (;;) {
        // A new line wholly after the edit has the same bytes as the old line
        // at the matching old offset; entered in the same state, it lexes the
        // same, and so does every line after it.
        if (start >= new_edit_end) {
            const ByteOffset old_start{start - new_edit_end + old_edit_end};
            const std::vector<LineRecord>::const_iterator found =
                std::lower_bound(lines_.cbegin() + static_cast<std::ptrdiff_t>(first), lines_.cend(),
                                 old_start, LineStartBefore{});
            const bool matches = found != lines_.cend() && (*found).start == old_start &&
                                 (*found).entry == state;
            if (matches) {
                converged = static_cast<std::size_t>(found - lines_.cbegin());
                break;
            }
        }
        const LineBreak line_break = find_line_break(text, start);
        LineRecord record{};
        record.start = ByteOffset{start};
        record.length = line_break.content_end - start;
        record.entry = state;
        record.first_token = scratch_tokens_.size();
        state = (*language_).lex_line(text.substr(start, record.length), state, scratch_tokens_);
        record.token_count = scratch_tokens_.size() - record.first_token;
        scratch_lines_.push_back(record);
        ++lexed;
        if (!line_break.terminated)
            break;
        start = line_break.next_start;
    }

    // Splice: old lines [first, converged) become the scratch lines.
    const std::size_t token_begin = lines_[first].first_token;
    const std::size_t token_end =
        converged < lines_.size() ? lines_[converged].first_token : tokens_.size();
    const std::ptrdiff_t token_delta =
        static_cast<std::ptrdiff_t>(scratch_tokens_.size()) -
        static_cast<std::ptrdiff_t>(token_end - token_begin);
    for (LineRecord &record : scratch_lines_)
        record.first_token += token_begin;
    for (std::size_t index = converged; index < lines_.size(); ++index) {
        LineRecord &record = lines_[index];
        record.start = ByteOffset{record.start.value - old_edit_end + new_edit_end};
        record.first_token = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(record.first_token) + token_delta);
    }
    tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(token_begin),
                  tokens_.begin() + static_cast<std::ptrdiff_t>(token_end));
    tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(token_begin),
                   scratch_tokens_.cbegin(), scratch_tokens_.cend());
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(first),
                 lines_.begin() + static_cast<std::ptrdiff_t>(converged));
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(first), scratch_lines_.cbegin(),
                  scratch_lines_.cend());
    text_size_ = text.size();
    ++counters_.edits;
    counters_.lines_lexed += lexed;
    counters_.last_update_lines = lexed;

    const std::size_t replaced_end_line = first + scratch_lines_.size();
    const ByteOffset changed_end =
        replaced_end_line < lines_.size() ? lines_[replaced_end_line].start : ByteOffset{text.size()};
    const ByteRange changed{lines_[first].start, changed_end};
    return changed;
}

ByteOffset DocumentHighlighter::line_start(LineIndex line) const {
    const ByteOffset result = lines_.at(line.value).start;
    return result;
}

ByteOffset DocumentHighlighter::line_end(LineIndex line) const {
    const LineRecord &record = lines_.at(line.value);
    const ByteOffset result{record.start.value + record.length};
    return result;
}

LineIndex DocumentHighlighter::line_at(ByteOffset offset) const noexcept {
    const std::vector<LineRecord>::const_iterator after =
        std::upper_bound(lines_.cbegin(), lines_.cend(), offset, OffsetBeforeLineStart{});
    const std::size_t index = after == lines_.cbegin() ? 0U : static_cast<std::size_t>(after - lines_.cbegin()) - 1U;
    const LineIndex result{index};
    return result;
}

LexState DocumentHighlighter::line_entry_state(LineIndex line) const {
    const LexState result = lines_.at(line.value).entry;
    return result;
}

std::span<const LineToken> DocumentHighlighter::line_tokens(LineIndex line) const {
    const LineRecord &record = lines_.at(line.value);
    const std::span<const LineToken> result(tokens_.data() + record.first_token, record.token_count);
    return result;
}

void DocumentHighlighter::tokens_in(ByteRange window, std::vector<Token> &tokens) const {
    tokens.clear();
    if (window.start >= window.end)
        return;
    for (std::size_t line = line_at(window.start).value; line < lines_.size(); ++line) {
        const LineRecord &record = lines_[line];
        if (record.start >= window.end)
            break;
        for (std::size_t index = 0U; index < record.token_count; ++index) {
            const LineToken &token = tokens_[record.first_token + index];
            const ByteOffset token_start{record.start.value + token.column};
            const ByteOffset token_end{token_start.value + token.length};
            if (token_end <= window.start)
                continue;
            if (token_start >= window.end)
                break;
            const Token absolute{{token_start, token_end}, token.kind};
            tokens.push_back(absolute);
        }
    }
}

Token DocumentHighlighter::token_at(ByteOffset offset) const {
    const LineIndex line = line_at(offset);
    const LineRecord &record = lines_[line.value];
    const ByteOffset content_end{record.start.value + record.length};
    ByteOffset gap_start = record.start;
    for (std::size_t index = 0U; index < record.token_count; ++index) {
        const LineToken &token = tokens_[record.first_token + index];
        const ByteOffset token_start{record.start.value + token.column};
        const ByteOffset token_end{token_start.value + token.length};
        if (offset < token_start) {
            const Token gap{{gap_start, token_start}, TokenKind::plain};
            return gap;
        }
        if (offset < token_end) {
            const Token found{{token_start, token_end}, token.kind};
            return found;
        }
        gap_start = token_end;
    }
    const Token tail{{gap_start, std::max(gap_start, content_end)}, TokenKind::plain};
    return tail;
}

ByteOffset DocumentHighlighter::offset_at(std::string_view text, SourcePosition position,
                                          ColumnUnit unit) const {
    if (text.size() != text_size_)
        throw std::invalid_argument("The text is not the highlighted text.");
    if (position.line == 0U)
        return ByteOffset{};
    const std::size_t line = static_cast<std::size_t>(position.line) - 1U;
    if (line >= lines_.size())
        return ByteOffset{text_size_};
    const LineRecord &record = lines_[line];
    const std::size_t column = position.column == 0U ? 0U : static_cast<std::size_t>(position.column) - 1U;
    if (unit == ColumnUnit::utf8_byte) {
        const ByteOffset result{record.start.value + std::min(column, record.length)};
        return result;
    }
    std::size_t byte = 0U;
    std::size_t scalars = 0U;
    while (scalars < column && byte < record.length) {
        const unsigned char lead = static_cast<unsigned char>(text[record.start.value + byte]);
        byte = std::min(byte + utf8_sequence_length(lead), record.length);
        ++scalars;
    }
    const ByteOffset result{record.start.value + byte};
    return result;
}

SourcePosition DocumentHighlighter::position_of(ByteOffset offset) const noexcept {
    const LineIndex line = line_at(offset);
    const LineRecord &record = lines_[line.value];
    const std::size_t clamped = std::min(offset.value, record.start.value + record.length);
    SourcePosition result{};
    result.line = static_cast<std::uint32_t>(line.value + 1U);
    result.column = static_cast<std::uint32_t>(clamped - record.start.value + 1U);
    return result;
}

} // namespace swiftedit::highlight
