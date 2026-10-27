#include "session_replace.hpp"
#include <stdexcept>
namespace swiftedit {
SessionReplacement::SessionReplacement(const Session &session, SearchPattern pattern,
                                       std::string replacement, bool match_case)
    : stamp_(session.stamp()),
      scan_(std::make_unique<ReplacementScan>(session.text(), std::move(pattern),
                                              std::move(replacement), match_case,
                                              editable_limit - 1)) {}
bool SessionReplacement::step(std::size_t budget) {
    if (consumed_)
        throw std::runtime_error("Replacement was already consumed.");
    if (!ready_)
        ready_ = (*scan_).step(budget);
    return ready_;
}
} // namespace swiftedit
