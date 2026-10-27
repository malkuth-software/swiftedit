#pragma once
#include <string>
#include <string_view>

namespace notepad {
// RTF import (owner decision 5, 2026-10-26): an RTF file opens as a new
// untitled Markdown document in the stx_rtf2md dialect. SwiftEdit never
// writes RTF. Content beginning "{\rtf" is RTF; the extension alone is not.
[[nodiscard]] bool looks_like_rtf(std::string_view bytes);
[[nodiscard]] bool has_rtf_extension(std::string_view filename);
struct RtfImport {
    std::string markdown{};
    // What the conversion could not keep, one plain sentence per kind, each
    // ending in a line feed; empty when nothing was lost.
    std::string losses{};
    // The body size the sizes are relative to, and the rule that chose it,
    // for example "11 pt (Word's default character format)".
    std::string body{};
};
// Converts RTF bytes. Throws std::runtime_error with a reader-facing
// explanation when the RTF cannot be read; nothing is partially returned.
[[nodiscard]] RtfImport import_rtf(std::string_view bytes);
} // namespace notepad
