#include "document.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
#ifdef _WIN32
        const std::string expected = "\r\n";
#else
        const std::string expected = "\n";
#endif
        const std::string native = notepad::native_newline();
        const std::string empty = notepad::preferred_newline("");
        const std::string single = notepad::preferred_newline("No ending yet");
        check(native == expected && empty == expected && single == expected,
              "New documents use the actual platform convention");
        const std::string crlf = notepad::preferred_newline("one\r\ntwo\nthree\r");
        const std::string lf = notepad::preferred_newline("one\ntwo\r\nthree\r");
        const std::string cr = notepad::preferred_newline("one\rtwo\r\nthree\n");
        check(crlf == "\r\n" && lf == "\n" && cr == "\r",
              "Existing files retain their first observed ending on every platform");
        const std::string mixed = notepad::newline_name("one\r\ntwo\nthree\r");
        const std::string none = notepad::newline_name("No ending yet");
        check(mixed == "Mixed (preserved)" && none == "No line endings",
              "Ending diagnostics distinguish mixed and absent endings");
        std::cout << "Native newline tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
