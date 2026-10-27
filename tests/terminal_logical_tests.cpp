#include "terminal_logical_page.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(const bool value, const char *const message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("swiftedit-logical-page-" + std::to_string(test_process_id()));
    Fixture() {
        require(std::filesystem::create_directory(directory), "Reserve unique fixture directory");
    }
    ~Fixture() {
        std::error_code ignored{};
        std::filesystem::remove_all(directory, ignored);
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
};
void check_rows(const std::string &source,
                const std::vector<swiftedit::TerminalLogicalRow> &expected) {
    const Fixture fixture{};
    const std::filesystem::path path = fixture.directory / "source.txt";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        output.close();
        require(static_cast<bool>(output), "Write exact source fixture");
    }
    swiftedit::Session session{};
    session.open(path);
    for (std::size_t budget = 1; budget <= 9; ++budget) {
        for (const std::size_t height : {1U, 2U, 3U, 300U}) {
            std::uint64_t start = 0;
            std::size_t observed = 0;
            bool more = true;
            while (more) {
                swiftedit::TerminalLogicalPage page(session, start, height);
                std::size_t steps = 0;
                bool complete = false;
                while (!complete) {
                    const std::uint64_t before = page.scanned_offset();
                    complete = page.step(session, budget);
                    require(page.scanned_offset() - before <= budget, "Bounded scan progress");
                    ++steps;
                    require(steps <= source.size() + 2, "Logical page makes bounded progress");
                }
                const std::vector<swiftedit::TerminalLogicalRow> &rows = page.result(session);
                require(!rows.empty() && rows.size() <= height, "Viewport row storage bound");
                for (const swiftedit::TerminalLogicalRow &row : rows) {
                    require(observed < expected.size(), "No extra logical rows");
                    const swiftedit::TerminalLogicalRow &wanted = expected[observed];
                    require(row.offset == wanted.offset && row.length == wanted.length &&
                                row.separator_bytes == wanted.separator_bytes,
                            "Exact logical body and separator ranges");
                    ++observed;
                }
                start = page.next(session);
                more = page.more(session);
                require(page.step(session, budget), "Completed task is idempotent");
            }
            require(observed == expected.size(), "Every logical row survives pagination");
            require(session.text() == source, "Logical scanning preserves exact source bytes");
            for (std::size_t row = 0; row < expected.size(); ++row) {
                swiftedit::TerminalLogicalPrevious previous(session, expected[row].offset, height);
                bool complete = false;
                std::size_t steps = 0;
                while (!complete) {
                    const std::uint64_t before = previous.scanned_offset();
                    complete = previous.step(session, budget);
                    require(before - previous.scanned_offset() <= budget, "Bounded backward scan");
                    ++steps;
                    require(steps <= source.size() + 2, "Backward navigation makes progress");
                }
                const std::size_t target = row >= height ? row - height : 0;
                require(previous.result(session) == expected[target].offset,
                        "Backward navigation agrees with exact logical rows at every boundary");
                require(previous.step(session, budget), "Completed backward scan is idempotent");
            }
        }
    }
}
void check_refusals() {
    swiftedit::Session session{};
    session.replace_ranges({{0, 0}}, "a\r\nb", session.stamp());
    for (const std::uint64_t offset : {1U, 2U, 4U, 5U}) {
        bool refused = false;
        try {
            const swiftedit::TerminalLogicalPage invalid(session, offset, 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Mid-line, split CRLF, unterminated EOF and out-of-range starts refused");
        refused = false;
        try {
            const swiftedit::TerminalLogicalPrevious invalid(session, offset, 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Backward scan rejects unproven logical boundaries");
    }
    for (const std::size_t height : {0U, 301U}) {
        bool refused = false;
        try {
            const swiftedit::TerminalLogicalPage invalid(session, 0, height);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Invalid viewport height refused");
    }
    swiftedit::TerminalLogicalPage pending(session, 0, 2);
    bool refused = false;
    try {
        static_cast<void>(pending.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Incomplete rows cannot become a viewport");
    for (const std::size_t budget : {0U, 8193U}) {
        refused = false;
        try {
            static_cast<void>(pending.step(session, budget));
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && pending.scanned_offset() == 0, "Invalid budget preserves scan state");
    }
    swiftedit::Session foreign{};
    foreign.replace_ranges({{0, 0}}, session.text(), foreign.stamp());
    refused = false;
    try {
        static_cast<void>(pending.step(foreign));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && pending.scanned_offset() == 0, "Identical foreign source is refused");
    swiftedit::TerminalLogicalPrevious previous(session, 3, 1);
    refused = false;
    try {
        static_cast<void>(previous.step(foreign));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && previous.scanned_offset() == 3, "Backward scan rejects identical foreign source");
    require(!previous.step(session, 1), "Backward scan splits CRLF across bounded reads");
    require(!pending.step(session, 2), "CR boundary remains pending until lookahead");
    session.replace_ranges({{0, 1}}, "z", session.stamp());
    refused = false;
    try {
        static_cast<void>(previous.step(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && previous.scanned_offset() == 2, "Backward scan rejects changed revision");
    refused = false;
    try {
        static_cast<void>(pending.step(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused && pending.scanned_offset() == 2, "Stale scan cannot consume new bytes");
    swiftedit::TerminalLogicalPage completed(session, 0, 2);
    require(completed.step(session), "Fresh task completes");
    session.reset();
    refused = false;
    try {
        static_cast<void>(completed.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Completed stale viewport cannot be published");
}
void check_large_file() {
    const Fixture fixture{};
    const std::filesystem::path path = fixture.directory / "long-line.txt";
    const std::string chunk(8192, 'x');
    {
        std::ofstream output(path, std::ios::binary);
        for (std::size_t index = 0; index < 2048; ++index)
            output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        output << "\r\nend";
        output.close();
        require(static_cast<bool>(output), "Write read-only long-line fixture");
    }
    swiftedit::Session session{};
    session.open(path);
    require(session.read_only(), "Large fixture uses actual paged read-only Session");
    {
        swiftedit::TerminalLogicalPage cancelled(session, 0, 2);
        require(!cancelled.step(session) && cancelled.scanned_offset() == 8192,
                "Long line yields after one bounded read");
    }
    require(!session.dirty(), "Dropping unfinished task cancels without source mutation");
    swiftedit::TerminalLogicalPage page(session, 0, 2);
    std::size_t steps = 0;
    bool complete = false;
    while (!complete) {
        complete = page.step(session);
        ++steps;
        require(steps <= 2049, "Large scan stays within expected read count");
    }
    const std::vector<swiftedit::TerminalLogicalRow> &rows = page.result(session);
    require(steps == 2049 && rows.size() == 2 && rows[0].offset == 0 &&
                rows[0].length == 16777216 && rows[0].separator_bytes == 2 &&
                rows[1].offset == 16777218 && rows[1].length == 3 &&
                rows[1].separator_bytes == 0 && !page.more(session),
            "Huge logical line uses bounded metadata and retains exact following row");
    {
        swiftedit::TerminalLogicalPrevious cancelled(session, rows[1].offset, 1);
        require(!cancelled.step(session) && cancelled.scanned_offset() == rows[1].offset - 8192,
                "Backward long-line navigation yields and can be dropped");
    }
    swiftedit::TerminalLogicalPrevious previous(session, rows[1].offset, 1);
    bool refused = false;
    try {
        static_cast<void>(previous.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Incomplete backward navigation cannot publish");
    for (const std::size_t budget : {0U, 8193U}) {
        refused = false;
        try {
            static_cast<void>(previous.step(session, budget));
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && previous.scanned_offset() == rows[1].offset,
                "Invalid backward budget preserves state");
    }
    steps = 0;
    do {
        complete = previous.step(session);
        ++steps;
    } while (!complete);
    require(steps == 2049 && previous.result(session) == 0 && !session.dirty(),
            "Actual read-only long line rewinds in bounded steps without content changes");
    session.reset();
    refused = false;
    try {
        static_cast<void>(previous.result(session));
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Completed backward navigation refuses changed identity");
}
} // namespace
int main() {
    try {
        check_rows("", {{0, 0, 0}});
        check_rows("abc", {{0, 3, 0}});
        check_rows("a\r\nb\rc\n", {{0, 1, 2}, {3, 1, 1}, {5, 1, 1}, {7, 0, 0}});
        check_rows("\r\r\n\n", {{0, 0, 1}, {1, 0, 2}, {3, 0, 1}, {4, 0, 0}});
        check_rows("a\r", {{0, 1, 1}, {2, 0, 0}});
        check_rows(std::string("\0\xff\r\nz", 5), {{0, 2, 2}, {4, 1, 0}});
        check_rows("\xe6\xbc\xa2\nend", {{0, 3, 1}, {4, 3, 0}});
        check_refusals();
        check_large_file();
        std::cout << "Logical pages preserve bytes/endings, bound work/storage and reject stale results.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
