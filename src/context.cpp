#include "context.hpp"
#include <limits>
#include <stdexcept>

namespace swiftedit {
void ContextCursor::reset(const Session &session) {
    stamp_ = session.stamp();
    offset_ = 0;
    line_ = 1;
    empty_ = true;
    after_cr_ = false;
    started_ = true;
}
ContextPage ContextCursor::next(const Session &session, std::size_t budget) {
    const DocumentStamp current = session.stamp();
    if (!started_ || current.identity != stamp_.identity || current.revision != stamp_.revision)
        throw std::runtime_error("Context changed. Start a new context read.");
    if (budget == 0 || budget > maximum_page)
        throw std::runtime_error("Context page budget must be between 1 and 65536 bytes.");
    ContextPage result{};
    result.content = session.page(offset_, budget);
    result.stamp = stamp_;
    // At most one blank-line record per source byte; reserve outside the scan.
    result.blanks.reserve(result.content.bytes.size());
    std::uint64_t line = line_;
    bool empty = empty_;
    bool after_cr = after_cr_;
    for (std::size_t i = 0; i < result.content.bytes.size(); ++i) {
        const char byte = result.content.bytes[i];
        if (after_cr && byte == '\n') {
            after_cr = false;
            continue;
        }
        after_cr = false;
        if (byte == '\r' || byte == '\n') {
            if (line == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Context line counter exhausted.");
            if (empty)
                result.blanks.push_back({offset_ + i, line});
            ++line;
            empty = true;
            after_cr = byte == '\r';
        } else {
            empty = false;
        }
    }
    // Publish cursor progress only after the entire page succeeds.
    offset_ = result.content.next;
    line_ = line;
    empty_ = empty;
    after_cr_ = after_cr;
    return result;
}
std::string blank_marker(const BlankAnchor &anchor) {
    if (anchor.line == 0)
        throw std::runtime_error("Blank-line metadata requires a nonzero line number.");
    const std::string result = "\r\rL" + std::to_string(anchor.line) + "\r\r";
    return result;
}
} // namespace swiftedit
