#include "terminal_row.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void compare(const swiftedit::TerminalRow &cached, const swiftedit::TerminalRow &reference) {
    check(cached.caret_column == reference.caret_column &&
              cached.total_cells == reference.total_cells &&
              cached.clipped_left == reference.clipped_left &&
              cached.clipped_right == reference.clipped_right &&
              cached.runs.size() == reference.runs.size(),
          "Cached row geometry differs from complete source traversal");
    for (std::size_t index = 0; index < cached.runs.size(); ++index) {
        const swiftedit::TerminalRun &actual = cached.runs[index];
        const swiftedit::TerminalRun &expected = reference.runs[index];
        check(actual.column == expected.column && actual.cells == expected.cells &&
                  actual.text == expected.text && actual.selected == expected.selected &&
                  actual.source.offset == expected.source.offset &&
                  actual.source.length == expected.source.length,
              "Cached row content or source mapping differs");
    }
}
void compare_view(swiftedit::TerminalRowCache &cache, swiftedit::TerminalBuffer &buffer,
                  std::size_t line, std::size_t column, std::size_t width) {
    const swiftedit::TerminalRow cached = cache.row(buffer, line, column, width);
    const swiftedit::TerminalRow reference = swiftedit::terminal_row(buffer, line, column, width);
    compare(cached, reference);
}
} // namespace
int main() {
    try {
        swiftedit::TerminalBuffer buffer{};
        swiftedit::TerminalRowCache cache{};
        compare_view(cache, buffer, 0, 0, 80);
        std::string mixed{};
        for (std::size_t index = 0; index < 150; ++index)
            mixed += "a\t\xe7\x95\x8c"
                     "e\xcc\x81\x1b\xf0\x9f\x98\x80";
        buffer.insert(mixed);
        buffer.select_all();
        const swiftedit::TerminalRow full = swiftedit::terminal_row(buffer, 0, 0, 80);
        for (std::size_t column = 0; column < full.total_cells + 100; column += 17) {
            compare_view(cache, buffer, 0, column, 1);
            compare_view(cache, buffer, 0, column, 7);
            compare_view(cache, buffer, 0, column, 80);
        }
        buffer.move(swiftedit::TerminalMotion::document_start);
        for (std::size_t index = 0; index < 902; ++index) {
            const swiftedit::TerminalSelection selection = buffer.selection();
            const std::size_t column = cache.source_column(buffer, 0, selection.caret);
            const swiftedit::TerminalRow reference = swiftedit::terminal_row(buffer, 0, column, 1);
            check(reference.caret_column == 0, "Sparse source lookup retains exact cell position");
            compare_view(cache, buffer, 0, column, 7);
            buffer.move(swiftedit::TerminalMotion::right);
        }
        bool split_refused = false;
        try {
            static_cast<void>(cache.source_column(buffer, 0, 3));
        } catch (const std::exception &) {
            split_refused = true;
        }
        check(split_refused, "Cell lookup must reject a source offset inside a UTF-8 grapheme");
        buffer.insert("changed");
        compare_view(cache, buffer, 0, full.total_cells - 10, 80);
        const bool undone = buffer.undo();
        check(undone, "Changed source can be undone");
        compare_view(cache, buffer, 0, full.total_cells - 10, 80);
        buffer.reset(true);
        buffer.insert("different");
        compare_view(cache, buffer, 0, 0, 80);
        buffer.reset(true);
        std::string lines{};
        for (std::size_t index = 0; index < 330; ++index)
            lines += std::to_string(index) + "\tline\r\n";
        buffer.insert(lines);
        for (std::size_t line = 0; line < buffer.line_count(); ++line)
            compare_view(cache, buffer, line, 1, 80);
        compare_view(cache, buffer, 0, 0, 80);
        bool invalid_width_refused = false;
        try {
            static_cast<void>(cache.row(buffer, 0, 0, 0));
        } catch (const std::exception &) {
            invalid_width_refused = true;
        }
        check(invalid_width_refused, "Cache retains viewport width validation");
        std::cout << "Terminal sparse row index tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
