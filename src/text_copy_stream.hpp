#pragma once
#include <cstddef>
#include <string>
#include <string_view>
namespace swiftedit {
struct TextCopyChunk {
    std::string bytes{};
    std::size_t invalid_bytes{};
};
// One owner per copy. Input chunks are at most 64 KiB. At most three trailing
// bytes are retained until the next chunk establishes whether UTF-8 is complete.
// finish=true consumes any incomplete final sequence as individual illegal bytes.
// Allocation/validation failure leaves the owner unchanged; no source is borrowed.
class TextCopyStream final {
public:
    [[nodiscard]] TextCopyChunk append(std::string_view bytes, bool finish = false);
private:
    std::string pending_{};
    bool finished_{};
};
} // namespace swiftedit
