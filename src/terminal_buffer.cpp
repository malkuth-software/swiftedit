#include "terminal_buffer.hpp"
#include <algorithm>
#include <stdexcept>

namespace swiftedit {
namespace gf = gui_forms;
void TerminalBuffer::open(const std::filesystem::path &path) {
    if (session_.dirty())
        throw std::runtime_error("Save or explicitly discard changes before opening another file.");
    session_.open(path);
    selection_ = {};
    desired_column_.reset();
    navigation_.reset();
    newline_ = session_.read_only() ? notepad::native_newline()
                                    : notepad::preferred_newline(session_.text());
}
void TerminalBuffer::reset(bool discard) {
    if (session_.dirty() && !discard)
        throw std::runtime_error("Save or explicitly discard changes before starting a new file.");
    session_.reset();
    selection_ = {};
    desired_column_.reset();
    navigation_.reset();
    newline_ = notepad::native_newline();
}
void TerminalBuffer::synchronize() {
    if (session_.read_only())
        throw std::runtime_error(
            "This file uses read-only pages; editable navigation is unavailable.");
    const DocumentStamp stamp = session_.stamp();
    if (navigation_ && navigation_stamp_.identity == stamp.identity &&
        navigation_stamp_.revision == stamp.revision)
        return;
    // One-byte control placeholders force grapheme breaks around each malformed
    // byte. They retain source byte positions and never enter Session or output.
    std::string metadata = session_.text();
    for (std::size_t i = 0; i < metadata.size();) {
        const std::size_t length = utf8_sequence_length(metadata, i);
        if (length)
            i += length;
        else {
            metadata[i] = '\x01';
            ++i;
        }
    }
    std::unique_ptr<gf::TextStore> prepared = std::make_unique<gf::TextStore>(metadata);
    navigation_ = std::move(prepared);
    navigation_stamp_ = stamp;
    selection_.anchor = snap(std::min(selection_.anchor, metadata.size()));
    selection_.caret = snap(std::min(selection_.caret, metadata.size()));
}
std::size_t TerminalBuffer::snap(std::size_t offset) const {
    if ((*navigation_).is_grapheme_boundary(gf::Utf8Offset(offset)))
        return offset;
    // Undo may restore a scalar around a formerly valid byte position. Locate
    // its preceding grapheme by index; scalar-position APIs reject that input.
    std::size_t first = 0;
    std::size_t last = (*navigation_).grapheme_count().value() + 1;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if ((*navigation_).utf8_offset(gf::GraphemeIndex(middle)).value() <= offset)
            first = middle + 1;
        else
            last = middle;
    }
    const std::size_t index = first ? first - 1 : 0;
    const std::size_t result = (*navigation_).utf8_offset(gf::GraphemeIndex(index)).value();
    return result;
}
TerminalSelection TerminalBuffer::selection() {
    synchronize();
    return selection_;
}
SourceRange TerminalBuffer::selected_range() {
    synchronize();
    const std::size_t first = std::min(selection_.anchor, selection_.caret);
    const std::size_t last = std::max(selection_.anchor, selection_.caret);
    const SourceRange result{first, last - first};
    return result;
}
std::string TerminalBuffer::selected_text() {
    const SourceRange range = selected_range();
    const std::string result = session_.text().substr(range.offset, range.length);
    return result;
}
std::size_t TerminalBuffer::line_at(std::size_t offset) const {
    std::size_t first = 0;
    std::size_t last = (*navigation_).line_count();
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if ((*navigation_).line_start(gf::LineIndex(middle)).value() <= offset)
            first = middle + 1;
        else
            last = middle;
    }
    const std::size_t result = first ? first - 1 : 0;
    return result;
}
void TerminalBuffer::move(TerminalMotion motion, bool extend, std::size_t rows) {
    synchronize();
    if (rows == 0 || rows > 1000)
        throw std::runtime_error("Navigation row count must be between 1 and 1000.");
    std::size_t next = selection_.caret;
    const gf::TextStore &text = *navigation_;
    const std::size_t line = line_at(next);
    const gf::Utf8Range current_line = text.line_content_range(gf::LineIndex(line));
    switch (motion) {
    case TerminalMotion::left:
        if (!extend && selection_.anchor != next)
            next = std::min(selection_.anchor, next);
        else
            next = text.previous_grapheme_boundary(gf::Utf8Offset(next)).value();
        break;
    case TerminalMotion::right:
        if (!extend && selection_.anchor != next)
            next = std::max(selection_.anchor, next);
        else
            next = text.next_grapheme_boundary(gf::Utf8Offset(next)).value();
        break;
    case TerminalMotion::home:
        next = current_line.start.value();
        break;
    case TerminalMotion::end:
        next = current_line.end.value();
        break;
    case TerminalMotion::document_start:
        next = 0;
        break;
    case TerminalMotion::document_end:
        next = session_.text().size();
        break;
    case TerminalMotion::up:
    case TerminalMotion::down: {
        if (!desired_column_) {
            const std::size_t start = text.grapheme_index(current_line.start).value();
            const std::size_t caret = text.grapheme_index(gf::Utf8Offset(next)).value();
            desired_column_ = caret - start;
        }
        const std::size_t target = motion == TerminalMotion::up
                                       ? line - std::min(line, rows)
                                       : line + std::min(text.line_count() - 1 - line, rows);
        const gf::Utf8Range target_line = text.line_content_range(gf::LineIndex(target));
        const std::size_t first = text.grapheme_index(target_line.start).value();
        const std::size_t last = text.grapheme_index(target_line.end).value();
        const std::size_t column = std::min(*desired_column_, last - first);
        next = text.utf8_offset(gf::GraphemeIndex(first + column)).value();
        break;
    }
    }
    if (motion != TerminalMotion::up && motion != TerminalMotion::down)
        desired_column_.reset();
    selection_.caret = next;
    if (!extend)
        selection_.anchor = next;
}
void TerminalBuffer::move_to(std::size_t source_offset, DocumentStamp observed, bool extend) {
    const DocumentStamp current = session_.stamp();
    if (current.identity != observed.identity || current.revision != observed.revision)
        throw std::runtime_error("Caret position refers to an older document.");
    synchronize();
    if (source_offset > session_.text().size() ||
        !(*navigation_).is_grapheme_boundary(gf::Utf8Offset(source_offset)))
        throw std::runtime_error("Caret position must be a source grapheme boundary.");
    selection_.caret = source_offset;
    if (!extend)
        selection_.anchor = source_offset;
    desired_column_.reset();
}
void TerminalBuffer::select_range(SourceRange range, DocumentStamp observed) {
    const DocumentStamp current = session_.stamp();
    if (current.identity != observed.identity || current.revision != observed.revision)
        throw std::runtime_error("Selection refers to an older document.");
    synchronize();
    if (range.offset > session_.text().size() ||
        range.length > session_.text().size() - range.offset ||
        !(*navigation_).is_grapheme_boundary(gf::Utf8Offset(range.offset)) ||
        !(*navigation_).is_grapheme_boundary(gf::Utf8Offset(range.offset + range.length)))
        throw std::runtime_error("Selection must contain whole source graphemes.");
    selection_ = {range.offset, range.offset + range.length};
    desired_column_.reset();
}
void TerminalBuffer::select_all() {
    synchronize();
    selection_ = {0, session_.text().size()};
    desired_column_.reset();
}
void TerminalBuffer::insert(std::string_view source) {
    const SourceRange range = selected_range();
    session_.replace_ranges({range}, source, session_.stamp());
    selection_ = {range.offset + source.size(), range.offset + source.size()};
    desired_column_.reset();
}
std::unique_ptr<SessionReplacement> TerminalBuffer::prepare_replacement(SearchPattern pattern,
                                                                        std::string replacement,
                                                                        bool match_case) {
    std::unique_ptr<SessionReplacement> result =
        session_.prepare_replacement(std::move(pattern), std::move(replacement), match_case);
    return result;
}
std::size_t TerminalBuffer::commit_replacement(SessionReplacement &prepared) {
    const std::size_t count = session_.commit_replacement(prepared);
    if (count) {
        selection_ = {};
        desired_column_.reset();
    }
    return count;
}
void TerminalBuffer::enter() { insert(newline_); }
void TerminalBuffer::erase(bool backward) {
    SourceRange range = selected_range();
    if (range.length == 0) {
        const gf::Utf8Offset caret(selection_.caret);
        const std::size_t other = backward
                                      ? (*navigation_).previous_grapheme_boundary(caret).value()
                                      : (*navigation_).next_grapheme_boundary(caret).value();
        range.offset = std::min(other, selection_.caret);
        range.length = std::max(other, selection_.caret) - range.offset;
    }
    if (!range.length)
        return;
    session_.replace_ranges({range}, "", session_.stamp());
    selection_ = {range.offset, range.offset};
    desired_column_.reset();
}
SourceClipboard TerminalBuffer::copy() {
    const SourceRange range = selected_range();
    SourceClipboard result = session_.copy_range(range, session_.stamp());
    return result;
}
void TerminalBuffer::paste(const SourceClipboard &clipboard) {
    const SourceRange range = selected_range();
    session_.paste_range(range, clipboard, session_.stamp());
    selection_ = {range.offset + clipboard.bytes().size(), range.offset + clipboard.bytes().size()};
    desired_column_.reset();
}
SourceClipboard TerminalBuffer::cut() {
    SourceRange range = selected_range();
    if (!range.length) {
        const std::size_t line = line_at(selection_.caret);
        range.offset = (*navigation_).line_start(gf::LineIndex(line)).value();
        const std::size_t end = line + 1 < (*navigation_).line_count()
                                    ? (*navigation_).line_start(gf::LineIndex(line + 1)).value()
                                    : session_.text().size();
        range.length = end - range.offset;
    }
    SourceClipboard result = session_.copy_range(range, session_.stamp());
    if (range.length) {
        session_.replace_ranges({range}, "", session_.stamp());
        selection_ = {range.offset, range.offset};
        desired_column_.reset();
    }
    return result;
}
bool TerminalBuffer::undo() {
    const bool changed = session_.undo();
    if (changed)
        desired_column_.reset();
    return changed;
}
bool TerminalBuffer::redo() {
    const bool changed = session_.redo();
    if (changed)
        desired_column_.reset();
    return changed;
}
void TerminalBuffer::save() { session_.save(); }
void TerminalBuffer::save_as(const std::filesystem::path &path) { session_.save_as(path); }
void TerminalBuffer::save_text_copy(const std::filesystem::path &path) const {
    session_.save_text_copy(path);
}
std::size_t TerminalBuffer::line_index() {
    synchronize();
    const std::size_t result = line_at(selection_.caret);
    return result;
}
SourceRange TerminalBuffer::line_range(std::size_t line) {
    synchronize();
    if (line >= (*navigation_).line_count())
        throw std::runtime_error("Line exceeds document.");
    const gf::Utf8Range range = (*navigation_).line_content_range(gf::LineIndex(line));
    const SourceRange result{range.start.value(), range.end.value() - range.start.value()};
    return result;
}
std::size_t TerminalBuffer::line_count() {
    synchronize();
    const std::size_t result = (*navigation_).line_count();
    return result;
}
SourceRange TerminalBuffer::grapheme_range(std::size_t byte_offset) {
    synchronize();
    if (byte_offset >= session_.text().size() ||
        !(*navigation_).is_grapheme_boundary(gf::Utf8Offset(byte_offset)))
        throw std::runtime_error("Terminal glyph offset must start a source grapheme.");
    const gf::GraphemeIndex index = (*navigation_).grapheme_index(gf::Utf8Offset(byte_offset));
    const gf::Utf8Range range = (*navigation_).grapheme_range(index);
    const SourceRange result{range.start.value(), range.end.value() - range.start.value()};
    return result;
}
} // namespace swiftedit
