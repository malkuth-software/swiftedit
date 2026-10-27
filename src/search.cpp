#include "search.hpp"
#include <stdexcept>

namespace swiftedit {
SearchPattern::SearchPattern(std::string_view query) {
    if (query.empty() || query.size() > 4096)
        throw std::runtime_error("Search text must contain 1..4096 UTF-8 bytes.");
    const gui_forms::TextStore parsed(query);
    const std::size_t count = parsed.grapheme_count().value();
    slots_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const gui_forms::Utf8Range range = parsed.grapheme_range(gui_forms::GraphemeIndex(index));
        const std::size_t length = range.end.value() - range.start.value();
        const std::string_view bytes = query.substr(range.start.value(), length);
        SearchSlot slot{std::string(bytes), false};
        slots_.push_back(std::move(slot));
    }
}
void SearchPattern::toggle(std::size_t slot) {
    if (slot >= slots_.size())
        throw std::runtime_error("Wildcard slot exceeds search pattern.");
    slots_[slot].wildcard = !slots_[slot].wildcard;
}
namespace {
std::string_view checked_source(std::string_view source) {
    if (source.size() >= editable_limit)
        throw std::runtime_error("Search snapshot exceeds editable document limit.");
    return source;
}
unsigned char fold(unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        const unsigned char folded = static_cast<unsigned char>(value + ('a' - 'A'));
        return folded;
    }
    return value;
}
bool equal_text(std::string_view left, std::string_view right, bool match_case) {
    if (match_case) {
        const bool equal = left == right;
        return equal;
    }
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const unsigned char a = fold(static_cast<unsigned char>(left[index]));
        const unsigned char b = fold(static_cast<unsigned char>(right[index]));
        if (a != b)
            return false;
    }
    return true;
}
} // namespace
PatternScan::PatternScan(std::string_view source, SearchPattern pattern, std::size_t byte_start,
                         bool match_case)
    : source_(checked_source(source)), pattern_(std::move(pattern)), match_case_(match_case) {
    candidate_ = source_.grapheme_index(gui_forms::Utf8Offset(byte_start)).value();
}
void PatternScan::restart() { seek(0); }
void PatternScan::seek(std::size_t byte_start) {
    const std::size_t next = source_.grapheme_index(gui_forms::Utf8Offset(byte_start)).value();
    candidate_ = next;
    slot_ = 0;
    byte_ = 0;
}
SearchProgress PatternScan::step(std::size_t work_budget) {
    if (!work_budget || work_budget > 65536)
        throw std::runtime_error("Search work budget must be 1..65536 operations.");
    const std::vector<SearchSlot> &slots = pattern_.slots();
    const std::size_t count = source_.grapheme_count().value();
    SearchProgress result{};
    std::size_t work = 0;
    while (slots.size() <= count && candidate_ <= count - slots.size() && work < work_budget) {
        const SearchSlot &slot = slots[slot_];
        bool mismatch = false;
        ++work;
        if (slot.wildcard) {
            ++slot_;
        } else {
            const gui_forms::Utf8Range range =
                source_.grapheme_range(gui_forms::GraphemeIndex(candidate_ + slot_));
            const std::size_t length = range.end.value() - range.start.value();
            if (length != slot.literal.size()) {
                mismatch = true;
            } else {
                unsigned char actual =
                    static_cast<unsigned char>(source_.utf8()[range.start.value() + byte_]);
                unsigned char expected = static_cast<unsigned char>(slot.literal[byte_]);
                if (!match_case_) {
                    actual = fold(actual);
                    expected = fold(expected);
                }
                mismatch = actual != expected;
                ++byte_;
                if (byte_ == length) {
                    byte_ = 0;
                    ++slot_;
                }
            }
        }
        if (mismatch) {
            ++candidate_;
            slot_ = 0;
            byte_ = 0;
        } else if (slot_ == slots.size()) {
            const gui_forms::Utf8Offset first =
                source_.utf8_offset(gui_forms::GraphemeIndex(candidate_));
            const gui_forms::Utf8Offset end =
                source_.utf8_offset(gui_forms::GraphemeIndex(candidate_ + slots.size()));
            result.match = SourceRange{first.value(), end.value() - first.value()};
            ++candidate_;
            slot_ = 0;
            byte_ = 0;
            break;
        }
    }
    result.next_grapheme = candidate_;
    result.complete = slots.size() > count || candidate_ > count - slots.size();
    result.work = work;
    return result;
}
ReplacementScan::ReplacementScan(std::string_view source, SearchPattern pattern,
                                 std::string replacement, bool match_case,
                                 std::size_t maximum_bytes)
    : scan_(source, std::move(pattern), 0, match_case), replacement_(std::move(replacement)),
      maximum_(maximum_bytes) {
    if (maximum_ >= editable_limit)
        throw std::runtime_error("Replacement output budget must remain below 16 MiB.");
    const gui_forms::TextStore validated(replacement_);
    static_cast<void>(validated);
    // Reserve once so incremental copies do not trigger an ever larger copy.
    result_.text.reserve(maximum_);
}
void ReplacementScan::append(std::string_view text, std::size_t &offset, std::size_t end,
                             std::size_t &budget) {
    const std::size_t count = std::min(end - offset, budget);
    if (count > maximum_ - result_.text.size())
        throw std::runtime_error("Replacement exceeds document byte limit.");
    result_.text.append(text.substr(offset, count));
    offset += count;
    budget -= count;
}
bool ReplacementScan::step(std::size_t budget) {
    if (!budget || budget > 65536)
        throw std::runtime_error("Replacement work budget must be 1..65536 operations.");
    if (phase_ == Phase::consumed)
        throw std::runtime_error("Replacement result was already consumed.");
    while (budget && phase_ != Phase::complete) {
        if (phase_ == Phase::search) {
            const SearchProgress progress = scan_.step(budget);
            budget -= progress.work;
            if (progress.match) {
                match_ = *progress.match;
                phase_ = Phase::unchanged;
            } else if (progress.complete)
                phase_ = Phase::tail;
        } else if (phase_ == Phase::unchanged) {
            append(scan_.text(), copied_, match_.offset, budget);
            if (copied_ == match_.offset) {
                inserted_ = 0;
                phase_ = Phase::replacement;
            }
        } else if (phase_ == Phase::replacement) {
            append(replacement_, inserted_, replacement_.size(), budget);
            if (inserted_ == replacement_.size()) {
                copied_ = match_.offset + match_.length;
                scan_.seek(copied_);
                ++result_.count;
                phase_ = Phase::search;
            }
        } else if (phase_ == Phase::tail) {
            append(scan_.text(), copied_, scan_.text().size(), budget);
            if (copied_ == scan_.text().size())
                phase_ = Phase::complete;
        }
    }
    const bool complete = phase_ == Phase::complete;
    return complete;
}
PatternReplacement ReplacementScan::take_result() {
    if (phase_ != Phase::complete)
        throw std::runtime_error("Replacement is not complete or was already consumed.");
    PatternReplacement result = std::move(result_);
    phase_ = Phase::consumed;
    return result;
}
SearchProgress search_slice(const gui_forms::TextStore &source, const SearchPattern &pattern,
                            std::size_t start_grapheme, std::size_t budget, bool match_case) {
    if (!budget || budget > 65536)
        throw std::runtime_error("Search slice budget must be 1..65536 candidates.");
    const std::size_t count = source.grapheme_count().value();
    if (start_grapheme > count)
        throw std::runtime_error("Search cursor exceeds document.");
    const std::vector<SearchSlot> &slots = pattern.slots();
    SearchProgress result{{}, start_grapheme, false};
    if (slots.size() > count || start_grapheme > count - slots.size()) {
        result.complete = true;
        return result;
    }
    const std::size_t last_candidate = count - slots.size();
    const std::string_view text = source.utf8();
    std::size_t checked = 0;
    while (result.next_grapheme <= last_candidate && checked < budget) {
        const std::size_t candidate = result.next_grapheme;
        ++result.next_grapheme;
        ++checked;
        bool matches = true;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (slots[index].wildcard)
                continue;
            const gui_forms::Utf8Range range =
                source.grapheme_range(gui_forms::GraphemeIndex(candidate + index));
            const std::size_t length = range.end.value() - range.start.value();
            const std::string_view value = text.substr(range.start.value(), length);
            const bool equal = equal_text(value, slots[index].literal, match_case);
            if (!equal) {
                matches = false;
                break;
            }
        }
        if (matches) {
            const gui_forms::Utf8Offset first =
                source.utf8_offset(gui_forms::GraphemeIndex(candidate));
            const gui_forms::Utf8Offset end =
                source.utf8_offset(gui_forms::GraphemeIndex(candidate + slots.size()));
            result.match = SourceRange{first.value(), end.value() - first.value()};
            return result;
        }
    }
    result.complete = result.next_grapheme > last_candidate;
    return result;
}
std::optional<SourceRange> find_pattern(const gui_forms::TextStore &source,
                                        const SearchPattern &pattern, std::size_t byte_start,
                                        bool match_case) {
    const gui_forms::Utf8Offset start(byte_start);
    std::size_t next = source.grapheme_index(start).value();
    for (;;) {
        const SearchProgress progress = search_slice(source, pattern, next, 64, match_case);
        if (progress.match || progress.complete)
            return progress.match;
        next = progress.next_grapheme;
    }
}
PatternReplacement replace_pattern(const gui_forms::TextStore &source, const SearchPattern &pattern,
                                   std::string_view replacement, bool match_case,
                                   std::size_t maximum_bytes) {
    const gui_forms::TextStore validated(replacement);
    static_cast<void>(validated);
    PatternReplacement result{};
    const std::string_view text = source.utf8();
    std::size_t copied = 0;
    while (copied < text.size()) {
        const std::optional<SourceRange> match = find_pattern(source, pattern, copied, match_case);
        if (!match)
            break;
        const std::size_t unchanged = (*match).offset - copied;
        if (unchanged > maximum_bytes - result.text.size() ||
            replacement.size() > maximum_bytes - result.text.size() - unchanged)
            throw std::runtime_error("Replacement exceeds document byte limit.");
        result.text.append(text.substr(copied, unchanged));
        result.text.append(replacement);
        copied = (*match).offset + (*match).length;
        ++result.count;
    }
    if (text.size() - copied > maximum_bytes - result.text.size())
        throw std::runtime_error("Replacement exceeds document byte limit.");
    result.text.append(text.substr(copied));
    return result;
}
} // namespace swiftedit
