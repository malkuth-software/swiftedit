#pragma once
#include <cstdint>
#include <string_view>

namespace notepad {
// Constant-space UTF-8 stream counter. Appends borrow bytes only for the call;
// words and partial scalars survive chunk boundaries. Failure poisons the
// counter, and finish refuses truncated input. No partial result is published.
class WordCounter final {
public:
    void append(const std::string_view bytes);
    [[nodiscard]] std::uint64_t finish();

private:
    void consume(const char32_t scalar);
    [[noreturn]] void invalid();
    std::uint64_t words_{};
    char32_t scalar_{}, minimum_{};
    unsigned remaining_{};
    bool in_word_{}, failed_{}, finished_{};
};
} // namespace notepad
