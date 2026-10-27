#include "selection_set.hpp"
#include <gui_forms/text.hpp>
#include <stdexcept>

namespace swiftedit {
namespace gf = gui_forms;
namespace {
bool ascii_context(const std::string_view context) {
    for (const unsigned char byte : context) {
        if (byte >= 128) return false;
    }
    return true;
}
std::size_t ascii_graphemes(const std::string_view context, const SourceRange range) {
    const std::size_t end = range.offset + range.length;
    if ((range.offset && range.offset < context.size() && context[range.offset - 1] == '\r' && context[range.offset] == '\n') ||
        (end && end < context.size() && context[end - 1] == '\r' && context[end] == '\n'))
        throw std::runtime_error("Selection splits a source grapheme or line ending.");
    std::size_t count = range.length;
    for (std::size_t offset = range.offset; offset < end; ++offset) {
        if (context[offset] == '\r' && offset + 1 < end && context[offset + 1] == '\n') {
            --count;
            ++offset;
        }
    }
    return count;
}
std::vector<SourceRange> map_selections(const Session &session, const gf::DocumentViewState &view,
                                      const gf::DocumentPageRequest &token,
                                      const std::vector<DisplaySelection> &selections) {
    const DocumentStamp current = session.stamp();
    if (token.revision.document != current.identity.value || token.revision.revision != current.revision.value)
        throw std::runtime_error("Display selections belong to an older or different document.");
    if (session.read_only())
        throw std::runtime_error("Interactive multi-selection metadata requires an editable document.");
    if (selections.empty() || selections.size() > 1000)
        throw std::runtime_error("Select between 1 and 1000 display ranges.");
    std::vector<SourceRange> ranges{};
    ranges.reserve(selections.size());
    for (const DisplaySelection &selection : selections) {
        if (selection.begin.value > selection.end.value)
            throw std::runtime_error("Display selection endpoints must be ordered.");
        const gf::SourceMappingResult begin = view.source_position(token, selection.begin);
        const gf::SourceMappingResult end = view.source_position(token, selection.end);
        if (begin.status != gf::DocumentViewStatus::success || end.status != gf::DocumentViewStatus::success ||
            !begin.position || !end.position)
            throw std::runtime_error("Display selection is stale or splits a character or control label.");
        const std::uint64_t first = (*begin.position).value;
        const std::uint64_t last = (*end.position).value;
        if (first > last || last > session.size())
            throw std::runtime_error("Mapped selection exceeds document.");
        // Editable Session size is below 16 MiB, so checked endpoints fit size_t.
        ranges.push_back({static_cast<std::size_t>(first), static_cast<std::size_t>(last - first)});
    }
    return ranges;
}
}
SelectionSet::SelectionSet(const Session &session, const gf::DocumentViewState &view,
                           const gf::DocumentPageRequest &token,
                           const std::vector<DisplaySelection> &selections)
    : SelectionSet(session, map_selections(session, view, token, selections)) {}
SelectionSet::SelectionSet(const Session &session, const std::vector<SourceRange> &ranges)
    : stamp_(session.stamp()) {
    if (session.read_only())
        throw std::runtime_error("Interactive multi-selection metadata requires an editable document.");
    if (ranges.empty() || ranges.size() > 1000)
        throw std::runtime_error("Select between 1 and 1000 source ranges.");
    const std::string &source = session.text();
    std::size_t previous_offset = 0, previous_end = 0, previous_length = 0;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const SourceRange &range = ranges[index];
        if (range.offset > source.size() || range.length > source.size() - range.offset)
            throw std::runtime_error("Selection range exceeds document.");
        if (index && (range.offset < previous_end || range.offset == previous_offset ||
                      (range.offset == previous_end && (!range.length || !previous_length))))
            throw std::runtime_error("Selections must be ordered and disjoint.");
        previous_offset = range.offset;
        previous_end = range.offset + range.length;
        previous_length = range.length;
    }
    // LF ends all grapheme context, including CRLF and regional-indicator
    // parity. Keep complete LF-delimited context around the selected interval;
    // never cut at a guessed scalar or a fixed look-behind distance.
    std::size_t first_count = 0;
    std::string metadata{};
    for (std::size_t group = 0; group < ranges.size();) {
        std::size_t context_begin = 0;
        if (ranges[group].offset) {
            const std::size_t previous_lf = source.rfind('\n', ranges[group].offset - 1);
            if (previous_lf != std::string::npos)
                context_begin = previous_lf + 1;
        }
        const std::size_t selected_end = ranges[group].offset + ranges[group].length;
        const std::size_t next_lf = source.find('\n', selected_end);
        std::size_t context_end = next_lf == std::string::npos ? source.size() : next_lf + 1;
        std::size_t after_group = group + 1;
        // Merge overlapping contexts so selections on one long line do not
        // repeatedly scan/segment it. Unselected intervening lines stay out.
        while (after_group < ranges.size() && ranges[after_group].offset <= context_end) {
            const SourceRange &next = ranges[after_group];
            const std::size_t end = next.offset + next.length;
            if (end > context_end) {
                const std::size_t following_lf = source.find('\n', end);
                context_end = following_lf == std::string::npos ? source.size() : following_lf + 1;
            }
            ++after_group;
        }
        const std::string_view context(source.data() + context_begin, context_end - context_begin);
        // With complete ASCII context, each byte is one grapheme except CRLF.
        // Inspect the entire context before taking this path: a preceding
        // non-ASCII Prepend or a following combining scalar changes legality.
        if (ascii_context(context)) {
            for (std::size_t index = group; index < after_group; ++index) {
                const SourceRange &range = ranges[index];
                const std::size_t count = ascii_graphemes(context, {range.offset - context_begin, range.length});
                if (index == 0)
                    first_count = count;
                else if (count != first_count)
                    equal_lengths_ = false;
            }
            group = after_group;
            continue;
        }
        // Illegal-byte sentinels force grapheme breaks without changing byte
        // lengths. Metadata is never copied into source or clipboard output.
        metadata.assign(source, context_begin, context_end - context_begin);
        for (std::size_t offset = 0; offset < metadata.size();) {
            const std::size_t length = utf8_sequence_length(metadata, offset);
            if (length)
                offset += length;
            else {
                metadata[offset] = '\x01';
                ++offset;
            }
        }
        const gf::TextStore text(metadata);
        for (std::size_t index = group; index < after_group; ++index) {
            const SourceRange &range = ranges[index];
            const gf::Utf8Offset start(range.offset - context_begin);
            const gf::Utf8Offset end(range.offset + range.length - context_begin);
            if (!text.is_grapheme_boundary(start) || !text.is_grapheme_boundary(end))
                throw std::runtime_error("Selection splits a source grapheme or line ending.");
            const std::size_t count = text.grapheme_index(end).value() - text.grapheme_index(start).value();
            if (index == 0)
                first_count = count;
            else if (count != first_count)
                equal_lengths_ = false;
        }
        group = after_group;
    }
    ranges_ = ranges;
}
void SelectionSet::require_current(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision)
        throw std::runtime_error("Selections belong to an older or different document.");
}
std::vector<SourceClipboard> SelectionSet::copy(const Session &session) const {
    require_current(session);
    std::vector<SourceClipboard> result{};
    result.reserve(ranges_.size());
    for (const SourceRange &range : ranges_)
        result.push_back(session.copy_range(range, stamp_));
    return result;
}
void SelectionSet::rewrite(Session &session, const std::string_view replacement) const {
    require_current(session);
    if (!equal_lengths_)
        throw std::runtime_error("Unequal-length selections are copy-only.");
    session.replace_ranges(ranges_, replacement, stamp_);
}
} // namespace swiftedit
