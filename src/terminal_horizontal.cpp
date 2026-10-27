#include "terminal_horizontal.hpp"
#include "terminal_cells.hpp"
#include <gui_forms/text.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace swiftedit {
TerminalHorizontalLine::TerminalHorizontalLine(const Session &session,
                                               const TerminalLogicalPage &page,
                                               const std::size_t row,
                                               const std::uint64_t left,
                                               const std::size_t width,
                                               const std::optional<std::uint64_t> source_caret)
    : stamp_(session.stamp()), size_(session.size()), left_(left), source_caret_(source_caret) {
    if (!width || width > 1000 || left > std::numeric_limits<std::uint64_t>::max() - width)
        throw std::runtime_error("Horizontal viewport width must be 1..1000 without overflow.");
    const std::vector<TerminalLogicalRow> &rows = page.result(session);
    if (row >= rows.size())
        throw std::runtime_error("Horizontal viewport row is outside the logical page.");
    offset_ = rows[row].offset;
    read_offset_ = offset_;
    end_ = offset_ + rows[row].length;
    if (source_caret_ && (*source_caret_ < offset_ || *source_caret_ > end_))
        throw std::runtime_error("Requested caret is outside the logical line body.");
    right_ = left + width;
    const std::size_t source_capacity = static_cast<std::size_t>(
        std::min<std::uint64_t>(rows[row].length, maximum_page));
    const std::size_t maximum_runs = static_cast<std::size_t>(
        std::min<std::uint64_t>(rows[row].length, width));
    source_.reserve(source_capacity);
    frame_.runs.reserve(maximum_runs);
    frame_.carets.reserve(maximum_runs * 2 + 2);
}
void TerminalHorizontalLine::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("Horizontal viewport belongs to an older document.");
    if (failed_)
        throw std::runtime_error("Horizontal viewport preparation previously failed.");
}
void TerminalHorizontalLine::retain_caret(const std::uint64_t offset,
                                         const std::uint64_t column) {
    if (frame_.carets.empty() || frame_.carets.back().source_offset != offset)
        frame_.carets.push_back({offset, column});
}
void TerminalHorizontalLine::prepare() {
    skip_offscreen_ascii();
    finish_preparation();
    if (complete_)
        return;
    const bool line_end = read_offset_ == end_;
    std::string metadata = source_;
    for (std::size_t index = 0; index < metadata.size();) {
        const std::size_t length = utf8_sequence_length(metadata, index);
        if (length) {
            index += length;
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(metadata[index]);
        const std::size_t expected = byte >= 0xc2 && byte <= 0xdf ? 2
            : byte >= 0xe0 && byte <= 0xef ? 3 : byte >= 0xf0 && byte <= 0xf4 ? 4 : 0;
        if (!line_end && expected && metadata.size() - index < expected) {
            metadata.resize(index);
            break;
        }
        // Match the paged terminal's byte-preserving segmentation policy.
        metadata[index] = '\x01';
        ++index;
    }
    const gui_forms::TextStore text(metadata);
    std::size_t count = text.grapheme_count().value();
    if (!line_end && count)
        --count;
    std::size_t consumed = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const gui_forms::Utf8Range range = text.grapheme_range(gui_forms::GraphemeIndex(index));
        const std::size_t start = range.start.value();
        const std::size_t length = range.end.value() - start;
        const std::string_view bytes(source_.data() + start, length);
        const TerminalGlyph glyph = terminal_glyph(bytes, static_cast<std::size_t>(column_ % 4));
        if (glyph.cells > std::numeric_limits<std::uint64_t>::max() - column_)
            throw std::runtime_error("Logical line display width exceeds the supported range.");
        const std::uint64_t following = column_ + glyph.cells;
        const std::uint64_t source_start = offset_ + start;
        const std::uint64_t source_end = source_start + length;
        if (source_caret_) {
            if (*source_caret_ == source_start)
                frame_.source_caret_column = column_;
            else if (*source_caret_ == source_end)
                frame_.source_caret_column = following;
            else if (*source_caret_ > source_start && *source_caret_ < source_end)
                throw std::runtime_error("Requested caret splits a source grapheme.");
        }
        if (column_ < right_ && following > left_) {
            const std::uint64_t first = std::max(column_, left_);
            const std::uint64_t last = std::min(following, right_);
            TerminalPageRun run{};
            run.column = static_cast<std::size_t>(first - left_);
            run.source_offset = offset_ + start;
            run.source_length = length;
            run.cells = static_cast<std::size_t>(last - first);
            run.starts_grapheme = first == column_;
            run.ends_grapheme = last == following;
            if (run.starts_grapheme && run.ends_grapheme)
                run.text = glyph.text;
            else if (glyph.label || bytes == "\t")
                run.text = glyph.text.substr(static_cast<std::size_t>(first - column_), run.cells);
            else
                run.text.assign(run.cells, ' ');
            frame_.runs.push_back(std::move(run));
            retain_caret(offset_ + start, column_);
            retain_caret(offset_ + start + length, following);
        }
        column_ = following;
        consumed = start + length;
        if (column_ >= right_ && (!source_caret_ || frame_.source_caret_column))
            break;
    }
    offset_ += consumed;
    source_.erase(0, consumed);
    finish_preparation();
    if (!complete_ && source_.size() == maximum_page)
        throw std::runtime_error("A complete grapheme exceeds the terminal page context limit. Source is unchanged.");
}
void TerminalHorizontalLine::skip_offscreen_ascii() {
    std::size_t consumed = 0;
    while (consumed < source_.size()) {
        const unsigned char byte = static_cast<unsigned char>(source_[consumed]);
        if (byte > 127)
            break;
        // Printable ASCII can acquire a combining/variation suffix. Keep the
        // last byte before non-ASCII or an unfinished read for full segmentation.
        if (consumed + 1 < source_.size()) {
            if (static_cast<unsigned char>(source_[consumed + 1]) > 127)
                break;
        } else if (read_offset_ != end_) {
            break;
        }
        const std::size_t cells = terminal_ascii_cells(byte, static_cast<std::size_t>(column_ % 4));
        if (cells > std::numeric_limits<std::uint64_t>::max() - column_)
            throw std::runtime_error("Logical line display width exceeds the supported range.");
        const std::uint64_t following = column_ + cells;
        if (column_ < right_ && following > left_)
            break;
        const std::uint64_t source_start = offset_ + consumed;
        if (source_caret_) {
            if (*source_caret_ == source_start)
                frame_.source_caret_column = column_;
            else if (*source_caret_ == source_start + 1)
                frame_.source_caret_column = following;
        }
        column_ = following;
        ++consumed;
        if (column_ >= right_ && (!source_caret_ || frame_.source_caret_column))
            break;
    }
    offset_ += consumed;
    source_.erase(0, consumed);
}
void TerminalHorizontalLine::finish_preparation() {
    if (source_caret_ && *source_caret_ == end_ && offset_ == end_)
        frame_.source_caret_column = column_;
    complete_ = (column_ >= right_ && (!source_caret_ || frame_.source_caret_column)) || offset_ == end_;
    if (complete_) {
        if (offset_ == end_) {
            frame_.total_cells = column_;
            retain_caret(end_, column_);
        }
        frame_.clipped_left = left_ > 0 && column_ > 0;
        frame_.clipped_right = column_ > right_ || offset_ < end_;
    }
}
bool TerminalHorizontalLine::step(const Session &session, const std::size_t budget) {
    validate(session);
    if (!budget || budget > 8192)
        throw std::runtime_error("Horizontal viewport step budget must be 1..8192 bytes.");
    if (complete_)
        return true;
    try {
        const std::uint64_t remaining = end_ - read_offset_;
        const std::size_t room = maximum_page - source_.size();
        const std::size_t count = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, std::min(budget, room)));
        if (count) {
            const Page page = session.page(read_offset_, count);
            source_.append(page.bytes);
            read_offset_ = page.next;
        }
        prepare();
    } catch (...) {
        // A partially prepared frame is private and cannot be resumed after a
        // failure. The caller retains its previous published viewport.
        failed_ = true;
        throw;
    }
    return complete_;
}
const TerminalHorizontalFrame &TerminalHorizontalLine::result(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("Horizontal viewport is not complete.");
    return frame_;
}
TerminalHorizontalPage::TerminalHorizontalPage(const Session &session,
                                               const std::uint64_t start,
                                               const std::size_t rows,
                                               const std::uint64_t left,
                                               const std::size_t width)
    : stamp_(session.stamp()), size_(session.size()), left_(left), width_(width),
      logical_(session, start, rows) {
    if (!width || width > 1000 || left > std::numeric_limits<std::uint64_t>::max() - width)
        throw std::runtime_error("Horizontal viewport width must be 1..1000 without overflow.");
    frames_.reserve(rows);
}
TerminalHorizontalPage::TerminalHorizontalPage(const Session &session,
                                               const TerminalHorizontalPage &previous,
                                               const std::uint64_t left,
                                               const std::size_t width)
    : stamp_(session.stamp()), size_(session.size()), left_(left), width_(width),
      logical_(previous.logical_) {
    previous.require_complete(session);
    if (!width || width > 1000 || left > std::numeric_limits<std::uint64_t>::max() - width)
        throw std::runtime_error("Horizontal viewport width must be 1..1000 without overflow.");
    indexed_ = true;
    if (left == previous.left_ && width == previous.width_) {
        frames_ = previous.frames_;
        complete_ = true;
    } else {
        seed_frames_ = previous.frames_;
        frames_.reserve(seed_frames_.size());
    }
}
void TerminalHorizontalPage::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("Horizontal viewport belongs to an older document.");
    if (failed_)
        throw std::runtime_error("Horizontal viewport preparation previously failed.");
}
void TerminalHorizontalPage::require_complete(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("Horizontal viewport is not complete.");
}
bool TerminalHorizontalPage::step(const Session &session, const std::size_t budget) {
    validate(session);
    if (!budget || budget > 8192)
        throw std::runtime_error("Horizontal viewport step budget must be 1..8192 bytes.");
    if (complete_)
        return true;
    try {
        if (!indexed_) {
            indexed_ = logical_.step(session, budget);
            return false;
        }
        if (!line_) {
            line_ = std::make_unique<TerminalHorizontalLine>(
                session, logical_, frames_.size(), left_, width_);
            if (!seed_frames_.empty()) {
                const TerminalHorizontalFrame &seed = seed_frames_[frames_.size()];
                std::optional<TerminalHorizontalCaret> start{};
                for (const TerminalHorizontalCaret caret : seed.carets) {
                    if (caret.column <= left_ && (!start || caret.column > (*start).column))
                        start = caret;
                }
                if (start) {
                    // Only this completed same-source page grants authority to
                    // skip the prefix. A caller cannot inject raw seed offsets.
                    (*line_).offset_ = (*start).source_offset;
                    (*line_).read_offset_ = (*start).source_offset;
                    (*line_).column_ = (*start).column;
                }
            }
        }
        const bool ready = (*line_).step(session, budget);
        if (ready) {
            // Copy only the completed visible row, then release its scratch.
            frames_.push_back((*line_).result(session));
            line_.reset();
            complete_ = frames_.size() == logical_.result(session).size();
            if (complete_)
                seed_frames_.clear();
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
    return complete_;
}
const std::vector<TerminalHorizontalFrame> &TerminalHorizontalPage::result(const Session &session) const {
    require_complete(session);
    return frames_;
}
const std::vector<TerminalLogicalRow> &TerminalHorizontalPage::rows(const Session &session) const {
    require_complete(session);
    const std::vector<TerminalLogicalRow> &result = logical_.result(session);
    return result;
}
std::uint64_t TerminalHorizontalPage::next(const Session &session) const {
    require_complete(session);
    const std::uint64_t offset = logical_.next(session);
    return offset;
}
bool TerminalHorizontalPage::more(const Session &session) const {
    require_complete(session);
    const bool remaining = logical_.more(session);
    return remaining;
}
} // namespace swiftedit
