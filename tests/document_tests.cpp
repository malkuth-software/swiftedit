#include "document.hpp"
#include "word_count.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "platform.hpp"

using namespace notepad;
void require(bool test, const char *label) {
    if (!test)
        throw std::runtime_error(label);
}

void raw(const std::filesystem::path &path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), text.size());
}
int main() {
    try {
        struct WordFixture {
            std::string text{};
            std::size_t expected{};
        };
        const WordFixture word_fixtures[] = {{"", 0},
                                             {" \r\n\t", 0},
                                             {"one\r\ntwo\rthree\nfour", 4},
                                             {"don't re-enter a,b", 3},
                                             {"e\xcc\x81 \xf0\x9f\x98\x80", 2},
                                             {"a\xc2\xa0"
                                              "b\xe2\x80\xaf"
                                              "c\xe3\x80\x80"
                                              "d",
                                              4},
                                             {"a\xe2\x80\x8b"
                                              "b",
                                              1},
                                             {"\xe4\xb8\xad\xe6\x96\x87", 1},
                                             {std::string("a\0b c", 5), 2},
                                             {"\xf4\x8f\xbf\xbf", 1}};
        for (const WordFixture &fixture : word_fixtures) {
            const std::size_t words = word_count(fixture.text);
            require(words == fixture.expected, "Whitespace word-count policy");
            for (std::size_t split = 0; split <= fixture.text.size(); ++split) {
                WordCounter counter{};
                const std::string_view text = fixture.text;
                counter.append(text.substr(0, split));
                counter.append(text.substr(split));
                require(counter.finish() == fixture.expected,
                        "Word and UTF-8 boundaries survive every two-chunk split");
            }
            WordCounter single_bytes{};
            for (std::size_t index = 0; index < fixture.text.size(); ++index)
                single_bytes.append(std::string_view(fixture.text).substr(index, 1));
            require(single_bytes.finish() == fixture.expected,
                    "Word count survives single-byte input chunks");
        }
        const std::string invalid_words[] = {"\x80",
                                             "\xc0\xaf",
                                             "\xe0\x80\x80",
                                             "\xed\xa0\x80",
                                             "\xf4\x90\x80\x80",
                                             "\xf5\x80\x80\x80",
                                             "\xf0\x9f",
                                             "\xc2x"};
        for (const std::string &bytes : invalid_words) {
            for (std::size_t split = 0; split <= bytes.size(); ++split) {
                WordCounter counter{};
                const std::string_view text = bytes;
                bool refused = false;
                try {
                    counter.append("two words ");
                    counter.append(text.substr(0, split));
                    counter.append(text.substr(split));
                    static_cast<void>(counter.finish());
                } catch (const std::runtime_error &) {
                    refused = true;
                }
                require(refused, "Malformed streamed UTF-8 is refused at every split");
                refused = false;
                try {
                    static_cast<void>(counter.finish());
                } catch (const std::runtime_error &) {
                    refused = true;
                }
                require(refused, "Failed counter never publishes a partial count");
            }
        }
        WordCounter finished{};
        finished.append("one");
        require(finished.finish() == 1 && finished.finish() == 1, "Finished count is stable");
        bool late_append_refused = false;
        try {
            finished.append(" two");
        } catch (const std::runtime_error &) {
            late_append_refused = true;
        }
        require(late_append_refused, "Finished counter refuses additional input");
        bool malformed_count_refused = false;
        try {
            static_cast<void>(word_count("\xff"));
        } catch (const std::runtime_error &) {
            malformed_count_refused = true;
        }
        require(malformed_count_refused, "Word count refuses malformed UTF-8");
        for (const Encoding e :
             {Encoding::utf8, Encoding::utf8_bom, Encoding::utf16_le, Encoding::utf16_be}) {
            for (const std::string text :
                 {std::string(), std::string("alpha\r\nbeta\ngamma\rdelta\t"),
                  std::string("A\xc3\xa9\xf0\x9f\x98\x80 e\xcc\x81")}) {
                const std::string bytes = encode(text, e);
                const Decoded value = decode(bytes);
                require(value.text == text && value.encoding == e, "Encoding round trip");
                const std::string calculated_1 = encode(value.text, value.encoding);
                require(calculated_1 == bytes, "Byte round trip");
            }
        }
        {
            bool refused = false;
            try {
                static_cast<void>(decode(std::string("\xc0\x80", 2)));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                static_cast<void>(decode(std::string("\xff\xfe\0\xd8", 4)));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                static_cast<void>(decode(std::string("\xff\xfe\0\0", 4)));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                static_cast<void>(decode(std::string("x\0y", 3)));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                static_cast<void>(decode(std::string(maximum_bytes + 1, 'a')));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        const std::string calculated_2 = newline_name("a\r\nb\nc\r");
        require(calculated_2 == "Mixed (preserved)", "Mixed newline observation");
        const std::string calculated_3 = preferred_newline("a\nb");
        require(calculated_3 == "\n", "First newline");
        const std::optional<std::size_t> calculated_4 = find_literal("abc", "", 0, true);
        require(!calculated_4, "Empty search");
        const std::optional<std::size_t> calculated_5 = find_literal("AbA", "ba", 0, false);
        require(calculated_5 == 1, "ASCII insensitive search");
        const Replacement replaced = replace_all("aaaa", "aa", "b", true);
        require(replaced.text == "bb" && replaced.count == 2, "Nonoverlapping replace all");
        const Replacement calculated_6 = replace_all("a", "a", "aa", true);
        require(calculated_6.text == "aa", "No recursive replacement");
        const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                          ("notepad-tests-" + std::to_string(test_process_id()));
        const bool observed_5 = std::filesystem::create_directory(dir);
        require(observed_5, "Unique fixture directory");
        struct Cleanup {
            std::filesystem::path dir{};
            ~Cleanup() {
                std::error_code ec{};
                std::filesystem::remove_all(dir, ec);
            }
        } cleanup{dir};
        const std::filesystem::path path = dir / L"test-\u00e9.txt";
        Document doc{};
        doc.save(path, "one\r\n", {});
        const notepad::FileSnapshot observed_1 = read_file(path);
        require(observed_1.bytes == "one\r\n", "Create exact bytes");
        doc.save(path, "two\r\n", doc.snapshot);
        const notepad::FileSnapshot observed_2 = read_file(path);
        require(observed_2.bytes == "two\r\n", "Replace exact bytes");
        raw(path, "external");
        {
            bool refused = false;
            try {
                doc.save(path, "lost", doc.snapshot);
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        const notepad::FileSnapshot observed_3 = read_file(path);
        require(observed_3.bytes == "external", "Conflict preserves external data");
        {
            bool refused = false;
            try {
                static_cast<void>(write_file(path, "collision", {}));
            } catch (const std::exception &) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        const std::filesystem::path bad = dir / "bad.txt";
        raw(bad, std::string("\xff", 1));
        const std::string original = doc.saved_text;
        {
            bool refused = false;
            try {
                doc.open(bad);
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        require(doc.saved_text == original, "Failed open retains document");
        const FileSnapshot observed = read_file(path);
        set_test_read_only(path, true);
        {
            bool refused = false;
            try {
                static_cast<void>(write_file(path, "readonly", observed));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        set_test_read_only(path, false);
        const std::filesystem::path hard = dir / "hard.txt";
        std::error_code hardlink_error{};
        std::filesystem::create_hard_link(path, hard, hardlink_error);
        require(!hardlink_error, "Create hardlink fixture");
        {
            bool refused = false;
            try {
                static_cast<void>(read_file(path));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        std::filesystem::remove(hard);
        std::filesystem::remove(path);
        {
            bool refused = false;
            try {
                static_cast<void>(write_file(path, "deleted", observed));
            } catch (const std::exception &failure) {
                refused = true;
            }
            require(refused, "Expected refusal");
        }
        const bool observed_6 = std::filesystem::exists(path);
        require(!observed_6, "Deleted source not resurrected");
        const std::filesystem::path utf16 = dir / "utf16.txt";
        raw(utf16, encode("alpha\r\nbeta\n", Encoding::utf16_be));
        doc.open(utf16);
        doc.save(utf16, doc.saved_text + "tail", doc.snapshot);
        const FileSnapshot saved_utf16 = read_file(utf16);
        const std::string expected_utf16 = encode("alpha\r\nbeta\ntail", Encoding::utf16_be);
        require(saved_utf16.bytes == expected_utf16, "UTF16 save preserves format");
        const FileSnapshot locked = read_file(utf16);
        {
            const TestWriter writer(utf16);
            {
                bool refused = false;
                try {
                    static_cast<void>(write_file(utf16, "contended", locked));
                } catch (const std::exception &failure) {
                    refused = true;
                }
                require(refused, "Expected refusal");
            }
        }
        const notepad::FileSnapshot observed_4 = read_file(utf16);
        require(observed_4 == locked, "Open writer refusal preserves original");
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator(dir))
            require(!entry.path().filename().wstring().starts_with(L".notepad-"),
                    "Successful saves clean temporary artifacts");
        std::cout << "Document tests passed: Unicode, exact bytes, search, safe publication, "
                     "conflicts, malformed input, read-only and hard links.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
