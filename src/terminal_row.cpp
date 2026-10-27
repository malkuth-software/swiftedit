#include "terminal_row.hpp"
#include "terminal_wrap.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace swiftedit {
namespace {
// A printable ASCII byte is a complete one-cell grapheme only when the next
// source byte cannot extend it. A following non-ASCII combining mark must still
// go through the Unicode navigation metadata.
bool single_ascii_grapheme(const std::string &source, std::size_t offset, std::size_t end) {
    const unsigned char byte = static_cast<unsigned char>(source[offset]);
    const bool printable = byte >= 0x20 && byte <= 0x7e;
    const bool separate =
        offset + 1 == end || static_cast<unsigned char>(source[offset + 1]) < 0x80;
    const bool single = printable && separate;
    return single;
}
void validate_view(std::size_t first_column, std::size_t width) {
    if (!width || width > 1000 || first_column > std::numeric_limits<std::size_t>::max() - width)
        throw std::runtime_error("Terminal row width must be 1..1000 cells without overflow.");
}
TerminalRow render_row(TerminalBuffer &buffer, SourceRange range, std::size_t first_column,
                       std::size_t width, std::size_t start_offset, std::size_t start_column,
                       std::optional<std::size_t> known_cells) {
    const TerminalSelection selection = buffer.selection();
    const SourceRange selected = buffer.selected_range();
    const std::string &source = buffer.session().text();
    TerminalRow result{};
    result.runs.reserve(width);
    std::size_t column = start_column;
    const std::size_t visible_end = first_column + width;
    for (std::size_t offset = start_offset; offset < range.offset + range.length;) {
        if (known_cells && column >= visible_end)
            break;
        const SourceRange grapheme = buffer.grapheme_range(offset);
        const std::string_view bytes(source.data() + grapheme.offset, grapheme.length);
        TerminalGlyph glyph = terminal_glyph(bytes, column);
        if (selection.caret == offset && column >= first_column && column < visible_end)
            result.caret_column = column - first_column;
        const std::size_t end = column + glyph.cells;
        if (column < visible_end && end > first_column) {
            const std::size_t visible_start = std::max(column, first_column);
            const std::size_t visible_stop = std::min(end, visible_end);
            TerminalRun run{};
            run.column = visible_start - first_column;
            run.cells = visible_stop - visible_start;
            run.source = grapheme;
            run.selected = selected.length && offset < selected.offset + selected.length &&
                           offset + grapheme.length > selected.offset;
            if (visible_start == column && visible_stop == end)
                run.text = std::move(glyph.text);
            else if (glyph.label || bytes == "\t")
                run.text = glyph.text.substr(visible_start - column, run.cells);
            else
                run.text.assign(run.cells, ' ');
            result.runs.push_back(std::move(run));
        }
        column = end;
        offset += grapheme.length;
    }
    if (known_cells)
        column = *known_cells;
    if (selection.caret == range.offset + range.length && column >= first_column &&
        column < visible_end)
        result.caret_column = column - first_column;
    result.total_cells = column;
    result.clipped_left = first_column > 0 && column > 0;
    result.clipped_right = column > visible_end;
    return result;
}
} // namespace
TerminalWrapSpan terminal_wrap_span(TerminalBuffer &buffer, std::size_t line, std::size_t start,
                                    std::size_t width) {
    validate_view(0, width);
    const SourceRange logical = buffer.line_range(line);
    const std::size_t end = logical.offset + logical.length;
    if (start < logical.offset || start > end)
        throw std::runtime_error("Wrapped row start is outside its logical line.");
    const std::string &source = buffer.session().text();
    std::size_t offset = start, cells = 0;
    std::optional<std::size_t> word_break{};
    while (offset < end) {
        const SourceRange grapheme = buffer.grapheme_range(offset);
        const std::string_view bytes(source.data() + offset, grapheme.length);
        const TerminalGlyph glyph = terminal_glyph(bytes, cells);
        if (glyph.cells > width - cells) {
            if (offset == start) {
                // A wide grapheme or inert label in a narrower viewport still
                // advances atomically. The painter clips its display, not source.
                offset += grapheme.length;
            } else if (word_break) {
                offset = *word_break;
            }
            const TerminalWrapSpan result{{start, offset - start}, false};
            return result;
        }
        cells += glyph.cells;
        offset += grapheme.length;
        if (bytes == " " || bytes == "\t")
            word_break = offset;
        if (cells == width) {
            if (offset < end && word_break)
                offset = *word_break;
            const TerminalWrapSpan result{{start, offset - start}, false};
            return result;
        }
    }
    const TerminalWrapSpan result{{start, offset - start}, true};
    return result;
}
TerminalWrappedRow terminal_wrapped_row(TerminalBuffer &buffer, std::size_t line, std::size_t start,
                                        std::size_t width) {
    TerminalWrappedRow result{};
    result.span = terminal_wrap_span(buffer, line, start, width);
    const SourceRange range = result.span.source;
    result.display = render_row(buffer, range, 0, width, range.offset, 0, {});
    if (!result.span.logical_end && buffer.selection().caret == range.offset + range.length)
        result.display.caret_column.reset();
    return result;
}
std::size_t terminal_wrap_source(TerminalBuffer &buffer, std::size_t line, std::size_t start,
                                 std::size_t width, std::size_t column) {
    const TerminalWrapSpan span = terminal_wrap_span(buffer, line, start, width);
    const std::string &source = buffer.session().text();
    const std::size_t end = span.source.offset + span.source.length;
    std::size_t offset = start, cells = 0, last = start;
    while (offset < end) {
        const SourceRange grapheme = buffer.grapheme_range(offset);
        const std::string_view bytes(source.data() + offset, grapheme.length);
        const TerminalGlyph glyph = terminal_glyph(bytes, cells);
        if (column < cells + glyph.cells)
            return offset;
        last = offset;
        cells += glyph.cells;
        offset += grapheme.length;
    }
    const std::size_t result = span.logical_end ? end : last;
    return result;
}
TerminalRow terminal_row(TerminalBuffer &buffer, std::size_t line, std::size_t first_column,
                         std::size_t width) {
    validate_view(first_column, width);
    const SourceRange range = buffer.line_range(line);
    TerminalRow result = render_row(buffer, range, first_column, width, range.offset, 0, {});
    return result;
}
TerminalRowCache::Entry &TerminalRowCache::prepare(TerminalBuffer &buffer, std::size_t line) {
    const SourceRange range = buffer.line_range(line);
    const DocumentStamp current = buffer.session().stamp();
    if (stamp_.identity != current.identity || stamp_.revision != current.revision) {
        entries_.clear();
        next_eviction_ = 0;
        stamp_ = current;
    }
    for (Entry &entry : entries_) {
        if (entry.line == line)
            return entry;
    }
    Entry prepared{};
    prepared.line = line;
    prepared.source = range;
    prepared.points.reserve(range.length / 256 + 1);
    std::size_t column = 0;
    std::size_t count = 0;
    const std::string &source = buffer.session().text();
    for (std::size_t offset = range.offset; offset < range.offset + range.length;) {
        if (count % 256 == 0)
            prepared.points.push_back({offset, column});
        if (single_ascii_grapheme(source, offset, range.offset + range.length)) {
            ++column;
            ++offset;
            ++count;
            continue;
        }
        const SourceRange grapheme = buffer.grapheme_range(offset);
        const std::string_view bytes(source.data() + offset, grapheme.length);
        const TerminalGlyph glyph = terminal_glyph(bytes, column);
        column += glyph.cells;
        offset += grapheme.length;
        ++count;
    }
    if (prepared.points.empty())
        prepared.points.push_back({range.offset, 0});
    prepared.cells = column;
    if (entries_.size() < 320) {
        entries_.push_back(std::move(prepared));
        return entries_.back();
    }
    const std::size_t destination = next_eviction_;
    entries_[destination] = std::move(prepared);
    next_eviction_ = (next_eviction_ + 1) % 320;
    return entries_[destination];
}
TerminalRow TerminalRowCache::row(TerminalBuffer &buffer, std::size_t line,
                                  std::size_t first_column, std::size_t width) {
    validate_view(first_column, width);
    const Entry &entry = prepare(buffer, line);
    std::size_t first = 0;
    std::size_t last = entry.points.size();
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (entry.points[middle].column <= first_column)
            first = middle + 1;
        else
            last = middle;
    }
    const Point &point = entry.points[first ? first - 1 : 0];
    TerminalRow result = render_row(buffer, entry.source, first_column, width, point.offset,
                                    point.column, entry.cells);
    return result;
}
std::size_t TerminalRowCache::source_column(TerminalBuffer &buffer, std::size_t line,
                                            std::size_t source_offset) {
    const Entry &entry = prepare(buffer, line);
    if (source_offset < entry.source.offset ||
        source_offset > entry.source.offset + entry.source.length)
        throw std::runtime_error("Caret source offset is outside the line.");
    std::size_t first = 0;
    std::size_t last = entry.points.size();
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (entry.points[middle].offset <= source_offset)
            first = middle + 1;
        else
            last = middle;
    }
    const Point &point = entry.points[first ? first - 1 : 0];
    std::size_t column = point.column;
    const std::string &source = buffer.session().text();
    for (std::size_t offset = point.offset; offset < source_offset;) {
        const SourceRange grapheme = buffer.grapheme_range(offset);
        if (grapheme.length > source_offset - offset)
            throw std::runtime_error("Caret source offset splits a grapheme.");
        const std::string_view bytes(source.data() + offset, grapheme.length);
        const TerminalGlyph glyph = terminal_glyph(bytes, column);
        column += glyph.cells;
        offset += grapheme.length;
    }
    return column;
}
} // namespace swiftedit
