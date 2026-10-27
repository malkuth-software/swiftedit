#include "context.hpp"
#include <iostream>
#include <fstream>
#include "platform.hpp"
#include <stdexcept>
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        swiftedit::Session session{};
        const std::string source = "a\r\n\r\n \n\nb\r\n\r\n";
        session.replace_ranges({{0, 0}}, source, session.stamp());
        for (std::size_t budget = 1; budget <= source.size(); ++budget) {
            swiftedit::ContextCursor cursor{};
            cursor.reset(session);
            std::string rebuilt{};
            std::vector<swiftedit::BlankAnchor> blanks{};
            for (;;) {
                const swiftedit::ContextPage page = cursor.next(session, budget);
                check(page.content.bytes.size() <= budget, "Source reads stay bounded");
                rebuilt += page.content.bytes;
                blanks.insert(blanks.end(), page.blanks.begin(), page.blanks.end());
                if (page.content.next == page.content.size)
                    break;
            }
            check(rebuilt == source, "Metadata never changes source bytes");
            check(blanks.size() == 3 && blanks[0].line == 2 && blanks[0].offset == 3 &&
                      blanks[1].line == 4 && blanks[1].offset == 7 && blanks[2].line == 6 &&
                      blanks[2].offset == 11,
                  "CRLF split across pages counts once; whitespace and EOF are not anchors");
            const swiftedit::ContextPage eof = cursor.next(session, budget);
            check(eof.content.bytes.empty() && eof.blanks.empty(),
                  "Repeated EOF has no invented anchor");
        }
        const std::string marker = swiftedit::blank_marker({3, 2});
        check(marker == "\r\rL2\r\r" && swiftedit::escape_field(marker) == "\\x0d\\x0dL2\\x0d\\x0d",
              "Marker grammar travels as escaped metadata");
        bool marker_refused = false;
        try {
            session.replace_ranges({{0, 0}}, marker, session.stamp());
        } catch (const std::exception &) {
            marker_refused = true;
        }
        check(marker_refused && session.text() == source, "Echoed marker cannot mutate document");
        swiftedit::ContextCursor stale{};
        stale.reset(session);
        session.replace_ranges({{0, 1}}, "edited", session.stamp());
        bool stale_refused = false;
        try {
            static_cast<void>(stale.next(session));
        } catch (const std::exception &) {
            stale_refused = true;
        }
        check(stale_refused, "Edits revoke context progress instead of mixing line numbering");
        stale.reset(session);
        swiftedit::Session foreign{};
        bool foreign_refused = false;
        try {
            static_cast<void>(stale.next(foreign));
        } catch (const std::exception &) {
            foreign_refused = true;
        }
        check(foreign_refused, "Other document identity cannot reuse context cursor");
        swiftedit::ContextCursor empty{};
        empty.reset(foreign);
        const swiftedit::ContextPage empty_page = empty.next(foreign);
        check(empty_page.blanks.empty(), "Empty document has no synthetic blank line");
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("swiftedit-context-" + std::to_string(test_process_id()));
        check(std::filesystem::create_directory(dir), "Unique context fixture");
        struct Cleanup {
            std::filesystem::path path{};
            ~Cleanup() {
                std::error_code error{};
                std::filesystem::remove_all(path, error);
            }
        } cleanup{dir};
        const std::filesystem::path path = dir / "large.txt";
        {
            std::ofstream file(path, std::ios::binary);
            file << "\r\rL2\r\r";
            file.seekp(swiftedit::editable_limit - 1);
            file.put('x');
            check(static_cast<bool>(file), "Large context fixture written");
        }
        swiftedit::Session large{};
        large.open(path);
        check(large.read_only(), "Large context uses paged file");
        swiftedit::ContextCursor large_cursor{};
        large_cursor.reset(large);
        const swiftedit::ContextPage large_page = large_cursor.next(large, 4);
        check(large_page.content.bytes == "\r\rL2" && large_page.blanks.size() == 2 &&
                  large_page.blanks[0].line == 1 && large_page.blanks[1].line == 2,
              "Existing marker-like source remains literal bytes, separate from generated anchors");
        std::cout << "Bounded context and blank-line metadata tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
