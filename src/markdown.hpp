#pragma once
#include <cstddef>
#include <exception>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace swiftedit {
enum class MarkdownKind { paragraph, heading, code, rule, table_row };
struct MarkdownSpan {
    std::string text{}, url{};
    bool bold{}, italic{}, code{}, strike{};
    std::size_t column{};
};
struct MarkdownBlock {
    MarkdownKind kind{MarkdownKind::paragraph};
    std::vector<MarkdownSpan> spans{};
    std::size_t level{}, indent{}, columns{1};
    bool quoted{}, header{};
};
// Parses inert presentation only; links/images never access files or network.
// Raw HTML is treated as text. All source remains owned and unchanged by caller.
class MarkdownCancelled final : public std::exception {
public:
    const char *what() const noexcept override { return "Markdown parsing cancelled."; }
};
// Cancellation is observed around validation, at MD4C callbacks, during
// attribute decoding and before publication. Library work between callbacks,
// validation, allocation and cleanup are not hard-latency bounded.
[[nodiscard]] std::vector<MarkdownBlock> parse_markdown(std::string_view,
                                                     std::stop_token cancellation = {});
} // namespace swiftedit
