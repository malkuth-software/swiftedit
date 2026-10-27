#include "selection_set.hpp"
#include "platform.hpp"
#include <gui_forms/text.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(const bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void refused_selection(const swiftedit::Session &session,
                       const std::vector<swiftedit::SourceRange> &ranges) {
    bool refused = false;
    try {
        const swiftedit::SelectionSet invalid(session, ranges);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    require(refused, "Invalid source selection refused");
}
void verify_context_boundaries(const swiftedit::Session &session) {
    std::string metadata = session.text();
    for (std::size_t offset = 0; offset < metadata.size();) {
        const std::size_t length = swiftedit::utf8_sequence_length(metadata, offset);
        if (length)
            offset += length;
        else {
            metadata[offset] = '\x01';
            ++offset;
        }
    }
    const gui_forms::TextStore whole(metadata);
    for (std::size_t begin = 0; begin <= metadata.size(); ++begin) {
        for (std::size_t end = begin; end <= metadata.size(); ++end) {
            const bool expected = whole.is_grapheme_boundary(gui_forms::Utf8Offset(begin)) &&
                                  whole.is_grapheme_boundary(gui_forms::Utf8Offset(end));
            bool accepted = false;
            try {
                const swiftedit::SelectionSet selection(session, {{begin, end - begin}});
                accepted = selection.can_rewrite();
            } catch (const std::runtime_error &) {
                accepted = false;
            }
            require(accepted == expected, "Line-context selection matches full-document segmentation at every byte pair");
            if (begin > 1) {
                bool parallel_accepted = false;
                bool rewritable = false;
                try {
                    const swiftedit::SelectionSet parallel(session, {{0, 1}, {begin, end - begin}});
                    parallel_accepted = true;
                    rewritable = parallel.can_rewrite();
                } catch (const std::runtime_error &) {
                    parallel_accepted = false;
                }
                require(parallel_accepted == expected, "Grouped selection acceptance matches whole-document oracle");
                if (expected) {
                    const std::size_t count = whole.grapheme_index(gui_forms::Utf8Offset(end)).value() -
                                              whole.grapheme_index(gui_forms::Utf8Offset(begin)).value();
                    require(rewritable == (count == 1), "Separate context groups retain equal-grapheme edit policy");
                }
            }
        }
    }
}
int main() {
    try {
        using namespace swiftedit;
        Session session{};
        const std::string unicode = "a \xc3\xa9 e\xcc\x81 \xf0\x9f\x98\x80";
        session.replace_ranges({{0, 0}}, unicode, session.stamp());
        const SelectionSet equal(session, {{0, 1}, {2, 2}, {5, 3}, {9, 4}});
        require(equal.can_rewrite(), "Equal grapheme counts permit unequal byte lengths");
        const std::vector<SourceClipboard> copied = equal.copy(session);
        require(copied.size() == 4 && copied[1].bytes() == "\xc3\xa9" &&
                    copied[2].bytes() == "e\xcc\x81", "Copy preserves independent original parts");
        refused_selection(session, {{3, 1}});
        refused_selection(session, {{5, 1}});
        refused_selection(session, {{0, 3}, {2, 2}});
        refused_selection(session, {{0, 0}, {0, 0}});
        refused_selection(session, {{2, 2}, {0, 1}});
        refused_selection(session, {{unicode.size(), SIZE_MAX}});
        equal.rewrite(session, "X");
        require(session.text() == "X X X X", "One replacement applied in source order");
        require(session.undo() && session.text() == unicode, "Parallel rewrite is one undo step");
        bool refused = false;
        try {
            equal.rewrite(session, "Y");
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && session.text() == unicode, "Undo does not revive stale selections");
        session.reset();
        session.replace_ranges({{0, 0}}, "\xc3\xa9 ab", session.stamp());
        const SelectionSet unequal(session, {{0, 2}, {3, 2}});
        require(!unequal.can_rewrite(), "Equal byte lengths do not imply equal character counts");
        const std::vector<SourceClipboard> unequal_copy = unequal.copy(session);
        require(unequal_copy[0].bytes() == "\xc3\xa9" && unequal_copy[1].bytes() == "ab",
                "Unequal selections remain copyable without inserted separators");
        const DocumentStamp unchanged = session.stamp();
        refused = false;
        try {
            unequal.rewrite(session, "Z");
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && session.stamp().revision == unchanged.revision &&
                    session.text() == "\xc3\xa9 ab", "Unequal rewrite refuses before mutation");
        Session other{};
        other.replace_ranges({{0, 0}}, session.text(), other.stamp());
        refused = false;
        try {
            static_cast<void>(unequal.copy(other));
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused, "Selection copy rejects a different document with identical contents");
        session.reset();
        session.replace_ranges({{0, 0}}, "a\r\nb", session.stamp());
        refused_selection(session, {{1, 1}});
        const SelectionSet insertions(session, {{0, 0}, {4, 0}});
        insertions.rewrite(session, "!");
        require(session.text() == "!a\r\nb!", "Distinct insertion points share one atomic edit");
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-selection-" + std::to_string(test_process_id()));
        require(std::filesystem::create_directory(directory), "Acquire unique fixture");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const std::filesystem::path path = directory / "bytes.txt";
        const std::string bytes("\xff \0", 3);
        {
            std::ofstream output(path, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.close();
            require(static_cast<bool>(output), "Write byte-faithful fixture");
        }
        session.open(path);
        const SelectionSet controls(session, {{0, 1}, {2, 1}});
        const std::vector<SourceClipboard> raw = controls.copy(session);
        require(controls.can_rewrite() && raw[0].bytes() == std::string("\xff", 1) &&
                    raw[1].bytes() == std::string("\0", 1),
                "Illegal bytes and controls retain source bytes and atomic character lengths");
        controls.rewrite(session, "Q");
        require(session.text() == "Q Q" && session.undo() && session.text() == bytes,
                "Replacing invalid bytes is undoable without metadata leaking into source");
        const std::string boundary_source = std::string("\n\r\ne\xcc\x81\r\n\xcc\x81\xff") +
            "\xf0\x9f\x87\xa6\xf0\x9f\x87\xa7\xf0\x9f\x87\xa8\n" +
            "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb\rZ\n\xd8\x80" "a\n";
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(boundary_source.data(), static_cast<std::streamsize>(boundary_source.size()));
            output.close();
            require(static_cast<bool>(output), "Write boundary-context fixture");
        }
        session.open(path);
        verify_context_boundaries(session);
        require(session.text() == boundary_source && !session.dirty(), "Boundary checks preserve source");
        session.reset();
        const std::string ascii = std::string("\nA\r\n\0\t\x01", 7) + "\x7f\rB\n\r\n";
        session.replace_ranges({{0, 0}}, ascii, session.stamp());
        verify_context_boundaries(session);
        require(session.text() == ascii, "ASCII fast path preserves every source control");
        std::cout << "Interactive selection policy tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
