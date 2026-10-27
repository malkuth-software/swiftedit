#include "session_word_count.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void no_result(const swiftedit::SessionWordCount &count) {
    bool refused = false;
    try {
        static_cast<void>(count.result());
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Incomplete count must not publish a result");
}
int main() {
    try {
        using namespace swiftedit;
        Session session{};
        SessionWordCount empty(session);
        require(empty.result() == 0, "Empty document count");
        session.replace_ranges({{0, 0}}, "alpha beta", session.stamp());
        SessionWordCount cancelled(session);
        cancelled.step(session, 1);
        cancelled.cancel();
        require(cancelled.state() == WordCountState::cancelled && cancelled.offset() == 1,
                "Cancellation preserves bounded progress");
        no_result(cancelled);
        SessionWordCount stale(session);
        stale.step(session, 1);
        session.replace_ranges({{0, 0}}, "new ", session.stamp());
        bool refused = false;
        try {
            stale.step(session, 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && stale.state() == WordCountState::failed,
                "Document mutation invalidates unfinished count");
        no_result(stale);
        Session other{};
        other.replace_ranges({{0, 0}}, session.text(), other.stamp());
        SessionWordCount foreign(session);
        refused = false;
        try {
            foreign.step(other, 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Foreign session rejected even with identical bytes");
        SessionWordCount bounded(session);
        refused = false;
        try {
            bounded.step(session, maximum_page + 1);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && bounded.offset() == 0 && bounded.state() == WordCountState::running,
                "Invalid budget does not consume input or poison the task");
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("swiftedit-word-count-" + std::to_string(test_process_id()));
        require(std::filesystem::create_directory(directory), "Acquire unique fixture");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const std::filesystem::path invalid_file = directory / "invalid.txt";
        {
            std::ofstream output(invalid_file, std::ios::binary);
            output << "word \xe3\x80";
            output.close();
            require(static_cast<bool>(output), "Write malformed source fixture");
        }
        Session malformed{};
        malformed.open(invalid_file);
        SessionWordCount invalid(malformed);
        invalid.step(malformed, 5);
        refused = false;
        try {
            invalid.step(malformed, 2);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && invalid.state() == WordCountState::failed,
                "Truncated final scalar fails the task after earlier successful pages");
        no_result(invalid);
        const std::filesystem::path file = directory / "large.txt";
        {
            std::ofstream output(file, std::ios::binary);
            const std::string spaces(maximum_page, ' ');
            for (std::size_t written = 0; written < editable_limit; written += spaces.size())
                output.write(spaces.data(), static_cast<std::streamsize>(spaces.size()));
            output.seekp(static_cast<std::streamoff>(maximum_page - 2));
            const std::string words = "a\xe3\x80\x80"
                                      "b";
            output.write(words.data(), static_cast<std::streamsize>(words.size()));
            output.close();
            require(static_cast<bool>(output), "Write paged fixture");
        }
        session.open(file);
        require(session.read_only(), "Fixture uses retained paged file adapter");
        const DocumentStamp before = session.stamp();
        SessionWordCount large(session);
        no_result(large);
        while (large.state() == WordCountState::running) {
            const std::uint64_t previous = large.offset();
            large.step(session, maximum_page);
            require(large.offset() > previous && large.offset() - previous <= maximum_page,
                    "Every step obeys its byte budget");
        }
        require(large.result() == 2 && large.offset() == editable_limit,
                "Paged count handles Unicode whitespace split across file pages");
        require(session.stamp().identity == before.identity &&
                    session.stamp().revision == before.revision && !session.dirty(),
                "Counting leaves document identity, revision and content unchanged");
        std::cout << "Session word count tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
