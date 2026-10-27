#include "terminal_horizontal.hpp"
#include "terminal_reveal.hpp"
#include "terminal_row.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(const bool value, const char *const message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("swiftedit-horizontal-" + std::to_string(test_process_id()));
    Fixture() {
        require(std::filesystem::create_directory(directory), "Reserve unique horizontal fixture");
    }
    ~Fixture() {
        std::error_code ignored{};
        std::filesystem::remove_all(directory, ignored);
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    std::filesystem::path write(const std::string &name, const std::string &bytes) const {
        const std::filesystem::path path = directory / name;
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(static_cast<bool>(output), "Write exact horizontal fixture");
        return path;
    }
};
void finish_logical(swiftedit::TerminalLogicalPage &page, const swiftedit::Session &session) {
    std::size_t steps = 0;
    while (!page.step(session)) {
        ++steps;
        require(steps <= session.size() / 8192 + 2, "Logical indexing progress");
    }
}
void compare_rows(const Fixture &fixture, const std::string &source) {
    const std::filesystem::path path = fixture.write("short.txt", source);
    swiftedit::TerminalBuffer buffer{};
    buffer.open(path);
    const swiftedit::Session &session = buffer.session();
    swiftedit::TerminalLogicalPage page(session, 0, 300);
    finish_logical(page, session);
    const std::vector<swiftedit::TerminalLogicalRow> &rows = page.result(session);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t left = 0; left < 32; ++left) {
            for (const std::size_t width : {1U, 2U, 7U}) {
                const swiftedit::TerminalRow expected = swiftedit::terminal_row(buffer, row, left, width);
                for (const std::size_t budget : {1U, 2U, 7U, 8192U}) {
                    swiftedit::TerminalHorizontalLine line(session, page, row, left, width);
                    bool complete = false;
                    std::size_t steps = 0;
                    while (!complete) {
                        const std::uint64_t before = line.read_offset();
                        complete = line.step(session, budget);
                        require(line.read_offset() - before <= budget, "Horizontal source read bound");
                        ++steps;
                        require(steps <= source.size() + 2, "Horizontal task makes progress");
                    }
                    const swiftedit::TerminalHorizontalFrame &actual = line.result(session);
                    require(actual.runs.size() == expected.runs.size(), "Same visible grapheme runs");
                    for (std::size_t index = 0; index < actual.runs.size(); ++index) {
                        const swiftedit::TerminalPageRun &run = actual.runs[index];
                        const swiftedit::TerminalRun &wanted = expected.runs[index];
                        require(run.row == 0 && run.column == wanted.column && run.cells == wanted.cells &&
                                    run.text == wanted.text && run.source_offset == wanted.source.offset &&
                                    run.source_length == wanted.source.length,
                                "Paged horizontal rendering matches established editable clipping policy");
                        require(run.text.find('\x1b') == std::string::npos &&
                                    run.text.find('\0') == std::string::npos,
                                "Control source never becomes an executable terminal sequence");
                    }
                    require(actual.clipped_left == expected.clipped_left &&
                                actual.clipped_right == expected.clipped_right,
                            "Horizontal clipping flags match visible content");
                    if (actual.total_cells)
                        require(*actual.total_cells == expected.total_cells, "Completed line width is exact");
                    for (const swiftedit::TerminalHorizontalCaret &caret : actual.carets) {
                        std::uint64_t offset = rows[row].offset;
                        std::uint64_t column = 0;
                        const std::uint64_t end = offset + rows[row].length;
                        while (offset < caret.source_offset && offset < end) {
                            const swiftedit::SourceRange grapheme = buffer.grapheme_range(
                                static_cast<std::size_t>(offset));
                            const std::string_view bytes(source.data() + grapheme.offset, grapheme.length);
                            const swiftedit::TerminalGlyph glyph = swiftedit::terminal_glyph(
                                bytes, static_cast<std::size_t>(column % 4));
                            offset += grapheme.length;
                            column += glyph.cells;
                        }
                        require(offset == caret.source_offset && column == caret.column,
                                "Every retained caret is an exact source grapheme boundary");
                    }
                    require(line.step(session, budget), "Completed horizontal task is idempotent");
                }
            }
        }
    }
}
void check_refusals(const Fixture &fixture) {
    swiftedit::Session session{};
    session.open(fixture.write("refusal.txt", "a\tbc"));
    swiftedit::TerminalLogicalPage pending(session, 0, 1);
    bool refused = false;
    try {
        const swiftedit::TerminalHorizontalLine invalid(session, pending, 0, 0, 10);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Incomplete logical page cannot be rendered");
    finish_logical(pending, session);
    for (const std::size_t width : {0U, 1001U}) {
        refused = false;
        try {
            const swiftedit::TerminalHorizontalLine invalid(session, pending, 0, 0, width);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Invalid horizontal width refused");
    }
    refused = false;
    try {
        const swiftedit::TerminalHorizontalLine invalid(
            session, pending, 0, std::numeric_limits<std::uint64_t>::max(), 1);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Horizontal endpoint overflow refused");
    swiftedit::TerminalHorizontalLine line(session, pending, 0, 0, 10);
    refused = false;
    try {
        static_cast<void>(line.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Incomplete horizontal frame is private");
    for (const std::size_t budget : {0U, 8193U}) {
        refused = false;
        try {
            static_cast<void>(line.step(session, budget));
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && line.read_offset() == 0, "Invalid budget preserves work");
    }
    require(!line.step(session, 1), "Unknown trailing grapheme remains unpublished");
    session.replace_ranges({{0, 1}}, "z", session.stamp());
    refused = false;
    try {
        static_cast<void>(line.step(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && line.read_offset() == 1, "Stale horizontal task cannot consume new bytes");
}
void check_context_limit(const Fixture &fixture) {
    std::string source = "a";
    for (std::size_t index = 0; index < 33000; ++index)
        source += "\xcc\x81";
    source += 'b';
    swiftedit::Session session{};
    session.open(fixture.write("cluster.txt", source));
    swiftedit::TerminalLogicalPage page(session, 0, 1);
    finish_logical(page, session);
    swiftedit::TerminalHorizontalLine line(session, page, 0, 0, 10);
    bool refused = false;
    try {
        for (std::size_t step = 0; step < 9; ++step)
            static_cast<void>(line.step(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && line.read_offset() == 65536, "Oversized grapheme refuses at bounded context");
    refused = false;
    try {
        static_cast<void>(line.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && session.text() == source, "Failure cannot publish partial text or alter source");
}
void check_large_file(const Fixture &fixture) {
    const std::filesystem::path path = fixture.directory / "large.txt";
    {
        const std::string chunk(8192, 'x');
        std::ofstream output(path, std::ios::binary);
        for (std::size_t index = 0; index < 2048; ++index)
            output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        output << "\r\nend";
        output.close();
        require(static_cast<bool>(output), "Write read-only horizontal fixture");
    }
    swiftedit::Session session{};
    session.open(path);
    require(session.read_only(), "Horizontal renderer uses an actual read-only Session");
    swiftedit::TerminalLogicalPage page(session, 0, 2);
    finish_logical(page, session);
    {
        swiftedit::TerminalHorizontalLine cancelled(session, page, 0, 8190, 12);
        require(!cancelled.step(session) && cancelled.read_offset() == 8192,
                "Horizontal seek yields at the read budget before its target");
    }
    require(!session.dirty(), "Dropping unfinished horizontal work preserves source");
    swiftedit::TerminalHorizontalLine line(session, page, 0, 8190, 12);
    require(!line.step(session) && line.step(session), "Viewport prefix needs only two bounded reads");
    const swiftedit::TerminalHorizontalFrame &frame = line.result(session);
    require(frame.runs.size() == 12 && line.read_offset() == 16384 && !frame.total_cells &&
                frame.clipped_left && frame.clipped_right,
            "Long-line rendering stops after the visible horizontal window");
    for (std::size_t index = 0; index < frame.runs.size(); ++index) {
        const swiftedit::TerminalPageRun &run = frame.runs[index];
        require(run.column == index && run.source_offset == 8190 + index &&
                    run.source_length == 1 && run.cells == 1 && run.text == "x",
                "Large-file horizontal source mapping remains exact");
    }
    swiftedit::TerminalHorizontalLine following(session, page, 1, 0, 10);
    require(following.step(session) && following.result(session).total_cells == 3,
            "Following short logical row renders independently");
    swiftedit::TerminalHorizontalLine caret_probe(session, page, 0, 0, 1, 8193);
    require(!caret_probe.step(session) && caret_probe.read_offset() == 8192,
            "Off-screen caret lookup yields before reading its target");
    require(caret_probe.step(session) && caret_probe.result(session).source_caret_column == 8193 &&
                caret_probe.result(session).runs.size() == 1 && caret_probe.read_offset() == 16384,
            "Actual read-only caret lookup stops at its target without retaining the long prefix");
    swiftedit::TerminalNoWrapReveal reveal(session, 8193, 0, 12, 2);
    std::size_t reveal_steps = 0;
    while (!reveal.step(session)) {
        ++reveal_steps;
        require(reveal_steps < 5000, "Read-only reveal phases make bounded progress");
    }
    require(reveal.left(session) == 8182 && reveal.caret(session).column == 11 &&
                reveal.viewport(session).result(session).size() == 2 && !session.dirty(),
            "Actual read-only reveal preserves source and exposes requested caret");
    session.reset();
    bool refused = false;
    try {
        static_cast<void>(line.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Completed horizontal result cannot outlive its source identity");
}
void check_viewport(const Fixture &fixture) {
    const std::string source = "abcdef\r\n\tZ\nlast\n";
    swiftedit::Session session{};
    session.open(fixture.write("viewport.txt", source));
    swiftedit::TerminalHorizontalPage page(session, 0, 2, 2, 3);
    bool refused = false;
    try {
        static_cast<void>(page.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Incomplete viewport remains unpublished");
    std::size_t steps = 0;
    while (!page.step(session, 1)) {
        ++steps;
        require(steps < 50, "Cooperative viewport advances");
    }
    const std::vector<swiftedit::TerminalHorizontalFrame> &frames = page.result(session);
    require(frames.size() == 2 && frames[0].runs.size() == 3 &&
                frames[0].runs[0].text == "c" && frames[0].runs[2].text == "e",
            "Viewport retains exact horizontal slice");
    require(frames[1].runs.size() == 2 && frames[1].runs[0].text == "  " &&
                frames[1].runs[1].text == "Z" && frames[1].runs[1].column == 2,
            "Rows share horizontal coordinates and inert tab expansion");
    require(page.rows(session)[1].offset == 8 && page.next(session) == 11 && page.more(session),
            "Viewport pagination retains logical CRLF offsets");
    swiftedit::TerminalHorizontalPage tail(session, page.next(session), 2, 0, 10);
    while (!tail.step(session, 2)) {}
    require(tail.result(session).size() == 2 && tail.result(session)[1].total_cells == 0 &&
                !tail.more(session) && tail.next(session) == source.size(),
            "Last separator retains empty EOF row");
    {
        swiftedit::TerminalHorizontalPage cancelled(session, 0, 2, 0, 10);
        require(!cancelled.step(session, 1), "Viewport cancellation occurs between bounded steps");
    }
    require(page.result(session)[0].runs[0].text == "c" && session.text() == source,
            "Cancelling replacement leaves published viewport and source intact");
    session.reset();
    refused = false;
    try {
        static_cast<void>(page.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Viewport refuses stale source identity");
}
void check_source_caret(const Fixture &fixture) {
    // Tab reaches column 4, Han reaches 6, combining sequence reaches 7.
    const std::string source = "a\t\xe6\xbc\xa2" "e\xcc\x81z";
    swiftedit::Session session{};
    session.open(fixture.write("source-caret.txt", source));
    swiftedit::TerminalLogicalPage page(session, 0, 1);
    finish_logical(page, session);
    const std::vector<swiftedit::TerminalHorizontalCaret> expected = {
        {0, 0}, {1, 1}, {2, 4}, {5, 6}, {8, 7}, {9, 8}};
    for (const swiftedit::TerminalHorizontalCaret caret : expected) {
        for (const std::size_t budget : {1U, 2U, 8192U}) {
            swiftedit::TerminalHorizontalLine line(session, page, 0, 0, 1, caret.source_offset);
            while (!line.step(session, budget)) {}
            const swiftedit::TerminalHorizontalFrame &frame = line.result(session);
            require(frame.source_caret_column == caret.column && frame.runs.size() == 1 &&
                        frame.runs[0].text == "a",
                    "Off-screen caret lookup preserves bounded visible output and exact columns");
        }
    }
    for (const std::uint64_t offset : {3U, 4U, 6U, 7U}) {
        swiftedit::TerminalHorizontalLine line(session, page, 0, 0, 1, offset);
        bool refused = false;
        try {
            while (!line.step(session, 1)) {}
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Source caret cannot split UTF-8 or a combining grapheme");
        refused = false;
        try {
            static_cast<void>(line.result(session));
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Invalid caret cannot publish partial visible output");
    }
    session.open(fixture.write("empty-caret.txt", ""));
    swiftedit::TerminalLogicalPage empty(session, 0, 1);
    finish_logical(empty, session);
    swiftedit::TerminalHorizontalLine line(session, empty, 0, 0, 10, 0);
    require(line.step(session) && line.result(session).source_caret_column == 0,
            "Empty logical row retains its source caret");
}
void finish_reveal(swiftedit::TerminalNoWrapReveal &task, const swiftedit::Session &session) {
    std::size_t steps = 0;
    while (!task.step(session)) {
        ++steps;
        require(steps < 10000, "Reveal advances through bounded preparation phases");
    }
}
void check_reveal(const Fixture &fixture) {
    const std::string source = "first\r\na\t\xe6\xbc\xa2" "e\xcc\x81z\nlast\n";
    swiftedit::Session session{};
    session.open(fixture.write("reveal.txt", source));
    swiftedit::TerminalNoWrapReveal task(session, 15, 0, 4, 3, 0);
    bool refused = false;
    try {
        static_cast<void>(task.viewport(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Reveal does not expose an incomplete replacement");
    finish_reveal(task, session);
    const swiftedit::TerminalPageCaret caret = task.caret(session);
    require(caret.row == 1 && caret.column == 3 && task.left(session) == 4 && task.top(session) == 0,
            "Reveal retains old vertical top and scrolls horizontally by display cells");
    const swiftedit::TerminalHorizontalPage &page = task.viewport(session);
    require(page.rows(session)[1].offset == 7 && page.result(session)[1].runs[0].source_offset == 9,
            "Revealed viewport maps the wide glyph to exact source bytes");
    swiftedit::TerminalNoWrapReveal resized(session, 15, task.left(session), 2, 1, task.top(session));
    finish_reveal(resized, session);
    require(resized.top(session) == 7 && resized.left(session) == 6 &&
                resized.caret(session).row == 0 && resized.caret(session).column == 1,
            "Narrower shorter viewport reveals the same source caret");
    {
        swiftedit::TerminalNoWrapReveal cancelled(session, 0, 0, 4, 3);
        require(!cancelled.step(session), "Reveal can be cancelled between phases");
    }
    require(task.caret(session).row == 1 && session.text() == source,
            "Cancelled replacement preserves previous viewport and source");
    for (const std::uint64_t offset : {6U, 10U, 13U}) {
        swiftedit::TerminalNoWrapReveal invalid(session, offset, 0, 4, 3);
        refused = false;
        try {
            finish_reveal(invalid, session);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Reveal refuses split CRLF, UTF-8 and combining boundaries");
        refused = false;
        try {
            static_cast<void>(invalid.step(session));
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Failed reveal cannot resume private partial work");
    }
    swiftedit::TerminalNoWrapReveal eof(session, source.size(), 0, 4, 3);
    finish_reveal(eof, session);
    require(eof.caret(session).row == 0 && eof.caret(session).column == 0 &&
                eof.viewport(session).result(session).size() == 1,
            "Trailing empty EOF line is a valid revealed caret");
    session.reset();
    refused = false;
    try {
        static_cast<void>(task.viewport(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Completed reveal refuses a changed source identity");
}
void check_logical_movement(const Fixture &fixture) {
    swiftedit::Session session{};
    session.open(fixture.write("logical-move.txt", "ab\r\n\t\xe6\xbc\xa2\n\nz"));
    struct Case {
        std::uint64_t caret{}, column{};
        bool down{};
        std::size_t count{};
        std::uint64_t target{};
    };
    const std::vector<Case> cases = {
        {10, 8, false, 1, 9}, {0, 5, true, 1, 5}, {4, 4, true, 1, 9},
        {10, 8, true, 1, 11}, {0, 1, false, 1, 1},
        {0, 0, true, 300, 10}, {11, 9, false, 300, 2}};
    for (const Case scenario : cases) {
        swiftedit::TerminalNoWrapMove move(session, scenario.caret, scenario.column,
                                         scenario.down, scenario.count);
        std::size_t steps = 0;
        while (!move.step(session)) {
            ++steps;
            require(steps < 100, "Logical movement advances through phases");
        }
        require(move.result(session) == scenario.target,
                "Logical movement handles wide glyphs, empty rows and clamped source edges");
    }
    swiftedit::TerminalNoWrapMove pending(session, 0, 0, true, 1);
    require(!pending.step(session), "Logical movement remains cancellable before publication");
    session.reset();
    bool refused = false;
    try {
        static_cast<void>(pending.step(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Logical movement rejects changed source identity");
}
void check_reused_reveal(const Fixture &fixture) {
    swiftedit::Session session{};
    session.open(fixture.write("reuse.txt", "a\t\xe6\xbc\xa2" "e\xcc\x81z\n\x1b\tend\n"));
    for (const std::uint64_t target : {0U, 1U, 2U, 5U, 8U, 9U, 10U, 11U}) {
        std::unique_ptr<swiftedit::TerminalNoWrapReveal> previous =
            std::make_unique<swiftedit::TerminalNoWrapReveal>(session, 8, 0, 4, 3);
        finish_reveal(*previous, session);
        const std::uint64_t left = (*previous).left(session);
        const std::uint64_t top = (*previous).top(session);
        swiftedit::TerminalNoWrapReveal reused(session, target, left, 4, 3, top, previous.get());
        previous.reset();
        finish_reveal(reused, session);
        swiftedit::TerminalNoWrapReveal fresh(session, target, left, 4, 3, top);
        finish_reveal(fresh, session);
        require(reused.left(session) == fresh.left(session) && reused.top(session) == fresh.top(session) &&
                    reused.caret(session).row == fresh.caret(session).row &&
                    reused.caret(session).column == fresh.caret(session).column,
                "Reused reveal matches fresh coordinates after previous owner destruction");
        const swiftedit::TerminalHorizontalPage &actual = reused.viewport(session);
        const swiftedit::TerminalHorizontalPage &expected = fresh.viewport(session);
        const std::vector<swiftedit::TerminalHorizontalFrame> &actual_rows = actual.result(session);
        const std::vector<swiftedit::TerminalHorizontalFrame> &expected_rows = expected.result(session);
        require(actual_rows.size() == expected_rows.size(), "Reused row count is exact");
        for (std::size_t row = 0; row < actual_rows.size(); ++row) {
            require(actual_rows[row].runs.size() == expected_rows[row].runs.size(), "Reused run count is exact");
            for (std::size_t index = 0; index < actual_rows[row].runs.size(); ++index) {
                const swiftedit::TerminalPageRun &a = actual_rows[row].runs[index];
                const swiftedit::TerminalPageRun &b = expected_rows[row].runs[index];
                require(a.column == b.column && a.cells == b.cells && a.text == b.text &&
                            a.source_offset == b.source_offset && a.source_length == b.source_length &&
                            a.starts_grapheme == b.starts_grapheme && a.ends_grapheme == b.ends_grapheme,
                        "Reused rendering preserves exact source and clipped grapheme mappings");
            }
        }
    }
    swiftedit::TerminalNoWrapReveal previous(session, 8, 0, 4, 3);
    finish_reveal(previous, session);
    session.reset();
    bool refused = false;
    try {
        const swiftedit::TerminalNoWrapReveal invalid(session, 0, 0, 4, 3, 0, &previous);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Reuse cannot admit metadata from an older source");
}
void check_ascii_prefix(const Fixture &fixture) {
    std::string ascii{};
    for (std::size_t value = 0; value < 128; ++value) {
        const unsigned char byte = static_cast<unsigned char>(value);
        const std::string source(1, static_cast<char>(byte));
        ascii += source;
        for (std::size_t column = 0; column < 8; ++column) {
            const swiftedit::TerminalGlyph glyph = swiftedit::terminal_glyph(source, column);
            require(swiftedit::terminal_ascii_cells(byte, column) == glyph.cells,
                    "Width-only ASCII policy matches the rendered glyph for every byte");
        }
    }
    compare_rows(fixture, ascii);
    swiftedit::Session session{};
    session.open(fixture.write("ascii-boundary.txt", std::string(8191, 'x') + "e\xcc\x81z"));
    swiftedit::TerminalLogicalPage page(session, 0, 1);
    finish_logical(page, session);
    swiftedit::TerminalHorizontalLine valid(session, page, 0, 8190, 8, 8194);
    require(!valid.step(session) && valid.step(session), "Prefix scan retains the last unfinished base byte");
    const swiftedit::TerminalHorizontalFrame &frame = valid.result(session);
    require(frame.source_caret_column == 8192 && frame.runs.size() == 3 &&
                frame.runs[1].text == "e\xcc\x81" && frame.runs[1].source_length == 3,
            "Combining sequence spanning an ASCII read boundary stays atomic");
    swiftedit::TerminalHorizontalLine invalid(session, page, 0, 0, 1, 8192);
    bool refused = false;
    try {
        while (!invalid.step(session)) {}
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Off-screen prefix optimization cannot admit a split combining caret");
}
} // namespace
int main() {
    try {
        const Fixture fixture{};
        compare_rows(fixture, "");
        compare_rows(fixture, "ordinary text\r\n\tend\n");
        compare_rows(fixture, "a\t\xe6\xbc\xa2" "e\xcc\x81-\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
        compare_rows(fixture, std::string("\0\xff\x1b\tX", 5));
        compare_rows(fixture, "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8\xf0\x9f\x87\xa8\xf0\x9f\x87\xa6-end");
        check_refusals(fixture);
        check_context_limit(fixture);
        check_large_file(fixture);
        check_viewport(fixture);
        check_source_caret(fixture);
        check_reveal(fixture);
        check_logical_movement(fixture);
        check_reused_reveal(fixture);
        check_ascii_prefix(fixture);
        std::cout << "Bounded horizontal rendering matches source, width and inert-control policies.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
