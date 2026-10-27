#include "terminal_input.hpp"
#include <iostream>
#include <stdexcept>

void check(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}
void feed(swiftedit::TerminalInputDecoder &decoder, const std::string_view bytes) {
    for (const unsigned char byte : bytes)
        decoder.feed(byte);
}
swiftedit::TerminalInput decode(const std::string_view bytes) {
    swiftedit::TerminalInputDecoder decoder{};
    feed(decoder, bytes);
    check(decoder.ready(), "Complete sequence must emit an event");
    swiftedit::TerminalInput result = decoder.take();
    return result;
}
int main() {
    try {
        swiftedit::TerminalInput event = decode("\x1b[1;6D");
        check(event.key == swiftedit::terminal_key::left && event.control && event.shift,
              "Modified cursor sequence preserves modifiers");
        event = decode("\x1bOP");
        check(event.key == swiftedit::terminal_key::f1, "SS3 function key");
        event = decode("\x1b[17~");
        check(event.key == swiftedit::terminal_key::f6, "Tilde function key");
        event = decode("\x13");
        check(event.key == 'S' && event.control, "Conventional Ctrl+S input");
        event = decode("\x08");
        check(event.key == 'H' && event.control, "Ctrl+H remains the Replace command");
        event = decode("\x1bq");
        check(event.key == 'Q' && event.text_unit == u'q' && event.alt, "Alt text preserves case");
        swiftedit::TerminalInputDecoder escape{};
        escape.feed(0x1b);
        check(!escape.ready() && escape.partial(), "Escape waits for host deadline");
        escape.expire();
        check(escape.take().key == swiftedit::terminal_key::escape, "Standalone Escape");
        swiftedit::TerminalInputDecoder unicode{};
        feed(unicode, "\xf0\x9f\x98");
        check(!unicode.ready(), "UTF-8 remains incomplete across reads");
        unicode.feed(0x80);
        event = unicode.take();
        check(event.text_unit == 0xd83d && unicode.ready(), "Supplementary high unit");
        check(unicode.take().text_unit == 0xde00 && !unicode.ready(), "Supplementary low unit");
        event = decode("\xed\xa0\x80");
        check(!event.error.empty() && !event.pressed, "UTF-8 encoded surrogate rejected");
        event = decode("\xe0\x80\x80");
        check(!event.error.empty(), "Overlong input rejected");
        event = decode("\x1b[999A");
        check(!event.error.empty(), "Unsupported sequence consumed as one error");
        swiftedit::TerminalInputDecoder long_key{};
        feed(long_key, "\x1b[");
        for (std::size_t index = 0; index < 80; ++index)
            long_key.feed('1');
        check(!long_key.ready(), "Oversized sequence drains through its terminator");
        long_key.feed('~');
        check(!long_key.take().error.empty(), "Oversized key rejected atomically");
        long_key.feed('x');
        check(long_key.take().text_unit == u'x', "Decoder recovers after rejected sequence");
        swiftedit::TerminalInputDecoder paste{};
        feed(paste, "\x1b[200~");
        const std::string payload = "line\r\n\x13\x18\x1b[20z\x1b\x1b[201x";
        feed(paste, payload);
        paste.expire();
        check(!paste.ready(), "Incomplete paste never executes commands or expires to keys");
        feed(paste, "\x1b[201~");
        event = paste.take();
        check(event.pasted && event.paste == payload && !event.pressed,
              "Bracketed paste is exact data, including control and partial-marker bytes");
        feed(paste, "\x1b[200~");
        for (std::size_t index = 0; index <= swiftedit::TerminalInputDecoder::maximum_paste; ++index)
            paste.feed('a');
        check(!paste.ready(), "Oversized paste drains without emitting commands");
        feed(paste, "\x1b[201~");
        event = paste.take();
        check(!event.error.empty() && !event.pasted, "Oversized paste publishes no partial data");
        paste.feed('q');
        check(paste.take().text_unit == u'q', "Input recovers after oversized paste");
        std::cout << "Terminal decoder: keys, modifiers, UTF-8, bounds and atomic paste passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
