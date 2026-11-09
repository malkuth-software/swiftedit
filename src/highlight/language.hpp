#pragma once
// A highlighting language and the fixed registry that finds one by name or
// by file name. Languages are chosen at run time from the document's file
// name, so a language is a virtual interface; each one is a stateless object
// with static storage duration, and the registry hands out borrowed
// references that stay valid for the whole program.
#include "highlight_types.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace swiftedit::highlight {

class Language {
public:
    Language() = default;
    Language(const Language &) = delete;
    Language &operator=(const Language &) = delete;
    virtual ~Language() = default;

    // The language's registry name, as "css" or "form". Stable: settings and
    // the theme editor store it.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    // What a person reads in a menu, as "CSS (theme sheet)".
    [[nodiscard]] virtual std::string_view display_name() const noexcept = 0;
    // The state the first line of a document starts in.
    [[nodiscard]] virtual LexState initial_state() const noexcept = 0;
    // Lexes one line (its bytes without the terminator) entered in `entry`.
    // Appends the line's tokens to `tokens` in increasing column order,
    // without overlap; bytes no token covers are plain. Returns the state the
    // next line starts in. Mutates nothing but `tokens`; a line longer than
    // UINT32_MAX bytes is the caller's to refuse.
    [[nodiscard]] virtual LexState lex_line(std::string_view line, LexState entry,
                                            std::vector<LineToken> &tokens) const = 0;
};

// The plain-text language: one plain gap per line, no tokens, one state.
class PlainLanguage final : public Language {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "plain"; }
    [[nodiscard]] std::string_view display_name() const noexcept override {
        return "Plain text";
    }
    [[nodiscard]] LexState initial_state() const noexcept override { return LexState{}; }
    [[nodiscard]] LexState lex_line(std::string_view line, LexState entry,
                                    std::vector<LineToken> &tokens) const override;
};

// The languages built in, in menu order. The first is plain text.
[[nodiscard]] std::span<const Language *const> registered_languages() noexcept;
// The language named `name`, or plain text when no language has that name.
[[nodiscard]] const Language &language_named(std::string_view name) noexcept;
// The language a file name selects by its extension, compared without regard
// to ASCII case (".CSS" is CSS): .css and .theme.css select the theme sheet,
// .form the form document, everything else plain text. `file_name` is UTF-8.
[[nodiscard]] const Language &language_for_file_name(std::string_view file_name) noexcept;

} // namespace swiftedit::highlight
