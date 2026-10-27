#include "document.hpp"
namespace notepad {
std::string native_newline() {
#ifdef _WIN32
    return "\r\n";
#else
    return "\n";
#endif
}
std::string newline_name(std::string_view s) {
    bool lf = false, cr = false, crlf = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') {
                crlf = true;
                ++i;
            } else
                cr = true;
        } else if (s[i] == '\n')
            lf = true;
    }
    if (static_cast<int>(lf) + static_cast<int>(cr) + static_cast<int>(crlf) > 1)
        return "Mixed (preserved)";
    const std::string result = crlf ? "CRLF" : lf ? "LF" : cr ? "CR" : "No line endings";
    return result;
}
std::string preferred_newline(std::string_view s) {
    const std::size_t i = s.find_first_of("\r\n");
    if (i == std::string_view::npos) {
        const std::string result = native_newline();
        return result;
    }
    const std::string result = s[i] == '\n'                           ? "\n"
                               : i + 1 < s.size() && s[i + 1] == '\n' ? "\r\n"
                                                                      : "\r";
    return result;
}
} // namespace notepad
