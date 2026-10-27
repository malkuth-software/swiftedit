#pragma once
#include "terminal_horizontal.hpp"

namespace swiftedit {
// Finds a caret on an earlier/later logical line at the preferred display
// column. Work is cooperative and independent of wrapped-row history.
class TerminalNoWrapMove final {
public:
    TerminalNoWrapMove(const Session &, std::uint64_t caret, std::uint64_t column,
                      bool down, std::size_t count);
    [[nodiscard]] bool step(const Session &);
    [[nodiscard]] std::uint64_t result(const Session &) const;
private:
    enum class Phase { boundary, navigate, target, measure, complete };
    void validate(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{}, column_{}, start_{}, target_{};
    std::size_t count_{};
    bool down_{}, failed_{};
    TerminalLineBoundary boundary_;
    std::unique_ptr<TerminalLogicalPrevious> previous_{};
    std::unique_ptr<TerminalLogicalPage> logical_{};
    std::unique_ptr<TerminalHorizontalLine> measurement_{};
    Phase phase_{Phase::boundary};
};
// Owns replacement preparation, not the currently displayed viewport. Dropping
// this task cancels it. A requested old top is retained only if it still exposes
// the caret; otherwise the caret's logical line becomes the top row.
class TerminalNoWrapReveal final {
public:
    TerminalNoWrapReveal(const Session &, std::uint64_t caret, std::uint64_t left,
                        std::size_t width, std::size_t rows,
                        std::optional<std::uint64_t> old_top = std::nullopt,
                        const TerminalNoWrapReveal *previous = nullptr);
    [[nodiscard]] bool step(const Session &);
    [[nodiscard]] const TerminalHorizontalPage &viewport(const Session &) const;
    [[nodiscard]] TerminalPageCaret caret(const Session &) const;
    [[nodiscard]] std::uint64_t left(const Session &) const;
    [[nodiscard]] std::uint64_t top(const Session &) const;

private:
    enum class Phase { boundary, logical, measure, viewport, complete };
    void validate(const Session &) const;
    void require_complete(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{}, source_caret_{}, left_{}, line_start_{}, top_{};
    std::size_t width_{}, rows_{};
    std::optional<std::uint64_t> old_top_{};
    TerminalPageCaret caret_{};
    TerminalLineBoundary boundary_;
    std::unique_ptr<TerminalLogicalPage> logical_{};
    std::unique_ptr<TerminalHorizontalLine> measurement_{};
    std::unique_ptr<TerminalHorizontalPage> viewport_{};
    Phase phase_{Phase::boundary};
    bool failed_{};
};
} // namespace swiftedit
