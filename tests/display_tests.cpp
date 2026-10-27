#include "display.hpp"
#include "search.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        const std::string source =
            std::string("A\0", 2) + "\xff\r\n" + "\xe2\x80\xae" + "e\xcc\x81";
        const swiftedit::DisplayPage page(source);
        check(page.text() == "A[U+0000][BYTE FF]\r\n[U+202E]e\xcc\x81",
              "Controls and invalid bytes have inert visible labels");
        for (const swiftedit::DisplayUnit &unit : page.units()) {
            const swiftedit::SourceRange mapped = page.source_range(unit.display);
            check(mapped.offset == unit.source.offset && mapped.length == unit.source.length,
                  "Every display unit maps exactly to source");
            const std::size_t offset = page.display_offset(unit.source.offset);
            check(offset == unit.display.offset, "Source mapping round trip");
        }
        bool partial_refused = false;
        try {
            static_cast<void>(page.source_range({2, 1}));
        } catch (const std::runtime_error &) {
            partial_refused = true;
        }
        check(partial_refused, "Partial control label cannot corrupt source");
        const swiftedit::DisplayPage empty("");
        const swiftedit::SourceRange end = empty.source_range({0, 0});
        check(end.offset == 0 && end.length == 0, "Empty insertion maps correctly");
        const swiftedit::DisplayPage literal("[BYTE FF]");
        check(literal.units().size() == 9, "Literal label text remains ordinary source text");
        const std::string clusters = "e\xcc\x81\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
        const swiftedit::DisplayPage graphemes(clusters);
        check(graphemes.text() == clusters && graphemes.units().size() == 2,
              "Combining text and joined emoji preserve bytes as complete display units");
        for (std::size_t interior = 1; interior < clusters.size(); ++interior) {
            if (interior == 3)
                continue;
            bool forward_refused = false;
            bool inverse_refused = false;
            try { static_cast<void>(graphemes.source_range({0, interior})); }
            catch (const std::runtime_error &) { forward_refused = true; }
            try { static_cast<void>(graphemes.display_offset(interior)); }
            catch (const std::runtime_error &) { inverse_refused = true; }
            check(forward_refused && inverse_refused, "Both mapping directions reject grapheme interiors");
        }
        const swiftedit::DisplayPage invalid_neighbors(std::string("e\xcc\x81\xff", 4) + "\xcc\x81");
        check(invalid_neighbors.units().size() == 3 &&
                  invalid_neighbors.units()[1].kind == swiftedit::DisplayKind::illegal_byte,
              "Illegal byte labels remain atomic between combining graphemes");
        std::string maximum_source(swiftedit::maximum_page, 'a');
        for (std::size_t index = 1; index < maximum_source.size(); index += 2)
            maximum_source[index] = static_cast<char>(0xff);
        const swiftedit::DisplayPage maximum(maximum_source);
        for (const swiftedit::DisplayUnit &unit : maximum.units()) {
            const swiftedit::SourceRange mapped = maximum.source_range(unit.display);
            check(mapped.offset == unit.source.offset && mapped.length == unit.source.length,
                  "Maximum-page expanded-label forward mapping");
            const std::size_t inverse = maximum.display_offset(unit.source.offset);
            check(inverse == unit.display.offset, "Maximum-page inverse mapping");
        }
        const std::size_t maximum_end = maximum.display_offset(maximum_source.size());
        const swiftedit::SourceRange maximum_eof = maximum.source_range({maximum_end, 0});
        check(maximum_eof.offset == maximum_source.size() && maximum_eof.length == 0,
              "Expanded maximum-page EOF maps exactly");
        for (const swiftedit::DisplayUnit &unit : page.units()) {
            for (std::size_t interior = 1; interior < unit.source.length; ++interior) {
                bool refused = false;
                try {
                    static_cast<void>(page.display_offset(unit.source.offset + interior));
                } catch (const std::runtime_error &) {
                    refused = true;
                }
                check(refused, "Inverse mapping rejects scalar and CRLF interiors");
            }
        }

        swiftedit::Session session{};
        session.replace_ranges({{0, 0}}, "abc abc", session.revision());
        const swiftedit::DocumentRevision initial = session.revision();
        session.replace_ranges({{0, 3}, {4, 3}}, "X", initial);
        check(session.text() == "X X", "Parallel replacement uses original source ranges");
        const bool undone = session.undo();
        check(undone && session.text() == "abc abc", "Parallel replacement is one undo");
        bool stale_refused = false;
        try {
            session.replace_ranges({{0, 1}}, "Y", initial);
        } catch (const std::runtime_error &) {
            stale_refused = true;
        }
        check(stale_refused && session.text() == "abc abc", "Stale range edit refused atomically");
        bool overlap_refused = false;
        try {
            session.replace_ranges({{0, 3}, {2, 3}}, "Y", session.revision());
        } catch (const std::runtime_error &) {
            overlap_refused = true;
        }
        check(overlap_refused && session.text() == "abc abc",
              "Overlapping ranges refused atomically");
        session.replace_ranges({{0, 3}, {4, 3}}, "", session.revision());
        check(session.text() == " ", "Discontiguous deletion preserves intervening source");
        const gui_forms::TextStore searchable("a?c a*c a\xc3\xa9"
                                              "c ae\xcc\x81"
                                              "c aZZc");
        swiftedit::SearchPattern pattern("a?c");
        const swiftedit::SearchProgress literal_hit =
            swiftedit::search_slice(searchable, pattern, 0, 1, true);
        check(literal_hit.match && (*literal_hit.match).length == 3,
              "Question mark is literal until explicitly flagged");
        pattern.toggle(1);
        const swiftedit::SearchProgress unicode_hit =
            swiftedit::search_slice(searchable, pattern, 8, 1, true);
        check(unicode_hit.match && (*unicode_hit.match).length == 4,
              "Wildcard consumes one Unicode grapheme");
        const swiftedit::SearchProgress combining_hit =
            swiftedit::search_slice(searchable, pattern, 12, 1, true);
        check(combining_hit.match && (*combining_hit.match).length == 5,
              "Combining sequence is one wildcard character");
        const swiftedit::SearchProgress slice =
            swiftedit::search_slice(searchable, pattern, 16, 1, true);
        check(!slice.match && slice.next_grapheme == 17 && !slice.complete,
              "Search slice yields without unbounded scanning");
        const swiftedit::PatternReplacement replaced =
            swiftedit::replace_pattern(searchable, pattern, "X", true, 100);
        check(replaced.text == "X X X X aZZc" && replaced.count == 4,
              "Wildcard replacement uses matched grapheme lengths, not query byte length");
        bool growth_refused = false;
        try {
            static_cast<void>(swiftedit::replace_pattern(searchable, pattern, "long", true, 5));
        } catch (const std::runtime_error &) {
            growth_refused = true;
        }
        check(growth_refused, "Replacement refuses excess output growth");
        std::string repeated(1024, 'a');
        repeated += 'b';
        std::string long_query(127, 'a');
        long_query += 'b';
        swiftedit::PatternScan scan(repeated, swiftedit::SearchPattern(long_query), 0, true);
        swiftedit::SearchProgress progress = scan.step(1);
        check(!progress.match && !progress.complete && progress.next_grapheme == 0,
              "Work budget can yield inside a partly matched candidate");
        std::size_t turns = 0;
        while (!progress.match && !progress.complete && turns < 1000) {
            progress = scan.step(256);
            ++turns;
        }
        check(progress.match && (*progress.match).offset == 897 && turns > 100,
              "Resumed adversarial search makes bounded progress without losing a candidate");
        scan.restart();
        const swiftedit::SearchProgress restarted = scan.step(1);
        check(!restarted.match && restarted.next_grapheme == 0,
              "Restart clears partial comparison state");
        swiftedit::PatternScan unicode_scan("e\xcc\x81", swiftedit::SearchPattern("e\xcc\x81"), 0,
                                            true);
        check(!unicode_scan.step(1).match && !unicode_scan.step(1).match,
              "Literal comparisons also yield inside a multibyte grapheme");
        const swiftedit::SearchProgress unicode_finished = unicode_scan.step(1);
        check(unicode_finished.match && (*unicode_finished.match).length == 3,
              "Resumed literal byte comparison preserves the complete grapheme");
        swiftedit::ReplacementScan incremental(searchable.utf8(), pattern, "X", true, 100);
        bool replacement_complete = incremental.step(1);
        check(!replacement_complete, "Replacement yields before publishing a partial result");
        bool premature_refused = false;
        try {
            static_cast<void>(incremental.take_result());
        } catch (const std::runtime_error &) {
            premature_refused = true;
        }
        check(premature_refused, "Partial replacement result cannot be consumed");
        for (std::size_t index = 0; !replacement_complete && index < 200; ++index)
            replacement_complete = incremental.step(1);
        check(replacement_complete, "One-operation replacement slices eventually complete");
        const swiftedit::PatternReplacement incremental_result = incremental.take_result();
        check(incremental_result.text == replaced.text &&
                  incremental_result.count == replaced.count,
              "Incremental wildcard replacement matches exact synchronous result");
        swiftedit::ReplacementScan nonoverlapping("aaaaa", swiftedit::SearchPattern("aa"), "", true,
                                                  5);
        while (!nonoverlapping.step(1)) {
        }
        const swiftedit::PatternReplacement deletion = nonoverlapping.take_result();
        check(deletion.text == "a" && deletion.count == 2,
              "Incremental deletion skips overlapping matches and preserves tail");
        std::cout << "Display mapping and atomic source-range tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
