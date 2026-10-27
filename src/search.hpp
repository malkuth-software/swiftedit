#pragma once
#include "session.hpp"
#include <gui_forms/text.hpp>

namespace swiftedit {
struct SearchSlot {
    std::string literal{};
    bool wildcard{};
};
// Wildcards are flags on grapheme slots, never punctuation interpreted as syntax.
class SearchPattern {
public:
    explicit SearchPattern(std::string_view query);
    void toggle(std::size_t slot);
    const std::vector<SearchSlot> &slots() const { return slots_; }

private:
    std::vector<SearchSlot> slots_{};
};
struct SearchProgress {
    std::optional<SourceRange> match{};
    std::size_t next_grapheme{};
    bool complete{};
    std::size_t work{};
};
// Owns the immutable query and source snapshot. Each step charges both slot
// checks and literal byte comparisons, including a partly matched candidate.
class PatternScan {
public:
    PatternScan(std::string_view source, SearchPattern pattern, std::size_t byte_start,
                bool match_case);
    [[nodiscard]] SearchProgress step(std::size_t work_budget);
    void restart();
    void seek(std::size_t byte_start);
    std::string_view text() const { return source_.utf8(); }

private:
    gui_forms::TextStore source_{};
    SearchPattern pattern_;
    std::size_t candidate_{}, slot_{}, byte_{};
    bool match_case_{};
};
// Bounded synchronous slice. The owner may cancel between slices; the source
// must remain unchanged across slices (retain/check its revision externally).
[[nodiscard]] SearchProgress search_slice(const gui_forms::TextStore &, const SearchPattern &,
                                          std::size_t start_grapheme, std::size_t budget,
                                          bool match_case);
[[nodiscard]] std::optional<SourceRange> find_pattern(const gui_forms::TextStore &,
                                                      const SearchPattern &, std::size_t byte_start,
                                                      bool match_case);
struct PatternReplacement {
    std::string text{};
    std::size_t count{};
};
class ReplacementScan {
public:
    ReplacementScan(std::string_view, SearchPattern, std::string replacement, bool match_case,
                    std::size_t maximum_bytes);
    [[nodiscard]] bool step(std::size_t work_budget);
    [[nodiscard]] PatternReplacement take_result();

private:
    enum class Phase { search, unchanged, replacement, tail, complete, consumed };
    void append(std::string_view, std::size_t &, std::size_t end, std::size_t &budget);
    PatternScan scan_;
    std::string replacement_{};
    PatternReplacement result_{};
    SourceRange match_{};
    std::size_t copied_{}, inserted_{}, maximum_{};
    Phase phase_{Phase::search};
};
[[nodiscard]] PatternReplacement replace_pattern(const gui_forms::TextStore &,
                                                 const SearchPattern &,
                                                 std::string_view replacement, bool match_case,
                                                 std::size_t maximum_bytes);
} // namespace swiftedit
