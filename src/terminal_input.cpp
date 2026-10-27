#include "terminal_input.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace swiftedit {
void TerminalInputDecoder::key(const std::uint32_t identity, const unsigned modifiers) {
    event_ = {};
    event_.key = identity;
    event_.pressed = true;
    const unsigned flags = modifiers - 1;
    event_.shift = (flags & 1) != 0;
    event_.alt = alt_ || (flags & 2) != 0;
    event_.control = (flags & 4) != 0;
    alt_ = false;
    state_ = State::ground;
    ready_ = true;
}
void TerminalInputDecoder::reject(const char *const message) {
    event_ = {};
    event_.error = message;
    ready_ = true;
    state_ = State::ground;
    alt_ = false;
    remaining_ = 0;
    sequence_.clear();
}
void TerminalInputDecoder::scalar(const char32_t value) {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        reject("Invalid Unicode keyboard input.");
        return;
    }
    std::uint32_t identity = 0;
    if (value >= U'a' && value <= U'z')
        identity = static_cast<std::uint32_t>(value - U'a' + U'A');
    else if (value >= U'A' && value <= U'Z')
        identity = static_cast<std::uint32_t>(value);
    key(identity);
    if (value <= 0xffff)
        event_.text_unit = static_cast<char16_t>(value);
    else {
        const char32_t relative = value - 0x10000;
        event_.text_unit = static_cast<char16_t>(0xd800 + relative / 0x400);
        trailing_unit_ = static_cast<char16_t>(0xdc00 + relative % 0x400);
    }
}
TerminalInput TerminalInputDecoder::take() {
    if (!ready_)
        throw std::logic_error("No decoded terminal input is ready.");
    TerminalInput result = std::move(event_);
    event_ = {};
    ready_ = trailing_unit_ != 0;
    if (ready_) {
        event_.pressed = true;
        event_.text_unit = trailing_unit_;
        event_.alt = result.alt;
        trailing_unit_ = 0;
    }
    return result;
}
void TerminalInputDecoder::expire() {
    if (ready_)
        return;
    if (state_ == State::escape)
        key(terminal_key::escape);
    else if (partial())
        reject("Incomplete terminal key sequence or UTF-8 input.");
}
void TerminalInputDecoder::feed(const unsigned char byte) {
    if (ready_)
        throw std::logic_error("Drain decoded input before feeding another byte.");
    if (state_ == State::paste) {
        paste_byte(byte);
        return;
    }
    if (state_ == State::discard_sequence) {
        if (byte >= 0x40 && byte <= 0x7e)
            reject("Terminal key sequence exceeds its length limit.");
        return;
    }
    if (state_ == State::csi || state_ == State::ss3) {
        sequence(byte);
        return;
    }
    if (state_ == State::utf8) {
        if ((byte & 0xc0) != 0x80) {
            reject("Invalid UTF-8 keyboard input.");
            return;
        }
        scalar_ = scalar_ * 64 + (byte & 0x3f);
        --remaining_;
        if (!remaining_) {
            if (scalar_ < minimum_scalar_)
                reject("Overlong UTF-8 keyboard input.");
            else
                scalar(scalar_);
        }
        return;
    }
    if (state_ == State::escape) {
        if (byte == '[' || byte == 'O') {
            state_ = byte == '[' ? State::csi : State::ss3;
            sequence_.clear();
            return;
        }
        state_ = State::ground;
        alt_ = true;
    }
    if (byte == 0x1b) {
        state_ = State::escape;
        return;
    }
    if (byte == '\r' || byte == '\n') { key(terminal_key::enter); return; }
    if (byte == '\t') { key(terminal_key::tab); return; }
    if (byte == 0x7f) { key(terminal_key::backspace); return; }
    if (byte >= 1 && byte <= 26) {
        key(static_cast<std::uint32_t>('A' + byte - 1), 5);
        return;
    }
    if (byte == 0x1f) { key(terminal_key::question, 6); return; }
    if (byte < 32) { reject("Unsupported terminal control input."); return; }
    if (byte < 0x80) { scalar(byte); return; }
    if (byte >= 0xc2 && byte <= 0xdf) {
        scalar_ = byte & 0x1f; remaining_ = 1; minimum_scalar_ = 0x80;
    } else if (byte >= 0xe0 && byte <= 0xef) {
        scalar_ = byte & 0x0f; remaining_ = 2; minimum_scalar_ = 0x800;
    } else if (byte >= 0xf0 && byte <= 0xf4) {
        scalar_ = byte & 0x07; remaining_ = 3; minimum_scalar_ = 0x10000;
    } else {
        reject("Invalid UTF-8 keyboard input.");
        return;
    }
    state_ = State::utf8;
}
void TerminalInputDecoder::sequence(const unsigned char byte) {
    if (byte < 0x40 || byte > 0x7e) {
        if (sequence_.size() == 32) {
            sequence_.clear();
            state_ = State::discard_sequence;
        } else
            sequence_ += static_cast<char>(byte);
        return;
    }
    const bool ss3 = state_ == State::ss3;
    std::array<unsigned, 2> parameters{0, 1};
    std::size_t parameter = 0;
    bool valid = true;
    for (const char value : sequence_) {
        if (value == ';' && parameter == 0) {
            parameter = 1;
            parameters[1] = 0;
        } else if (value >= '0' && value <= '9' && parameters[parameter] < 1000)
            parameters[parameter] = parameters[parameter] * 10 + static_cast<unsigned>(value - '0');
        else
            valid = false;
    }
    sequence_.clear();
    if (!valid || parameters[1] < 1 || parameters[1] > 8) {
        reject("Unsupported terminal key sequence.");
        return;
    }
    if (!ss3 && byte == '~' && parameters[0] == 200 && parameter == 0) {
        state_ = State::paste;
        paste_.clear();
        paste_end_ = 0;
        paste_overflow_ = false;
        return;
    }
    if ((ss3 && parameter != 0) || (byte != '~' && parameters[0] > 1)) {
        reject("Unsupported terminal key sequence.");
        return;
    }
    std::uint32_t identity = 0;
    switch (byte) {
    case 'A': identity = terminal_key::up; break;
    case 'B': identity = terminal_key::down; break;
    case 'C': identity = terminal_key::right; break;
    case 'D': identity = terminal_key::left; break;
    case 'H': identity = terminal_key::home; break;
    case 'F': identity = terminal_key::end; break;
    case 'P': identity = terminal_key::f1; break;
    case 'Q': identity = terminal_key::f2; break;
    case 'R': identity = terminal_key::f3; break;
    case '~':
        switch (parameters[0]) {
        case 1: case 7: identity = terminal_key::home; break;
        case 4: case 8: identity = terminal_key::end; break;
        case 3: identity = terminal_key::erase; break;
        case 5: identity = terminal_key::page_up; break;
        case 6: identity = terminal_key::page_down; break;
        case 11: identity = terminal_key::f1; break;
        case 12: identity = terminal_key::f2; break;
        case 13: identity = terminal_key::f3; break;
        case 15: identity = terminal_key::f5; break;
        case 17: identity = terminal_key::f6; break;
        default: break;
        }
        break;
    default: break;
    }
    if (!identity)
        reject("Unsupported terminal key sequence.");
    else
        key(identity, parameters[1]);
}
void TerminalInputDecoder::append_paste(const unsigned char byte) {
    if (paste_.size() == maximum_paste) {
        paste_overflow_ = true;
        return;
    }
    if (!paste_overflow_)
        paste_ += static_cast<char>(byte);
}
void TerminalInputDecoder::paste_byte(const unsigned char byte) {
    constexpr std::string_view ending = "\x1b[201~";
    if (byte == static_cast<unsigned char>(ending[paste_end_])) {
        ++paste_end_;
        if (paste_end_ == ending.size()) {
            paste_end_ = 0;
            if (paste_overflow_) {
                paste_.clear();
                reject("Paste exceeds the 16 MiB input limit; source unchanged.");
            } else {
                event_ = {};
                event_.pasted = true;
                event_.paste = std::move(paste_);
                ready_ = true;
                state_ = State::ground;
                alt_ = false;
            }
        }
        return;
    }
    for (std::size_t index = 0; index < paste_end_; ++index)
        append_paste(static_cast<unsigned char>(ending[index]));
    paste_end_ = 0;
    if (byte == 0x1b)
        paste_end_ = 1;
    else
        append_paste(byte);
}
} // namespace swiftedit
