#include "session_search.hpp"
#include <algorithm>
#include <stdexcept>
namespace swiftedit {
SessionSearch::SessionSearch(const Session &session, SearchPattern pattern,
                             const std::uint64_t start, const bool match_case)
    : stamp_(session.stamp()), size_(session.size()), start_(start),
      pattern_(std::move(pattern)), match_case_(match_case) {
    if (start_ > size_)
        throw std::runtime_error("Search start exceeds source size.");
    if (pattern_.slots().empty())
        throw std::runtime_error("Search pattern has no grapheme slots.");
    candidates_.resize(pattern_.slots().size());
    complete_ = start_ == size_;
}
void SessionSearch::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("Search belongs to an older document.");
}
void SessionSearch::load(const Session &session) {
    page_ = session.page(next_, context_);
    const bool eof = page_.next == size_;
    std::string metadata = page_.bytes;
    for (std::size_t index = 0; index < metadata.size();) {
        const std::size_t length = utf8_sequence_length(metadata, index);
        if (length) {
            index += length;
            continue;
        }
        const unsigned char first = static_cast<unsigned char>(metadata[index]);
        const std::size_t expected = first >= 0xc2 && first <= 0xdf ? 2
                                    : first >= 0xe0 && first <= 0xef ? 3
                                    : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!eof && expected && metadata.size() - index < expected) {
            metadata.resize(index);
            break;
        }
        // Metadata replacement preserves byte positions and forces an atomic
        // invalid-byte boundary. Literal comparisons still use original bytes.
        metadata[index] = '\x01';
        ++index;
    }
    parsed_ = std::make_unique<gui_forms::TextStore>(metadata);
    count_ = (*parsed_).grapheme_count().value();
    if (!eof && count_)
        --count_;
    index_ = 0;
    if (!eof && !count_) {
        if (context_ == maximum_page)
            throw std::runtime_error("A complete grapheme exceeds the search context limit. Source is unchanged.");
        context_ = std::min(maximum_page, context_ * 2);
        parsed_.reset();
        return;
    }
    context_ = 8192;
    if (!count_)
        complete_ = true;
}
static unsigned char search_fold(const unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        const unsigned char result = static_cast<unsigned char>(value + ('a' - 'A'));
        return result;
    }
    return value;
}
bool SessionSearch::step(const Session &session, const std::size_t work_budget) {
    validate(session);
    if (!work_budget || work_budget > 65536)
        throw std::runtime_error("Search work budget must be 1..65536 operations.");
    if (complete_)
        return true;
    if (!parsed_) {
        load(session);
        // Source acquisition and comparison are separate cancellation points.
        return complete_;
    }
    const bool finished = match_case_ ? compare<true>(work_budget) : compare<false>(work_budget);
    return finished;
}
template <bool MatchCase>
bool SessionSearch::compare(const std::size_t work_budget) {
    std::size_t work = 0;
    const std::vector<SearchSlot> &slots = pattern_.slots();
    while (index_ < count_ && work < work_budget) {
        if (!comparing_) {
            const gui_forms::Utf8Range range =
                (*parsed_).grapheme_range(gui_forms::GraphemeIndex(index_));
            first_ = range.start.value();
            length_ = range.end.value() - first_;
            slot_ = std::min(active_count_, slots.size() - 1);
            next_active_count_ = 0;
            byte_ = 0;
            comparing_ = true;
        }
        const std::uint64_t offset = page_.offset + first_;
        const bool eligible = slot_ ? candidates_[slot_ - 1].has_value() : offset >= start_;
        const SearchSlot &slot = slots[slot_];
        bool matched = false;
        bool finished = true;
        ++work;
        if (eligible) {
            if (slot.wildcard)
                matched = true;
            else if (slot.literal.size() == length_) {
                unsigned char actual = static_cast<unsigned char>(page_.bytes[first_ + byte_]);
                unsigned char expected = static_cast<unsigned char>(slot.literal[byte_]);
                if constexpr (!MatchCase) {
                    actual = search_fold(actual);
                    expected = search_fold(expected);
                }
                if (actual == expected) {
                    ++byte_;
                    matched = byte_ == length_;
                    finished = matched;
                }
            }
        }
        if (!finished)
            continue;
        if (matched) {
            const std::uint64_t candidate = slot_ ? *candidates_[slot_ - 1] : offset;
            candidates_[slot_] = candidate;
            next_active_count_ = std::max(next_active_count_, slot_ + 1);
            if (slot_ + 1 == slots.size()) {
                match_ = PagedSearchMatch{candidate, offset + length_ - candidate};
                complete_ = true;
                return true;
            }
        } else {
            candidates_[slot_].reset();
        }
        byte_ = 0;
        if (slot_)
            --slot_;
        else {
            comparing_ = false;
            active_count_ = next_active_count_;
            next_ = offset + length_;
            ++index_;
        }
    }
    if (index_ == count_) {
        parsed_.reset();
        complete_ = next_ == size_;
    }
    return complete_;
}
std::optional<PagedSearchMatch> SessionSearch::result(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("Search is not complete.");
    return match_;
}
} // namespace swiftedit
