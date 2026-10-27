#pragma once
#include "session.hpp"

namespace swiftedit {
struct BlankAnchor {
    std::uint64_t offset{}, line{};
};
struct ContextPage {
    Page content{};
    DocumentStamp stamp{};
    std::vector<BlankAnchor> blanks{};
};
// Sequential bounded context reader. Markers describe complete empty logical
// lines, never whitespace-only lines or a synthetic trailing EOF line.
// The caller owns Session through each synchronous read. No reference retained.
class ContextCursor {
public:
    void reset(const Session &);
    [[nodiscard]] ContextPage next(const Session &, std::size_t budget = 4096);

private:
    DocumentStamp stamp_{};
    std::uint64_t offset_{}, line_{1};
    bool started_{}, empty_{true}, after_cr_{};
};
[[nodiscard]] std::string blank_marker(const BlankAnchor &);
} // namespace swiftedit
