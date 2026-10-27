#pragma once
#include "search.hpp"
namespace swiftedit {
class TerminalQuery {
public:
    void insert(std::string_view);
    void erase(bool backward);
    void move(bool right);
    void home();
    void end();
    void toggle();
    SearchPattern pattern() const;
    std::string display() const;
    std::string_view text() const { return store_.utf8(); }

private:
    void replace(std::size_t start, std::size_t length, std::string_view);
    gui_forms::TextStore store_{};
    std::vector<SearchSlot> slots_{};
    std::size_t caret_{};
};
} // namespace swiftedit
