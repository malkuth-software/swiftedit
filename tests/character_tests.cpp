#include "characters.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        const swiftedit::CharacterInfo letter = swiftedit::character_info(U'A');
        check(letter.assigned && !letter.control && letter.name == "LATIN CAPITAL LETTER A",
              "Ordinary Unicode name and category");
        const swiftedit::CharacterInfo null = swiftedit::character_info(0);
        check(null.control && null.name == "NULL" && !null.explanation.empty(),
              "Control uses pinned Unicode name alias and explanation");
        const swiftedit::CharacterInfo bidi = swiftedit::character_info(0x202e);
        check(bidi.control && bidi.name == "RIGHT-TO-LEFT OVERRIDE",
              "Direction controls belong to separate catalog");
        const swiftedit::CharacterInfo hangul = swiftedit::character_info(0xac00);
        const swiftedit::CharacterInfo hangul_last = swiftedit::character_info(0xd7a3);
        check(hangul.name == "HANGUL SYLLABLE GA" && hangul_last.name == "HANGUL SYLLABLE HIH",
              "Algorithmic Hangul names cover both endpoints");
        const swiftedit::CharacterInfo cjk = swiftedit::character_info(0x4e00);
        check(cjk.name == "CJK UNIFIED IDEOGRAPH-4E00", "Algorithmic ideograph name");
        check(!swiftedit::character_info(0xd800).assigned &&
                  !swiftedit::character_info(0x10ffff).assigned &&
                  !swiftedit::character_info(0x378).assigned,
              "Surrogate, noncharacter and unassigned values excluded");
        const std::string emoji = swiftedit::character_utf8(0x1f600);
        check(emoji == "\xf0\x9f\x98\x80", "Supplementary scalar encodes exactly");
        const std::string zero = swiftedit::character_utf8(0);
        check(zero.size() == 1 && zero[0] == '\0', "Null is preserved as one byte");
        check(swiftedit::parse_codepoint("U+1F600") == 0x1f600,
              "Unicode scalar entry accepts explicit code point");
        bool surrogate_refused = false;
        try {
            static_cast<void>(swiftedit::parse_codepoint("D800"));
        } catch (const std::runtime_error &) {
            surrogate_refused = true;
        }
        check(surrogate_refused, "Surrogate insertion input refused");
        const std::vector<swiftedit::CharacterInfo> ordinary = swiftedit::character_page(0, false);
        for (const swiftedit::CharacterInfo &info : ordinary)
            check(info.assigned && !info.control, "Ordinary picker excludes controls");
        const std::vector<swiftedit::CharacterInfo> controls = swiftedit::character_page(0, true);
        check(controls.size() == 66, "ASCII/C1 page includes controls and soft hyphen");
        for (const swiftedit::CharacterInfo &info : controls)
            check(info.control && !info.explanation.empty(), "Every control has an explanation");
        const std::string source = std::string("A\0", 2) + "\xe2\x80\xae\xff";
        const std::string report = swiftedit::inspect_characters(source);
        check(report.find("U+0000") != report.npos &&
                  report.find("RIGHT-TO-LEFT OVERRIDE") != report.npos &&
                  report.find("Illegal UTF-8 byte 00FF") != report.npos,
              "Inspector names controls and illegal bytes without emitting them");
        check(report.find('\0') == report.npos && report.find("\xe2\x80\xae") == report.npos,
              "Inspector output cannot activate source control characters");
        const std::string bounded = swiftedit::inspect_characters(std::string(100000, 'a'));
        check(bounded.find("first 32") != bounded.npos, "Inspector output is bounded");
        std::cout << "Character catalog and inspector tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
