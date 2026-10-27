#pragma once
#include "search.hpp"
namespace swiftedit {
struct PagedSearchMatch {
    std::uint64_t offset{}, length{};
};
// Streaming fixed-grapheme pattern search. Owns only a bounded source window
// and one partial candidate per pattern slot. Dropping the task cancels it.
// Reads/parses at most one 64 KiB context per step; comparison work is charged
// separately. A grapheme beyond that context ceiling is explicitly refused.
class SessionSearch final {
public:
    SessionSearch(const Session &, SearchPattern, std::uint64_t start = 0,
                  bool match_case = false);
    [[nodiscard]] bool step(const Session &, std::size_t work_budget = 4096);
    [[nodiscard]] std::optional<PagedSearchMatch> result(const Session &) const;
    std::uint64_t offset() const { return next_; }
    std::uint64_t size() const { return size_; }
private:
    void validate(const Session &) const;
    void load(const Session &);
    template <bool MatchCase> bool compare(std::size_t work_budget);
    DocumentStamp stamp_{};
    std::uint64_t size_{}, start_{}, next_{};
    SearchPattern pattern_;
    bool match_case_{}, complete_{};
    std::optional<PagedSearchMatch> match_{};
    std::vector<std::optional<std::uint64_t>> candidates_{};
    Page page_{};
    std::unique_ptr<gui_forms::TextStore> parsed_{};
    std::size_t count_{}, index_{}, context_{8192};
    std::size_t slot_{}, byte_{}, first_{}, length_{};
    std::size_t active_count_{}, next_active_count_{};
    bool comparing_{};
};
} // namespace swiftedit
