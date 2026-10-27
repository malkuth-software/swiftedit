#pragma once
#include "terminal_wrap_view.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>

namespace swiftedit {
namespace terminal_key {
// Letters use uppercase ASCII identities independently of typed text. These
// non-text identities are adapter-neutral, not operating-system key codes.
inline constexpr std::uint32_t escape = 256, enter = 257, tab = 258,
    backspace = 259, erase = 260, left = 261, right = 262, up = 263, down = 264,
    home = 265, end = 266, page_up = 267, page_down = 268, f1 = 269, f2 = 270,
    f3 = 271, f5 = 272, f6 = 273, question = 274, other = 275;
}
struct TerminalSize {
    std::size_t columns{80}, rows{24};
};
struct TerminalInput {
    std::uint32_t key{};
    // A lossless UTF-16 input stream. The shared loop validates surrogate pairs;
    // adapters must not split bytes, replace invalid input or normalize text.
    char16_t text_unit{};
    std::size_t repeats{1};
    bool pressed{}, resized{}, control{}, alt{}, shift{};
    // Bracketed paste is one atomic source insertion, never a command stream.
    bool pasted{};
    std::string paste{}, error{};
};
class TerminalConsole : public TerminalWrapControl {
public:
    virtual void start() = 0;
    virtual TerminalSize size() const = 0;
    virtual void write(std::string_view) const = 0;
    virtual bool input_ready() const = 0;
    virtual TerminalInput read() const = 0;
    virtual void allow_wrap_cancel(bool) = 0;
};
} // namespace swiftedit
