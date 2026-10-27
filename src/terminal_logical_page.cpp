#include "terminal_logical_page.hpp"
#include <algorithm>
#include <stdexcept>

namespace swiftedit {
namespace {
void validate_logical_start(const Session &session, const std::uint64_t start,
                            const std::size_t rows) {
    if (!rows || rows > 300 || start > session.size())
        throw std::runtime_error("Logical page bounds are invalid.");
    if (start) {
        const Page boundary = session.page(start - 1, 2);
        const bool after_lf = boundary.bytes[0] == '\n';
        const bool after_cr = boundary.bytes[0] == '\r' &&
            (boundary.bytes.size() == 1 || boundary.bytes[1] != '\n');
        if (!after_lf && !after_cr)
            throw std::runtime_error("Logical page must start at a complete line boundary.");
    }
}
} // namespace
TerminalLogicalPage::TerminalLogicalPage(const Session &session, const std::uint64_t start,
                                         const std::size_t rows)
    : stamp_(session.stamp()), size_(session.size()), scan_(start), line_start_(start), limit_(rows) {
    validate_logical_start(session, start, rows);
    rows_.reserve(rows);
}
TerminalLogicalPrevious::TerminalLogicalPrevious(const Session &session,
                                                 const std::uint64_t start,
                                                 const std::size_t rows)
    : stamp_(session.stamp()), size_(session.size()), start_(start), scan_(start),
      remaining_(rows), complete_(start == 0) {
    validate_logical_start(session, start, rows);
}
void TerminalLogicalPrevious::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("Logical navigation belongs to an older document.");
}
bool TerminalLogicalPrevious::step(const Session &session, const std::size_t budget) {
    validate(session);
    if (!budget || budget > 8192)
        throw std::runtime_error("Logical navigation step budget must be 1..8192 bytes.");
    if (complete_)
        return true;
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(scan_, budget));
    const Page source = session.page(scan_ - count, count);
    for (std::size_t index = source.bytes.size(); index > 0;) {
        --index;
        --scan_;
        const char byte = source.bytes[index];
        const bool boundary = byte == '\n' || (byte == '\r' && following_ != '\n');
        following_ = byte;
        const std::uint64_t offset = scan_ + 1;
        if (boundary && offset < start_) {
            --remaining_;
            if (!remaining_) {
                result_ = offset;
                complete_ = true;
                break;
            }
        }
    }
    if (!scan_)
        complete_ = true;
    return complete_;
}
std::uint64_t TerminalLogicalPrevious::result(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("Logical navigation is not complete.");
    return result_;
}
void TerminalLogicalPage::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
        session.size() != size_)
        throw std::runtime_error("Logical page belongs to an older document.");
}
void TerminalLogicalPage::require_complete(const Session &session) const {
    validate(session);
    if (!complete_)
        throw std::runtime_error("Logical page is not complete.");
}
void TerminalLogicalPage::finish_row(const std::size_t separator_bytes) {
    const std::uint64_t body_end = scan_ - separator_bytes;
    rows_.push_back({line_start_, body_end - line_start_, separator_bytes});
    line_start_ = scan_;
    // A trailing separator leaves one real, empty EOF line to display.
    more_ = scan_ < size_ || separator_bytes != 0;
    complete_ = rows_.size() == limit_ || !more_;
}
bool TerminalLogicalPage::step(const Session &session, const std::size_t budget) {
    validate(session);
    if (!budget || budget > 8192)
        throw std::runtime_error("Logical page step budget must be 1..8192 bytes.");
    if (complete_)
        return true;
    // Acquire the complete bounded read before mutating scan state. Row
    // storage was reserved up front; this loop neither allocates nor copies text.
    const Page source = session.page(scan_, budget);
    std::size_t index = 0;
    while (index < source.bytes.size()) {
        const char byte = source.bytes[index];
        if (pending_cr_) {
            pending_cr_ = false;
            if (byte == '\n') {
                ++scan_;
                ++index;
                finish_row(2);
                if (complete_)
                    return true;
                continue;
            }
            finish_row(1);
            if (complete_)
                return true;
            // This byte belongs to the following line; process it below.
        }
        ++scan_;
        ++index;
        if (byte == '\r')
            pending_cr_ = true;
        else if (byte == '\n') {
            finish_row(1);
            if (complete_)
                return true;
        }
    }
    if (scan_ == size_) {
        if (pending_cr_) {
            pending_cr_ = false;
            finish_row(1);
            if (complete_)
                return true;
        }
        finish_row(0);
    }
    return complete_;
}
const std::vector<TerminalLogicalRow> &TerminalLogicalPage::result(const Session &session) const {
    require_complete(session);
    return rows_;
}
std::uint64_t TerminalLogicalPage::next(const Session &session) const {
    require_complete(session);
    return scan_;
}
bool TerminalLogicalPage::more(const Session &session) const {
    require_complete(session);
    return more_;
}
} // namespace swiftedit
