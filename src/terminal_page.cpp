#include "terminal_page.hpp"
#include "terminal_cells.hpp"
#include <algorithm>
#include <map>
#include <gui_forms/text.hpp>
#include <stdexcept>
namespace swiftedit {
namespace gf = gui_forms;
std::optional<std::uint64_t> terminal_page_target(
    const TerminalPageFrame &frame, const std::size_t row, const std::size_t column,
    const std::size_t width, const std::size_t rows) {
    if (!width || row >= rows)
        return std::nullopt;
    // Resolve shared source boundaries in the same order as caret rendering.
    // This bounded frame index avoids rescanning every run for every candidate.
    std::map<std::uint64_t, TerminalPageCaret> positions{};
    bool next_known = false;
    for (const TerminalPageRun &run : frame.runs) {
        if ((run.starts_grapheme && run.source_offset == frame.next.offset) ||
            (run.ends_grapheme && run.source_offset + run.source_length == frame.next.offset))
            next_known = true;
        if (run.starts_grapheme && (run.row == row || positions.contains(run.source_offset)))
            positions[run.source_offset] = {run.row, run.column};
        if (run.ends_grapheme) {
            const std::size_t end = run.column + run.cells;
            const TerminalPageCaret position = end == width ? TerminalPageCaret{run.row + 1, 0}
                                                            : TerminalPageCaret{run.row, end};
            const std::uint64_t offset = run.source_offset + run.source_length;
            if (position.row == row || positions.contains(offset))
                positions[offset] = position;
        }
    }
    for (std::size_t index = 0; index < frame.row_starts.size(); ++index) {
        const TerminalPageCursor start = frame.row_starts[index];
        if (!start.label_cell && start.offset == frame.next.offset)
            next_known = true;
        if (!start.label_cell && (index == row || positions.contains(start.offset)))
            positions[start.offset] = {index, 0};
    }
    for (const TerminalPageNewlineCaret boundary : frame.newline_carets) {
        if (boundary.offset == frame.next.offset)
            next_known = true;
        if (boundary.row == row || positions.contains(boundary.offset))
            positions[boundary.offset] = {boundary.row, boundary.column};
    }
    if (!frame.more && !frame.row_starts.empty() && !next_known)
        positions[frame.next.offset] = {frame.row_starts.size() - 1, 0};
    std::optional<std::uint64_t> before{}, first{};
    std::size_t before_column = 0;
    std::size_t first_column = width;
    for (const std::pair<const std::uint64_t, TerminalPageCaret> &entry : positions) {
        const TerminalPageCaret position = entry.second;
        if (position.row != row || position.column >= width)
            continue;
        if (!first || position.column < first_column) {
            first = entry.first;
            first_column = position.column;
        }
        if (position.column <= column && (!before || position.column >= before_column)) {
            before = entry.first;
            before_column = position.column;
        }
    }
    const std::optional<std::uint64_t> result = before ? before : first;
    return result;
}
TerminalLineBoundary::TerminalLineBoundary(const Session &session, const std::uint64_t caret,
                                           const bool end)
    : stamp_(session.stamp()), size_(session.size()), cursor_(caret), end_(end) {
    if (caret > size_)
        throw std::runtime_error("Line navigation starts outside the document.");
    complete_ = end_ ? caret == size_ : caret == 0;
}
void TerminalLineBoundary::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision || session.size() != size_)
        throw std::runtime_error("Line navigation source changed.");
}
bool TerminalLineBoundary::step(const Session &session) {
    validate(session);
    if (complete_)
        return true;
    if (end_) {
        const Page source = session.page(cursor_, 8192);
        for (std::size_t index = 0; index < source.bytes.size(); ++index) {
            if (source.bytes[index] == '\r' || source.bytes[index] == '\n') {
                cursor_ += index;
                complete_ = true;
                return true;
            }
        }
        cursor_ = source.next;
        complete_ = cursor_ == size_;
    } else {
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(cursor_, 8192));
        const std::uint64_t start = cursor_ - count;
        const Page source = session.page(start, count);
        for (std::size_t index = source.bytes.size(); index > 0; --index) {
            if (source.bytes[index - 1] == '\r' || source.bytes[index - 1] == '\n') {
                cursor_ = start + index;
                complete_ = true;
                return true;
            }
        }
        cursor_ = start;
        complete_ = cursor_ == 0;
    }
    return complete_;
}
std::uint64_t TerminalLineBoundary::result(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("Line navigation is not complete.");
    return cursor_;
}
struct BuiltPage {
    TerminalPageFrame page{};
    std::size_t next_column{};
    bool needs_context{};
    bool viewport_filled{};
    std::optional<TerminalPageCursor> word_break{};
};
struct PageWordBreak {
    TerminalPageCursor cursor{};
    std::size_t next_index{}, runs{}, graphemes{};
    bool local{};
};
template <bool PaintRuns>
static void rewind_page_word(BuiltPage &built, const PageWordBreak &point,
                             std::size_t &row, std::size_t &column, std::size_t &index) {
    if constexpr (PaintRuns) {
        built.page.runs.resize(point.runs);
        built.page.graphemes.resize(point.graphemes);
    }
    ++row;
    column = 0;
    built.page.next = point.cursor;
    built.page.row_starts.push_back(point.cursor);
    if (point.local)
        index = point.next_index - 1;
}
template <bool PaintRuns>
static BuiltPage build_terminal_page(const Session &session, const TerminalPageCursor cursor,
                                     const std::size_t width, const std::size_t rows,
                                     const std::size_t source_budget, const std::size_t initial_column,
                                     const std::optional<TerminalPageCursor> initial_break = std::nullopt) {
    if (!width || width > 1000 || !rows || rows > 32768)
        throw std::runtime_error("Terminal page dimensions exceed their bounded range.");
    if (initial_column >= width)
        throw std::runtime_error("Invalid terminal scan column.");
    const Page source = session.page(cursor.offset, source_budget);
    const bool eof = source.next == source.size;
    std::string metadata = source.bytes;
    for (std::size_t i = 0; i < metadata.size();) {
        const std::size_t length = utf8_sequence_length(metadata, i);
        if (length) {
            i += length;
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(metadata[i]);
        const std::size_t expected = byte >= 0xc2 && byte <= 0xdf   ? 2
                                     : byte >= 0xe0 && byte <= 0xef ? 3
                                     : byte >= 0xf0 && byte <= 0xf4 ? 4
                                                                    : 0;
        if (!eof && expected && metadata.size() - i < expected) {
            metadata.resize(i);
            break;
        }
        metadata[i] = '\x01';
        ++i;
    }
    const gf::TextStore text(metadata);
    std::size_t count = text.grapheme_count().value();
    if (!eof && count)
        --count;
    BuiltPage built{};
    if (!eof && !count) {
        built.needs_context = true;
        return built;
    }
    TerminalPageFrame &result = built.page;
    if constexpr (PaintRuns) {
        result.runs.reserve(std::min<std::size_t>(metadata.size(), width * rows));
        result.graphemes.reserve(std::min<std::size_t>(count, width * rows));
    }
    result.next = cursor;
    if (!initial_column)
        result.row_starts.push_back(cursor);
    std::size_t row = 0;
    std::size_t column = initial_column;
    bool after_wrap = cursor.after_wrap;
    std::optional<PageWordBreak> word_break{};
    if (initial_break)
        word_break = PageWordBreak{*initial_break, 0, 0, 0, false};
    bool stop = false;
    for (std::size_t index = 0; index < count && row < rows; ++index) {
        const gf::Utf8Range range = text.grapheme_range(gf::GraphemeIndex(index));
        const std::size_t offset = range.start.value();
        const std::size_t length = range.end.value() - offset;
        const std::string_view bytes(source.bytes.data() + offset, length);
        if constexpr (PaintRuns)
            result.graphemes.push_back({static_cast<std::size_t>(cursor.offset + offset), length});
        if (bytes == "\r" || bytes == "\n" || bytes == "\r\n") {
            word_break.reset();
            if (index == 0 && cursor.label_cell)
                throw std::runtime_error("Invalid terminal label continuation.");
            if constexpr (PaintRuns)
                result.newline_carets.push_back({cursor.offset + offset, row, column});
            const bool advance_row = !after_wrap;
            if (advance_row)
                ++row;
            after_wrap = false;
            column = 0;
            result.next = {cursor.offset + offset + length, 0, false};
            if constexpr (PaintRuns)
                result.newline_carets.push_back({result.next.offset, row, column});
            if (advance_row)
                result.row_starts.push_back(result.next);
            continue;
        }
        after_wrap = false;
        TerminalGlyph glyph = terminal_glyph(bytes, column);
        std::size_t consumed = index == 0 ? cursor.label_cell : 0;
        if (consumed && (!glyph.label || consumed >= glyph.cells))
            throw std::runtime_error("Invalid terminal label continuation.");
        if (!glyph.label && glyph.cells > width)
            throw std::runtime_error("A printable grapheme is wider than this terminal. Enlarge "
                                     "the window; source is unchanged.");
        if (word_break && column + glyph.cells - consumed > width) {
            const PageWordBreak point = *word_break;
            rewind_page_word<PaintRuns>(built, point, row, column, index);
            word_break.reset();
            after_wrap = false;
            if (!point.local) {
                stop = true;
                break;
            }
            continue;
        }
        if (!glyph.label && column + glyph.cells > width) {
            ++row;
            column = 0;
            result.row_starts.push_back({cursor.offset + offset, 0, false});
            word_break.reset();
            if (row == rows)
                break;
            // Tab stops depend on the row column after wrapping.
            if (bytes == "\t")
                glyph = terminal_glyph(bytes, 0);
        }
        while (consumed < glyph.cells && row < rows) {
            const std::size_t available = width - column;
            const std::size_t take = std::min(available, glyph.cells - consumed);
            if constexpr (PaintRuns) {
                TerminalPageRun run{};
                run.row = row;
                run.column = column;
                run.source_offset = cursor.offset + offset;
                run.source_length = length;
                run.cells = take;
                run.starts_grapheme = consumed == 0;
                run.ends_grapheme = consumed + take == glyph.cells;
                run.text = glyph.label ? glyph.text.substr(consumed, take) : glyph.text;
                result.runs.push_back(std::move(run));
            }
            consumed += take;
            column += take;
            if (consumed == glyph.cells)
                result.next = {cursor.offset + offset + length, 0};
            else
                result.next = {cursor.offset + offset, consumed};
            if (consumed == glyph.cells && (bytes == " " || bytes == "\t"))
                word_break = PageWordBreak{result.next, index + 1, result.runs.size(), result.graphemes.size(), true};
            if (column == width) {
                bool follows = consumed < glyph.cells;
                if (!follows && index + 1 < text.grapheme_count().value()) {
                    const gf::Utf8Range next_range = text.grapheme_range(gf::GraphemeIndex(index + 1));
                    const char next_byte = source.bytes[next_range.start.value()];
                    follows = next_byte != '\r' && next_byte != '\n';
                }
                if (word_break && follows && (*word_break).cursor.offset < result.next.offset) {
                    const PageWordBreak point = *word_break;
                    rewind_page_word<PaintRuns>(built, point, row, column, index);
                    word_break.reset();
                    after_wrap = false;
                    stop = !point.local;
                    break;
                }
                ++row;
                column = 0;
                after_wrap = true;
                result.next.after_wrap = true;
                result.row_starts.push_back(result.next);
                word_break.reset();
            }
        }
        if (stop)
            break;
    }
    result.more = result.next.offset < source.size || result.next.label_cell != 0;
    built.next_column = column;
    if (word_break)
        built.word_break = (*word_break).cursor;
    built.viewport_filled = row >= rows;
    return built;
}
std::optional<std::uint64_t> terminal_page_horizontal(const TerminalPageFrame &frame,
                                                       const std::uint64_t caret, const bool right) {
    std::optional<std::uint64_t> result{};
    if (right) {
        for (const SourceRange range : frame.graphemes) {
            const std::uint64_t end = range.offset + range.length;
            if (caret >= range.offset && caret < end) {
                result = end;
                break;
            }
        }
    } else {
        for (const SourceRange range : frame.graphemes) {
            const std::uint64_t end = range.offset + range.length;
            if (caret > range.offset && caret <= end) {
                result = range.offset;
                break;
            }
        }
    }
    return result;
}
std::optional<TerminalPageCaret> terminal_page_caret(const TerminalPageFrame &frame,
                                                   const std::uint64_t caret,
                                                   const std::size_t width, const std::size_t rows) {
    std::optional<TerminalPageCaret> result{};
    for (const TerminalPageRun &run : frame.runs) {
        if (run.starts_grapheme && run.source_offset == caret) {
            result = TerminalPageCaret{run.row, run.column};
            break;
        }
        if (run.ends_grapheme && run.source_offset + run.source_length == caret) {
            const std::size_t column = run.column + run.cells;
            result = column == width ? TerminalPageCaret{run.row + 1, 0}
                                     : TerminalPageCaret{run.row, column};
        }
    }
    // A row-start boundary wins over the preceding run's end when a wide
    // grapheme wrapped before painting. Newlines also have source boundaries
    // on otherwise empty rows, including a suppressed newline after wrapping.
    for (std::size_t row = 0; row < frame.row_starts.size(); ++row) {
        if (!frame.row_starts[row].label_cell && frame.row_starts[row].offset == caret) {
            result = TerminalPageCaret{row, 0};
            break;
        }
    }
    for (const TerminalPageNewlineCaret boundary : frame.newline_carets)
        if (boundary.offset == caret)
            result = TerminalPageCaret{boundary.row, boundary.column};
    if (!result && !frame.more && frame.next.offset == caret && !frame.row_starts.empty())
        result = TerminalPageCaret{frame.row_starts.size() - 1, 0};
    if (result && ((*result).row >= rows || (*result).column >= width))
        result.reset();
    return result;
}
TerminalPageFrame terminal_page(const Session &session, TerminalPageCursor cursor,
                                std::size_t width, std::size_t rows) {
    if (!width || width > 1000 || !rows || rows > 300)
        throw std::runtime_error("Terminal page dimensions exceed their bounded range.");
    // Start near the visible ASCII extent rather than segmenting 8 KiB for
    // every scrolled row. Extra context keeps the final scalar/CRLF intact;
    // multibyte text and longer graphemes grow through the same checked path.
    std::size_t budget = std::min<std::size_t>(8192, std::max<std::size_t>(256, width * rows + 4));
    BuiltPage built = build_terminal_page<true>(session, cursor, width, rows, budget, 0);
    // Most viewports fit in a small source window. Grow only when more source
    // is needed to fill the viewport or complete its first grapheme.
    while (budget < maximum_page &&
           (built.needs_context || (!built.viewport_filled && built.page.more))) {
        budget = std::min(maximum_page, budget * 2);
        built = build_terminal_page<true>(session, cursor, width, rows, budget, 0);
    }
    if (built.needs_context)
        throw std::runtime_error(
            "A complete grapheme exceeds the terminal page context limit. Source is unchanged.");
    TerminalPageFrame result = std::move(built.page);
    return result;
}
TerminalPageEnd::TerminalPageEnd(const Session &session, const std::size_t width,
                               const std::size_t rows, const std::size_t source_budget)
    : stamp_(session.stamp()), size_(session.size()), width_(width), rows_(rows), source_budget_(source_budget) {
    if (!width_ || width_ > 1000 || !rows_ || rows_ > 300)
        throw std::runtime_error("Terminal page dimensions exceed their bounded range.");
    if (!source_budget_ || source_budget_ > maximum_page)
        throw std::runtime_error("End scan source budget must be 1..65536 bytes.");
}
TerminalPageEnd::TerminalPageEnd(const Session &session, const std::size_t width,
                               const std::size_t rows, const TerminalPageCursor before)
    : TerminalPageEnd(session, width, rows) {
    if (before.offset > size_)
        throw std::runtime_error("Previous page cursor exceeds source size.");
    before_ = before;
}
static bool cursor_precedes(const TerminalPageCursor left, const TerminalPageCursor right) {
    const bool result = left.offset < right.offset ||
                        (left.offset == right.offset && left.label_cell < right.label_cell);
    return result;
}
void TerminalPageEnd::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("End navigation belongs to an older document.");
}
bool TerminalPageEnd::step(const Session &session) {
    validate(session);
    if (complete_)
        return true;
    // Grow context only when the first grapheme is incomplete. Ordinary steps
    // yield after 8 KiB; exceptionally long graphemes retain the 64 KiB ceiling.
    std::size_t budget = source_budget_;
    BuiltPage built = build_terminal_page<false>(session, cursor_, width_, 32768, budget, column_, word_break_);
    while (built.needs_context && budget < maximum_page) {
        budget = std::min(maximum_page, budget * 2);
        built = build_terminal_page<false>(session, cursor_, width_, 32768, budget, column_, word_break_);
    }
    if (built.needs_context)
        throw std::runtime_error("A complete grapheme exceeds the terminal page context limit. Source is unchanged.");
    const TerminalPageFrame &frame = built.page;
    if (frame.more && frame.next == cursor_ && built.next_column == column_)
        throw std::runtime_error("End navigation made no source progress.");
    bool passed_before = false;
    for (const TerminalPageCursor start : frame.row_starts) {
        if (before_ && !cursor_precedes(start, *before_)) {
            passed_before = true;
            break;
        }
        if (start.offset == size_ && size_ != 0)
            continue;
        if (!tail_.empty() && tail_.back() == start)
            continue;
        tail_.push_back(start);
        if (tail_.size() > 32768 + rows_)
            tail_.pop_front();
    }
    cursor_ = frame.next;
    column_ = built.next_column;
    word_break_ = built.word_break;
    // A word can move to the following row after a later overflow. Do not
    // finalize backward reconstruction merely because its bytes were scanned.
    complete_ = !frame.more || passed_before;
    return complete_;
}
TerminalPageCursor TerminalPageEnd::result(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("End navigation is not complete.");
    const std::size_t first = tail_.size() > rows_ ? tail_.size() - rows_ : 0;
    const TerminalPageCursor result = tail_.empty() ? TerminalPageCursor{} : tail_[first];
    return result;
}
std::unique_ptr<TerminalPageEnd> TerminalPager::prepare_previous(const Session &session,
                                                               const bool page) const {
    std::unique_ptr<TerminalPageEnd> result{};
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision)
        throw std::runtime_error("Previous navigation belongs to an older document.");
    if (!width_ || !rows_ || (!cursor_.offset && !cursor_.label_cell))
        return result;
    const std::size_t requested = page ? rows_ : 1;
    bool retained = history_.size() >= requested;
    if (page && !page_history_.empty()) {
        retained = false;
        for (const TerminalPageCursor entry : history_) {
            if (entry == page_history_.back()) {
                retained = true;
                break;
            }
        }
    }
    if (!retained)
        result = std::make_unique<TerminalPageEnd>(session, width_, requested, cursor_);
    return result;
}
void TerminalPager::finish_end(const Session &session, const TerminalPageEnd &task) {
    const TerminalPageCursor target = task.result(session);
    std::deque<TerminalPageCursor> history{};
    for (const TerminalPageCursor start : task.tail_) {
        if (start == target)
            break;
        history.push_back(start);
    }
    reset(session);
    cursor_ = target;
    width_ = task.width_;
    history_.swap(history);
}
void TerminalPager::reset(const Session &session) {
    stamp_ = session.stamp();
    cursor_ = {};
    history_.clear();
    page_history_.clear();
    frame_ = {};
    width_ = 0;
    rows_ = 0;
    ready_ = false;
}
const TerminalPageFrame &TerminalPager::frame(const Session &session, std::size_t width,
                                              std::size_t rows) {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision)
        throw std::runtime_error("Terminal page belongs to an older document. Reopen the view.");
    if (!ready_ || width != width_ || rows != rows_) {
        TerminalPageFrame prepared = terminal_page(session, cursor_, width, rows);
        // Row cursors depend on wrap width; page jumps also depend on height.
        // Keep the source anchor but reconstruct earlier rows at the new width.
        if (width_ && width != width_)
            history_.clear();
        if (width != width_ || rows != rows_)
            page_history_.clear();
        frame_ = std::move(prepared);
        width_ = width;
        rows_ = rows;
        ready_ = true;
    }
    return frame_;
}
void TerminalPager::next() {
    if (!ready_ || !frame_.more || frame_.next == cursor_)
        return;
    page_history_.push_back(cursor_);
    if (page_history_.size() > 1024)
        page_history_.pop_front();
    for (const TerminalPageCursor start : frame_.row_starts)
        if (!(start == frame_.next))
            retain(start);
    cursor_ = frame_.next;
    ready_ = false;
}
void TerminalPager::previous() {
    if (history_.empty()) {
        if (cursor_.offset || cursor_.label_cell)
            throw std::runtime_error(
                "Earlier page history is no longer retained. Ctrl+Home returns to the first page.");
        return;
    }
    std::size_t count = std::min(rows_, history_.size());
    if (!page_history_.empty()) {
        const TerminalPageCursor target = page_history_.back();
        count = 0;
        bool found = false;
        for (std::size_t index = history_.size(); index > 0; --index) {
            ++count;
            if (history_[index - 1] == target) {
                found = true;
                break;
            }
        }
        if (!found)
            throw std::runtime_error(
                "Earlier page history is no longer retained. Ctrl+Home returns to the first page.");
        page_history_.pop_back();
    }
    for (std::size_t row = 0; row < count; ++row) {
        cursor_ = history_.back();
        history_.pop_back();
    }
    ready_ = false;
}
void TerminalPager::retain(TerminalPageCursor cursor) {
    history_.push_back(cursor);
    // Bound history by row cursors, independent of window height.
    if (history_.size() > 32768)
        history_.pop_front();
}
void TerminalPager::down() {
    if (!ready_ || frame_.row_starts.size() < 2)
        return;
    const TerminalPageCursor next = frame_.row_starts[1];
    // An EOF boundary is not another visual row to scroll into.
    if (!frame_.more && next == frame_.next)
        return;
    retain(cursor_);
    cursor_ = next;
    ready_ = false;
}
void TerminalPager::up() {
    if (history_.empty()) {
        if (cursor_.offset || cursor_.label_cell)
            throw std::runtime_error(
                "Earlier row history is no longer retained. Ctrl+Home returns to the first page.");
        return;
    }
    cursor_ = history_.back();
    history_.pop_back();
    if (!page_history_.empty() && cursor_ == page_history_.back())
        page_history_.pop_back();
    ready_ = false;
}
void TerminalPager::reveal(const Session &session, const std::uint64_t offset) {
    if (offset > session.size())
        throw std::runtime_error("Search destination exceeds source size.");
    reset(session);
    cursor_.offset = offset;
}
void TerminalPager::first() {
    cursor_ = {};
    history_.clear();
    page_history_.clear();
    ready_ = false;
}
} // namespace swiftedit
