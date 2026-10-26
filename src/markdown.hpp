#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <exception>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace swiftedit {
enum class MarkdownKind { paragraph, heading, code, rule, table_row };
enum class MarkdownAlignment { left, center, right };
struct MarkdownColour {
    std::uint8_t red{}, green{}, blue{};
    bool operator==(const MarkdownColour &other) const {
        const bool equal = red == other.red && green == other.green && blue == other.blue;
        return equal;
    }
};
// The stx_rtf2md dialect's attribute spans ([text]{u color=#rrggbb size=+1})
// add underline, super/subscript, colours and a size step of -2 to +4
// relative to body text; plain Markdown leaves them unset.
struct MarkdownSpan {
    std::string text{}, url{};
    bool bold{}, italic{}, code{}, strike{};
    bool underline{}, superscript{}, subscript{};
    std::optional<MarkdownColour> colour{}, background{};
    int size_step{};
    std::size_t column{};
};
struct MarkdownBlock {
    MarkdownKind kind{MarkdownKind::paragraph};
    std::vector<MarkdownSpan> spans{};
    std::size_t level{}, indent{}, columns{1};
    bool quoted{}, header{};
    // From the dialect's block attribute line ({align=center}).
    MarkdownAlignment alignment{MarkdownAlignment::left};
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
