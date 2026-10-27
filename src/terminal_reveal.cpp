#include "terminal_reveal.hpp"
#include <limits>
#include <stdexcept>

namespace swiftedit {
TerminalNoWrapMove::TerminalNoWrapMove(const Session &session, const std::uint64_t caret,
                                     const std::uint64_t column, const bool down,
                                     const std::size_t count)
    : stamp_(session.stamp()), size_(session.size()), column_(column), count_(count),
      down_(down), boundary_(session, caret, false) {
    if (!count || count > 300 || column == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("No-wrap movement bounds are invalid.");
}
void TerminalNoWrapMove::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_ || failed_)
        throw std::runtime_error("No-wrap movement is stale or failed.");
}
bool TerminalNoWrapMove::step(const Session &session) {
    validate(session);
    try {
        switch (phase_) {
        case Phase::boundary:
            if (boundary_.step(session)) {
                start_ = boundary_.result(session);
                phase_ = Phase::navigate;
            }
            break;
        case Phase::navigate:
            if (down_) {
                if (!logical_) {
                    logical_ = std::make_unique<TerminalLogicalPage>(session, start_, count_);
                } else if ((*logical_).step(session)) {
                    const std::vector<TerminalLogicalRow> &rows = (*logical_).result(session);
                    start_ = (*logical_).more(session) ? (*logical_).next(session)
                        : rows.back().offset;
                    logical_.reset();
                    phase_ = Phase::target;
                }
            } else {
                if (!previous_) {
                    previous_ = std::make_unique<TerminalLogicalPrevious>(session, start_, count_);
                } else if ((*previous_).step(session)) {
                    start_ = (*previous_).result(session);
                    previous_.reset();
                    phase_ = Phase::target;
                }
            }
            break;
        case Phase::target:
            if (!logical_) {
                logical_ = std::make_unique<TerminalLogicalPage>(session, start_, 1);
            } else if ((*logical_).step(session)) {
                phase_ = Phase::measure;
            }
            break;
        case Phase::measure:
            if (!measurement_)
                measurement_ = std::make_unique<TerminalHorizontalLine>(session, *logical_, 0, column_, 1);
            if ((*measurement_).step(session)) {
                const TerminalHorizontalFrame &frame = (*measurement_).result(session);
                std::optional<TerminalHorizontalCaret> best{};
                for (const TerminalHorizontalCaret caret : frame.carets) {
                    if (caret.column <= column_ && (!best || caret.column >= (*best).column))
                        best = caret;
                }
                if (!best)
                    throw std::runtime_error("Logical movement found no legal caret boundary.");
                target_ = (*best).source_offset;
                measurement_.reset();
                logical_.reset();
                phase_ = Phase::complete;
            }
            break;
        case Phase::complete:
            break;
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
    const bool complete = phase_ == Phase::complete;
    return complete;
}
std::uint64_t TerminalNoWrapMove::result(const Session &session) const {
    validate(session);
    if (phase_ != Phase::complete)
        throw std::runtime_error("No-wrap movement is not complete.");
    return target_;
}
TerminalNoWrapReveal::TerminalNoWrapReveal(const Session &session,
                                         const std::uint64_t caret,
                                         const std::uint64_t left,
                                         const std::size_t width,
                                         const std::size_t rows,
                                         const std::optional<std::uint64_t> old_top,
                                         const TerminalNoWrapReveal *const previous)
    : stamp_(session.stamp()), size_(session.size()), source_caret_(caret), left_(left),
      width_(width), rows_(rows), old_top_(old_top), boundary_(session, caret, false) {
    if (!width || width > 1000 || !rows || rows > 300 ||
        left > std::numeric_limits<std::uint64_t>::max() - width ||
        (old_top && *old_top > size_))
        throw std::runtime_error("No-wrap reveal bounds are invalid.");
    if (previous) {
        (*previous).require_complete(session);
        if (width_ == (*previous).width_ && rows_ == (*previous).rows_ &&
            (!old_top_ || *old_top_ == (*previous).top(session))) {
            const TerminalHorizontalPage &page = (*previous).viewport(session);
            const std::vector<TerminalHorizontalFrame> &frames = page.result(session);
            const std::vector<TerminalLogicalRow> &logical_rows = page.rows(session);
            for (std::size_t row = 0; row < frames.size(); ++row) {
                for (const TerminalHorizontalCaret boundary : frames[row].carets) {
                    if (boundary.source_offset != source_caret_)
                        continue;
                    if (boundary.column < left_)
                        left_ = boundary.column;
                    else if (boundary.column - left_ >= width_)
                        left_ = boundary.column - width_ + 1;
                    caret_ = {row, static_cast<std::size_t>(boundary.column - left_)};
                    line_start_ = logical_rows[row].offset;
                    top_ = (*previous).top(session);
                    viewport_ = std::make_unique<TerminalHorizontalPage>(session, page, left_, width_);
                    phase_ = Phase::viewport;
                    return;
                }
            }
        }
    }
}
void TerminalNoWrapReveal::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("No-wrap reveal belongs to an older document.");
    if (failed_)
        throw std::runtime_error("No-wrap reveal preparation previously failed.");
}
void TerminalNoWrapReveal::require_complete(const Session &session) const {
    validate(session);
    if (phase_ != Phase::complete)
        throw std::runtime_error("No-wrap reveal is not complete.");
}
bool TerminalNoWrapReveal::step(const Session &session) {
    validate(session);
    try {
        switch (phase_) {
        case Phase::boundary:
            if (boundary_.step(session)) {
                line_start_ = boundary_.result(session);
                top_ = old_top_ && *old_top_ <= line_start_ ? *old_top_ : line_start_;
                phase_ = Phase::logical;
            }
            break;
        case Phase::logical:
            if (!logical_) {
                // Boundary validation reads at most two bytes in its own step.
                logical_ = std::make_unique<TerminalLogicalPage>(session, line_start_, 1);
            } else if ((*logical_).step(session)) {
                phase_ = Phase::measure;
            }
            break;
        case Phase::measure:
            if (!measurement_)
                measurement_ = std::make_unique<TerminalHorizontalLine>(
                    session, *logical_, 0, left_, width_, source_caret_);
            if ((*measurement_).step(session)) {
                const std::optional<std::uint64_t> column =
                    (*measurement_).result(session).source_caret_column;
                if (!column)
                    throw std::runtime_error("No-wrap source caret has no display boundary.");
                if (*column < left_)
                    left_ = *column;
                else if (*column - left_ >= width_)
                    left_ = *column - width_ + 1;
                caret_.column = static_cast<std::size_t>(*column - left_);
                measurement_.reset();
                logical_.reset();
                phase_ = Phase::viewport;
            }
            break;
        case Phase::viewport:
            if (!viewport_) {
                viewport_ = std::make_unique<TerminalHorizontalPage>(
                    session, top_, rows_, left_, width_);
            } else if ((*viewport_).step(session)) {
                const std::vector<TerminalLogicalRow> &rows = (*viewport_).rows(session);
                bool found = false;
                for (std::size_t row = 0; row < rows.size(); ++row) {
                    if (rows[row].offset == line_start_) {
                        caret_.row = row;
                        found = true;
                        break;
                    }
                }
                if (found) {
                    phase_ = Phase::complete;
                } else {
                    // A previous top may no longer cover the caret after resize.
                    if (top_ == line_start_)
                        throw std::runtime_error("No-wrap viewport omitted its caret line.");
                    top_ = line_start_;
                    viewport_.reset();
                }
            }
            break;
        case Phase::complete:
            break;
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
    const bool complete = phase_ == Phase::complete;
    return complete;
}
const TerminalHorizontalPage &TerminalNoWrapReveal::viewport(const Session &session) const {
    require_complete(session);
    return *viewport_;
}
TerminalPageCaret TerminalNoWrapReveal::caret(const Session &session) const {
    require_complete(session);
    return caret_;
}
std::uint64_t TerminalNoWrapReveal::left(const Session &session) const {
    require_complete(session);
    return left_;
}
std::uint64_t TerminalNoWrapReveal::top(const Session &session) const {
    require_complete(session);
    return top_;
}
} // namespace swiftedit
