#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace swiftedit {
struct CharacterInfo {
    char32_t scalar{};
    std::string name{}, category{}, explanation{};
    bool assigned{}, control{};
};
[[nodiscard]] CharacterInfo character_info(char32_t);
[[nodiscard]] std::string codepoint_label(char32_t);
[[nodiscard]] char32_t parse_codepoint(std::string_view);
[[nodiscard]] std::string character_utf8(char32_t);
[[nodiscard]] std::vector<CharacterInfo> character_page(char32_t first, bool controls);
[[nodiscard]] std::vector<CharacterInfo> control_characters();
[[nodiscard]] std::string inspect_characters(std::string_view);
} // namespace swiftedit
