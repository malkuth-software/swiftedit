#include "decorations.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace swiftedit::highlight {
namespace {

struct DecorationStartsBefore final {
    bool operator()(const Decoration &left, const Decoration &right) const noexcept {
        return left.range.start < right.range.start;
    }
};

struct DecorationStartsBeforeOffset final {
    bool operator()(const Decoration &decoration, ByteOffset offset) const noexcept {
        return decoration.range.start < offset;
    }
};

struct OffsetBeforeDecorationStart final {
    bool operator()(ByteOffset offset, const Decoration &decoration) const noexcept {
        return offset < decoration.range.start;
    }
};

struct OffsetBefore final {
    bool operator()(ByteOffset left, ByteOffset right) const noexcept { return left < right; }
};

struct DecorationIsEmpty final {
    bool operator()(const Decoration &decoration) const noexcept { return decoration.range.empty(); }
};

} // namespace

void DecorationLayer::replace(std::span<const Decoration> decorations) {
    for (const Decoration &decoration : decorations) {
        if (decoration.range.end < decoration.range.start)
            throw std::invalid_argument("A decoration ends before it starts.");
    }
    std::vector<Decoration> replacement{};
    replacement.reserve(decorations.size());
    for (const Decoration &decoration : decorations) {
        if (!decoration.range.empty())
            replacement.push_back(decoration);
    }
    std::stable_sort(replacement.begin(), replacement.end(), DecorationStartsBefore{});
    items_.swap(replacement);
}

void DecorationLayer::shift_for_edit(TextEdit edit) {
    const std::size_t old_end = edit.start.value + edit.removed;
    const std::size_t new_end = edit.start.value + edit.inserted;
    for (Decoration &decoration : items_) {
        std::size_t start = decoration.range.start.value;
        std::size_t end = decoration.range.end.value;
        const bool removed_entirely = start >= edit.start.value && end <= old_end && edit.removed > 0U;
        if (removed_entirely) {
            decoration.range.end = decoration.range.start;  // dropped below
            continue;
        }
        if (start >= old_end)
            start = start - old_end + new_end;
        else if (start > edit.start.value)
            start = edit.start.value;
        if (end >= old_end)
            end = end - old_end + new_end;
        else if (end > edit.start.value)
            end = new_end;
        decoration.range = ByteRange{ByteOffset{start}, ByteOffset{end}};
    }
    const std::vector<Decoration>::iterator kept =
        std::remove_if(items_.begin(), items_.end(), DecorationIsEmpty{});
    items_.erase(kept, items_.end());
    std::stable_sort(items_.begin(), items_.end(), DecorationStartsBefore{});
}

void DecorationLayer::append_overlapping(ByteRange window, std::vector<Decoration> &out) const {
    if (window.empty())
        return;
    // Decorations may overlap, so an earlier start can still reach the window;
    // only those starting at or after the window's end are certainly out.
    const std::vector<Decoration>::const_iterator last =
        std::lower_bound(items_.cbegin(), items_.cend(), window.end, DecorationStartsBeforeOffset{});
    for (std::vector<Decoration>::const_iterator item = items_.cbegin(); item != last; ++item) {
        if ((*item).range.end > window.start)
            out.push_back(*item);
    }
}

std::optional<std::size_t> DecorationLayer::next_after(ByteOffset offset) const noexcept {
    if (items_.empty())
        return std::nullopt;
    const std::vector<Decoration>::const_iterator after =
        std::upper_bound(items_.cbegin(), items_.cend(), offset, OffsetBeforeDecorationStart{});
    const std::size_t index = after == items_.cend() ? 0U : static_cast<std::size_t>(after - items_.cbegin());
    return index;
}

std::optional<std::size_t> DecorationLayer::previous_before(ByteOffset offset) const noexcept {
    if (items_.empty())
        return std::nullopt;
    const std::vector<Decoration>::const_iterator first_at =
        std::lower_bound(items_.cbegin(), items_.cend(), offset, DecorationStartsBeforeOffset{});
    const std::size_t index = first_at == items_.cbegin()
                                  ? items_.size() - 1U
                                  : static_cast<std::size_t>(first_at - items_.cbegin()) - 1U;
    return index;
}

void StyleComposer::compose(ByteRange window, std::span<const Token> tokens,
                            std::span<const DecorationLayer *const> layers,
                            std::vector<StyleRun> &runs) {
    runs.clear();
    if (window.start >= window.end)
        return;

    // Every decoration in the window, clipped, as opening and closing events.
    overlapping_.clear();
    for (const DecorationLayer *layer : layers)
        (*layer).append_overlapping(window, overlapping_);
    events_.clear();
    cuts_.clear();
    cuts_.push_back(window.start);
    cuts_.push_back(window.end);
    for (const Decoration &decoration : overlapping_) {
        const ByteOffset start = std::max(decoration.range.start, window.start);
        const ByteOffset end = std::min(decoration.range.end, window.end);
        events_.push_back(Event{start, decoration.kind, true});
        events_.push_back(Event{end, decoration.kind, false});
        cuts_.push_back(start);
        cuts_.push_back(end);
    }
    for (const Token &token : tokens) {
        if (token.range.start > window.start && token.range.start < window.end)
            cuts_.push_back(token.range.start);
        if (token.range.end > window.start && token.range.end < window.end)
            cuts_.push_back(token.range.end);
    }
    std::sort(cuts_.begin(), cuts_.end(), OffsetBefore{});
    const std::vector<ByteOffset>::iterator unique_end = std::unique(cuts_.begin(), cuts_.end());
    cuts_.erase(unique_end, cuts_.end());
    std::stable_sort(events_.begin(), events_.end(), EventBefore{});

    // Sweep the segments between cuts. Every event at or before a segment's
    // start is applied before the segment's mask is read; each decoration is
    // non-empty, so its opening precedes its closing and no count goes
    // negative.
    std::array<std::uint32_t, decoration_kind_count> active{};
    std::size_t next_event = 0U;
    std::size_t next_token = 0U;
    for (std::size_t cut = 0U; cut + 1U < cuts_.size(); ++cut) {
        const ByteOffset segment_start = cuts_[cut];
        const ByteOffset segment_end = cuts_[cut + 1U];
        while (next_event < events_.size() && events_[next_event].at <= segment_start) {
            const Event &event = events_[next_event];
            std::uint32_t &count = active[static_cast<std::size_t>(event.kind)];
            count = event.opening ? count + 1U : count - 1U;
            ++next_event;
        }
        while (next_token < tokens.size() && tokens[next_token].range.end <= segment_start)
            ++next_token;
        TokenKind syntax = TokenKind::plain;
        if (next_token < tokens.size() && tokens[next_token].range.start <= segment_start)
            syntax = tokens[next_token].kind;
        DecorationMask mask{};
        for (std::size_t kind = 0U; kind < decoration_kind_count; ++kind) {
            if (active[kind] > 0U)
                mask.add(static_cast<DecorationKind>(kind));
        }
        const bool merges = !runs.empty() && runs.back().syntax == syntax &&
                            runs.back().decorations == mask && runs.back().range.end == segment_start;
        if (merges) {
            runs.back().range.end = segment_end;
            continue;
        }
        const StyleRun run{{segment_start, segment_end}, syntax, mask};
        runs.push_back(run);
    }
}

} // namespace swiftedit::highlight
