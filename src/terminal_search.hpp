#pragma once
#include "search.hpp"
#include "session_search.hpp"
#include "terminal_buffer.hpp"
namespace swiftedit {
enum class TerminalSearchState { idle, pending, found, not_found, cancelled };
class TerminalSearch {
public:
    void begin(TerminalBuffer &, SearchPattern, bool match_case = false, std::uint64_t paged_start = 0);
    TerminalSearchState step(TerminalBuffer &, std::size_t budget = 4096);
    void cancel();
    TerminalSearchState state() const { return state_; }
    const std::optional<PagedSearchMatch> &paged_match() const { return paged_match_; }

private:
    std::unique_ptr<PatternScan> scan_{};
    std::unique_ptr<SessionSearch> paged_{};
    std::optional<SearchPattern> paged_pattern_{};
    std::optional<PagedSearchMatch> paged_match_{};
    std::uint64_t paged_start_{};
    bool paged_match_case_{};
    DocumentStamp stamp_{};
    TerminalSelection selection_{};
    std::size_t start_{};
    bool wrapped_{};
    TerminalSearchState state_{TerminalSearchState::idle};
};
enum class TerminalReplaceState { idle, pending, complete, cancelled };
class TerminalReplace {
public:
    void begin(TerminalBuffer &, SearchPattern, std::string replacement, bool match_case = false);
    TerminalReplaceState step(TerminalBuffer &, std::size_t budget = 4096);
    void cancel();
    TerminalReplaceState state() const { return state_; }
    std::size_t count() const { return count_; }

private:
    std::unique_ptr<SessionReplacement> scan_{};
    DocumentStamp stamp_{};
    TerminalSelection selection_{};
    TerminalReplaceState state_{TerminalReplaceState::idle};
    std::size_t count_{};
};
} // namespace swiftedit
