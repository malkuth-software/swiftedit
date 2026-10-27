#include "session_copy.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(const bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void no_result(swiftedit::SessionCopy &copy) {
    bool refused = false;
    try {
        static_cast<void>(copy.take());
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Unfinished copy must not expose partial bytes");
}
int main() {
    try {
        using namespace swiftedit;
        Session session{};
        SessionCopy empty(session, 0, 0);
        require(empty.take().bytes().empty(), "Empty completed clipboard");
        no_result(empty);
        session.replace_ranges({{0, 0}}, "alpha beta", session.stamp());
        SessionCopy bounded(session, 1, 7);
        bool refused = false;
        try {
            bounded.step(session, 0);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && bounded.copied() == 0 && bounded.state() == CopyState::running,
                "Bad budget leaves task retryable");
        while (bounded.state() == CopyState::running)
            bounded.step(session, 2);
        const SourceClipboard selected = bounded.take();
        require(selected.bytes() == "lpha be", "Exact nonzero range without trailing bytes");
        SessionCopy cancelled(session, 0, 5);
        cancelled.step(session, 1);
        cancelled.cancel();
        no_result(cancelled);
        require(cancelled.state() == CopyState::cancelled, "Cancelled copy state");
        SessionCopy stale(session, 0, 5);
        stale.step(session, 1);
        session.replace_ranges({{0, 1}}, "A", session.stamp());
        refused = false;
        try {
            stale.step(session, 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && stale.state() == CopyState::failed, "Same-size edit invalidates copy");
        no_result(stale);
        Session other{};
        other.replace_ranges({{0, 0}}, session.text(), other.stamp());
        SessionCopy foreign(session, 0, 5);
        refused = false;
        try {
            foreign.step(other, 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && foreign.state() == CopyState::failed, "Foreign identity rejected");
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-copy-" + std::to_string(test_process_id()));
        require(std::filesystem::create_directory(directory), "Acquire unique fixture");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const std::filesystem::path path = directory / "large.txt";
        const std::string unusual("\0\xff\r\n\xe3\x80\x80", 7);
        {
            std::ofstream output(path, std::ios::binary);
            const std::string block(maximum_page, 'x');
            for (std::size_t written = 0; written < editable_limit; written += block.size())
                output.write(block.data(), static_cast<std::streamsize>(block.size()));
            output.seekp(static_cast<std::streamoff>(maximum_page - 2));
            output.write(unusual.data(), static_cast<std::streamsize>(unusual.size()));
            output.close();
            require(static_cast<bool>(output), "Write paged fixture");
        }
        session.open(path);
        require(session.read_only(), "Large fixture is read-only");
        const DocumentStamp before = session.stamp();
        SessionCopy whole(session, 0, session.size());
        while (whole.state() == CopyState::running)
            whole.step(session, maximum_page);
        const SourceClipboard full = whole.take();
        require(full.bytes().size() == editable_limit && full.bytes().front() == 'x' &&
                    full.bytes().back() == 'x' &&
                    full.bytes().substr(maximum_page - 2, unusual.size()) == unusual,
                "Full read-only selection crosses all pages without byte loss");
        SessionCopy large(session, maximum_page - 2, unusual.size());
        while (large.state() == CopyState::running) {
            const std::uint64_t previous = large.copied();
            large.step(session, 1);
            require(large.copied() == previous + 1, "Read obeys caller budget");
            if (large.state() == CopyState::running)
                no_result(large);
        }
        SourceClipboard preserved = large.take();
        require(preserved.bytes() == unusual, "NUL, illegal bytes, CRLF and split UTF-8 preserved");
        require(session.stamp().identity == before.identity &&
                    session.stamp().revision == before.revision && !session.dirty(),
                "Copy leaves read-only source unchanged");
        session.reset();
        require(preserved.bytes() == unusual, "Clipboard outlives source document");
        session.paste_range({0, 0}, preserved, session.stamp());
        require(session.text() == unusual && session.undo() && session.text().empty(),
                "Paged clipboard pastes byte-faithfully as one undoable edit");
        refused = false;
        try {
            SessionCopy outside(session, 1, UINT64_MAX);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Out-of-range and overflowing range rejected before allocation");
        std::cout << "Session copy tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
