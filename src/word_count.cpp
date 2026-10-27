#include "word_count.hpp"
#include <limits>
#include <stdexcept>

namespace notepad {
void WordCounter::invalid() {
    failed_ = true;
    throw std::runtime_error("Word count requires valid UTF-8 text.");
}

void WordCounter::consume(const char32_t scalar) {
    const bool whitespace =
        (scalar >= 0x09 && scalar <= 0x0d) || scalar == 0x20 || scalar == 0x85 || scalar == 0xa0 ||
        scalar == 0x1680 || (scalar >= 0x2000 && scalar <= 0x200a) || scalar == 0x2028 ||
        scalar == 0x2029 || scalar == 0x202f || scalar == 0x205f || scalar == 0x3000;
    if (whitespace) {
        in_word_ = false;
    } else if (!in_word_) {
        if (words_ == std::numeric_limits<std::uint64_t>::max()) {
            failed_ = true;
            throw std::overflow_error("Word count exceeds its supported range.");
        }
        ++words_;
        in_word_ = true;
    }
}

void WordCounter::append(const std::string_view bytes) {
    if (failed_ || finished_)
        throw std::runtime_error("Word counter is no longer accepting input.");
    for (const unsigned char byte : bytes) {
        if (remaining_ != 0) {
            if (byte < 0x80 || byte > 0xbf)
                invalid();
            scalar_ = (scalar_ << 6) | (byte & 63);
            --remaining_;
            if (remaining_ == 0) {
                if (scalar_ < minimum_ || scalar_ > 0x10ffff ||
                    (scalar_ >= 0xd800 && scalar_ <= 0xdfff))
                    invalid();
                consume(scalar_);
            }
        } else if (byte < 0x80) {
            consume(byte);
        } else if (byte >= 0xc2 && byte <= 0xdf) {
            scalar_ = byte & 31;
            minimum_ = 0x80;
            remaining_ = 1;
        } else if (byte >= 0xe0 && byte <= 0xef) {
            scalar_ = byte & 15;
            minimum_ = 0x800;
            remaining_ = 2;
        } else if (byte >= 0xf0 && byte <= 0xf4) {
            scalar_ = byte & 7;
            minimum_ = 0x10000;
            remaining_ = 3;
        } else {
            invalid();
        }
    }
}

std::uint64_t WordCounter::finish() {
    if (failed_ || remaining_ != 0)
        invalid();
    finished_ = true;
    return words_;
}
} // namespace notepad
