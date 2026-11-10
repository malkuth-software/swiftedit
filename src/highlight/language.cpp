#include "language.hpp"
#include "css_language.hpp"
#include <array>

namespace swiftedit::highlight {
namespace {

const PlainLanguage &plain_language() noexcept {
    static const PlainLanguage language{};
    return language;
}

bool ends_with_ignoring_ascii_case(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size())
        return false;
    const std::size_t offset = text.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        char character = text[offset + index];
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
        if (character != suffix[index])
            return false;
    }
    return true;
}

} // namespace

LexState PlainLanguage::lex_line(std::string_view, LexState entry, std::vector<LineToken> &) const {
    return entry;
}

std::span<const Language *const> registered_languages() noexcept {
    // Built on first use from the languages' own static objects, so the order
    // of static initialization across files never matters.
    static const std::array<const Language *, 4U> languages{
        &plain_language(), &css_theme_sheet_language(), &css_form_document_language(),
        &css_inline_style_language()};
    const std::span<const Language *const> result(languages.data(), languages.size());
    return result;
}

const Language &language_named(std::string_view name) noexcept {
    for (const Language *language : registered_languages()) {
        if ((*language).name() == name)
            return *language;
    }
    return plain_language();
}

const Language &language_for_file_name(std::string_view file_name) noexcept {
    if (ends_with_ignoring_ascii_case(file_name, ".css"))
        return css_theme_sheet_language();
    if (ends_with_ignoring_ascii_case(file_name, ".form"))
        return css_form_document_language();
    return plain_language();
}

} // namespace swiftedit::highlight
