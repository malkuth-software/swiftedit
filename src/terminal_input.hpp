#pragma once
#include "terminal_console.hpp"

namespace swiftedit {
// Incremental xterm-compatible input. The caller drains ready events before
// feeding another byte, bounding retained events to one scalar/paste/error.
// Escape/partial UTF-8 deadlines belong to the OS poll loop, which calls expire.
class TerminalInputDecoder final {
public:
    void feed(unsigned char);
    void expire();
    bool ready() const { return ready_; }
    bool partial() const {
        const bool pending = state_ != State::ground && state_ != State::paste;
        return pending;
    }
    bool escape_pending() const {
        const bool pending = state_ == State::escape || state_ == State::csi ||
                             state_ == State::ss3 || state_ == State::discard_sequence;
        return pending;
    }
    TerminalInput take();
    static constexpr std::size_t maximum_paste = 16 * 1024 * 1024;

private:
    enum class State { ground, escape, csi, ss3, discard_sequence, utf8, paste };
    void scalar(char32_t);
    void key(std::uint32_t, unsigned modifiers = 1);
    void reject(const char *);
    void sequence(unsigned char);
    void paste_byte(unsigned char);
    void append_paste(unsigned char);
    State state_{State::ground};
    TerminalInput event_{};
    bool ready_{}, alt_{}, paste_overflow_{};
    char16_t trailing_unit_{};
    char32_t scalar_{}, minimum_scalar_{};
    unsigned remaining_{};
    std::string sequence_{}, paste_{};
    std::size_t paste_end_{};
};
} // namespace swiftedit
