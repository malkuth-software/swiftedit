#pragma once
#include "search.hpp"
namespace swiftedit {
// Session-issued, one-use replacement authority. Callers can advance or discard
// it, but cannot change its source identity, replacement bytes or prepared text.
class SessionReplacement {
public:
    SessionReplacement(const SessionReplacement &) = delete;
    SessionReplacement &operator=(const SessionReplacement &) = delete;
    bool step(std::size_t budget = 4096);

private:
    friend class Session;
    SessionReplacement(const Session &, SearchPattern, std::string, bool match_case);
    DocumentStamp stamp_{};
    std::unique_ptr<ReplacementScan> scan_{};
    bool ready_{}, consumed_{};
};
} // namespace swiftedit
