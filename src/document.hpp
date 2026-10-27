#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace notepad {
constexpr std::size_t maximum_bytes = 16 * 1024 * 1024;
enum class Encoding { utf8, utf8_bom, utf16_le, utf16_be };
enum class TextControls { legacy_gui, preserve };
struct Decoded {
    std::string text{};
    Encoding encoding{Encoding::utf8};
};
// The current GUI retains its control-character restrictions. Mapped Session
// consumers explicitly preserve every valid scalar, including NUL and controls.
[[nodiscard]] Decoded decode(std::string_view bytes, TextControls = TextControls::legacy_gui);
[[nodiscard]] std::string encode(std::string_view text, Encoding encoding,
                                 TextControls = TextControls::legacy_gui);
[[nodiscard]] std::string encoding_name(Encoding encoding);
// New files use the host convention; existing endings are never converted implicitly.
[[nodiscard]] std::string native_newline();
[[nodiscard]] std::string newline_name(std::string_view text);
[[nodiscard]] std::string preferred_newline(std::string_view text);
// Count nonempty runs separated by Unicode whitespace. Punctuation stays in
// its run; this is not language-specific segmentation. Invalid UTF-8 is refused.
[[nodiscard]] std::size_t word_count(std::string_view text);
[[nodiscard]] std::optional<std::size_t> find_literal(std::string_view text, std::string_view query,
                                                      std::size_t start, bool match_case);
struct Replacement {
    std::string text{};
    std::size_t count{};
};
[[nodiscard]] Replacement replace_all(std::string_view text, std::string_view query,
                                      std::string_view replacement, bool match_case);
struct FileSnapshot {
    bool exists{};
    std::uint64_t volume{}, identity{}, modified{};
    std::string bytes{};
    bool operator==(const FileSnapshot &other) const {
        const bool equal = exists == other.exists && volume == other.volume &&
                           identity == other.identity && modified == other.modified &&
                           bytes == other.bytes;
        return equal;
    }
};
[[nodiscard]] FileSnapshot read_file(const std::filesystem::path &path);
// Expected absence is distinct from an observed existing file. Never truncates.
[[nodiscard]] FileSnapshot write_file(const std::filesystem::path &path, std::string_view bytes,
                                      const FileSnapshot &expected);
struct Document {
    std::filesystem::path path{};
    std::string saved_text{};
    std::string opened_text{};
    Encoding encoding{Encoding::utf8};
    FileSnapshot snapshot{};
    void open(const std::filesystem::path &source);
    void save(const std::filesystem::path &target, std::string_view text,
              const FileSnapshot &expected);
    void save_encoded(const std::filesystem::path &target, std::string_view text,
                      const FileSnapshot &expected, Encoding requested);
    [[nodiscard]] bool dirty(std::string_view text) const {
        const bool changed = text != saved_text;
        return changed;
    }
};
} // namespace notepad
