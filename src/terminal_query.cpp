#include "terminal_query.hpp"
#include <stdexcept>
namespace swiftedit {
namespace gf = gui_forms;
void TerminalQuery::replace(std::size_t start, std::size_t length, std::string_view inserted) {
    if (inserted.size() > 4096 - (store_.utf8().size() - length))
        throw std::runtime_error("Search query exceeds 4096 UTF-8 bytes.");
    std::string source(store_.utf8());
    source.replace(start, length, inserted);
    gf::TextStore next(source);
    std::vector<SearchSlot> slots{};
    if (!source.empty()) {
        const SearchPattern parsed(source);
        slots = parsed.slots();
    }
    // Flags belong to source occurrences, not matching text. A repeated
    // character inserted before a flagged character must not steal its flag.
    // Retain a flag only when the entire unchanged source grapheme survives
    // as a whole grapheme; Unicode edits can merge neighboring graphemes.
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].wildcard)
            continue;
        const gf::Utf8Range old = store_.grapheme_range(gf::GraphemeIndex(index));
        const std::size_t begin = old.start.value();
        const std::size_t end = old.end.value();
        std::size_t mapped = begin;
        if (begin >= start + length)
            mapped = begin - length + inserted.size();
        else if (end > start)
            continue;
        const std::size_t mapped_end = mapped + end - begin;
        if (!next.is_grapheme_boundary(gf::Utf8Offset(mapped)) ||
            !next.is_grapheme_boundary(gf::Utf8Offset(mapped_end)))
            continue;
        const std::size_t target = next.grapheme_index(gf::Utf8Offset(mapped)).value();
        if (target < slots.size() && slots[target].literal == slots_[index].literal)
            slots[target].wildcard = true;
    }
    std::size_t caret = start + inserted.size();
    if (!next.is_grapheme_boundary(gf::Utf8Offset(caret)))
        caret = next.next_grapheme_boundary(gf::Utf8Offset(caret)).value();
    store_ = std::move(next);
    slots_ = std::move(slots);
    caret_ = caret;
}
void TerminalQuery::insert(std::string_view text) { replace(caret_, 0, text); }
void TerminalQuery::erase(bool backward) {
    const std::size_t other =
        backward ? store_.previous_grapheme_boundary(gf::Utf8Offset(caret_)).value()
                 : store_.next_grapheme_boundary(gf::Utf8Offset(caret_)).value();
    const std::size_t first = backward ? other : caret_;
    const std::size_t length = backward ? caret_ - other : other - caret_;
    if (length)
        replace(first, length, "");
}
void TerminalQuery::move(bool right) {
    caret_ = right ? store_.next_grapheme_boundary(gf::Utf8Offset(caret_)).value()
                   : store_.previous_grapheme_boundary(gf::Utf8Offset(caret_)).value();
}
void TerminalQuery::home() { caret_ = 0; }
void TerminalQuery::end() { caret_ = store_.utf8().size(); }
void TerminalQuery::toggle() {
    if (slots_.empty())
        return;
    std::size_t index = store_.grapheme_index(gf::Utf8Offset(caret_)).value();
    if (index == slots_.size())
        --index;
    slots_[index].wildcard = !slots_[index].wildcard;
}
SearchPattern TerminalQuery::pattern() const {
    SearchPattern result(store_.utf8());
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].wildcard)
            result.toggle(i);
    return result;
}
std::string TerminalQuery::display() const {
    const std::size_t caret = store_.grapheme_index(gf::Utf8Offset(caret_)).value();
    std::string result{};
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (i == caret)
            result += '|';
        result += slots_[i].wildcard ? "[wild]" : slots_[i].literal;
    }
    if (caret == slots_.size())
        result += '|';
    return result;
}
} // namespace swiftedit
