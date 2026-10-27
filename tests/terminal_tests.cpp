#include "terminal_buffer.hpp"
#include "terminal_row.hpp"
#include "terminal_wrap.hpp"
#include "terminal_page.hpp"
#include "terminal_search.hpp"
#include "terminal_query.hpp"
#include "date_time.hpp"
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
void check_paged_word_wrap(const std::string &source, const std::size_t width) {
    swiftedit::TerminalBuffer buffer{};
    buffer.insert(source);
    const swiftedit::Session &session = buffer.session();
    std::vector<std::uint64_t> starts{};
    std::size_t offset = 0;
    swiftedit::TerminalPageCursor cursor{};
    while (offset < source.size()) {
        starts.push_back(offset);
        const swiftedit::TerminalWrapSpan expected = swiftedit::terminal_wrap_span(buffer, 0, offset, width);
        const swiftedit::TerminalPageFrame frame = swiftedit::terminal_page(session, cursor, width, 1);
        const std::size_t next = expected.source.offset + expected.source.length;
        check(frame.next.offset == next && !frame.next.label_cell && next > offset,
              "Paged space wrapping must agree with editable logical-row wrapping");
        for (const swiftedit::TerminalPageRun &run : frame.runs)
            check(run.row == 0 && run.source_offset >= offset &&
                      run.source_offset + run.source_length <= next,
                  "A rewound word cannot leave stale painted runs on the previous row");
        offset = next;
        cursor = frame.next;
    }
    for (const std::size_t budget : {1U, 2U, 3U, 7U, 33U, 8192U}) {
        for (const std::size_t rows : {1U, 2U, 3U}) {
            swiftedit::TerminalPageEnd end(session, width, rows, budget);
            std::size_t steps = 0;
            while (!end.step(session)) {
                ++steps;
                check(steps < source.size() * 4 + 100, "Wrapped End reconstruction must make progress");
            }
            const std::size_t first = starts.size() > rows ? starts.size() - rows : 0;
            check(end.result(session).offset == starts[first],
                  "End reconstruction must preserve word boundaries across source chunks");
        }
    }
    for (std::size_t row = 1; row < starts.size(); ++row) {
        swiftedit::TerminalPageEnd previous(session, width, 1,
            swiftedit::TerminalPageCursor{starts[row], 0, false});
        while (!previous.step(session)) {}
        check(previous.result(session).offset == starts[row - 1],
              "Backward reconstruction must find the preceding word-wrapped row");
    }
    check(session.text() == source, "Word wrapping must preserve original whitespace and source bytes");
}
int main() {
    try {
        check_paged_word_wrap("one two three four five", 9);
        check_paged_word_wrap("ab cd ef gh", 6);
        check_paged_word_wrap("    abc   def  end ", 7);
        check_paged_word_wrap("word\tword\tlast", 8);
        check_paged_word_wrap("abcdefghijklmnop longwordwithnospaces z", 8);
        check_paged_word_wrap("a \xe6\xbc\xa2\xe6\xbc\xa2 " "e\xcc\x81 next end", 7);
        check_paged_word_wrap(std::string(8189, 'x') + " " + std::string(900, 'y') + " z", 1000);
        swiftedit::Session wrapped_control{};
        wrapped_control.replace_ranges({{0, 0}}, "ab \x1b cd", wrapped_control.stamp());
        const swiftedit::TerminalPageFrame wrapped_control_frame =
            swiftedit::terminal_page(wrapped_control, {}, 6, 3);
        std::vector<std::string> control_rows(3);
        for (const swiftedit::TerminalPageRun &run : wrapped_control_frame.runs)
            control_rows[run.row] += run.text;
        check(control_rows[0] == "ab " && control_rows[1] == "[U+001" && control_rows[2] == "B] cd",
              "Word wrapping preserves complete inert control labels across narrower rows");
        swiftedit::TerminalPageEnd control_end(wrapped_control, 6, 1, 1);
        while (!control_end.step(wrapped_control)) {}
        check(control_end.result(wrapped_control).offset == 3 &&
                  control_end.result(wrapped_control).label_cell == 6,
              "End reconstruction retains the control-label continuation after a word break");
        swiftedit::TerminalBuffer terminal{};
        std::tm calendar{};
        calendar.tm_year = 126;
        calendar.tm_mon = 9;
        calendar.tm_mday = 1;
        calendar.tm_hour = 12;
        calendar.tm_min = 34;
        calendar.tm_sec = 56;
        calendar.tm_isdst = -1;
        const std::time_t instant = std::mktime(&calendar);
        check(instant != static_cast<std::time_t>(-1), "Local timestamp fixture is representable");
        const std::string timestamp = notepad::local_date_time(instant);
        check(timestamp == "2026-10-01 12:34:56",
              "Shared local date/time uses plain stable formatting");
        terminal.insert("replace me");
        terminal.select_all();
        terminal.insert(timestamp);
        check(terminal.session().text() == timestamp, "Timestamp insertion replaces the selection");
        check(terminal.undo() && terminal.session().text() == "replace me",
              "Timestamp insertion is one undoable edit");
        terminal.reset(true);
        terminal.move(swiftedit::TerminalMotion::left);
        terminal.move(swiftedit::TerminalMotion::right);
        terminal.erase(true);
        check(terminal.selection().caret == 0 && terminal.session().text().empty(),
              "Empty boundary navigation is inert");
        terminal.insert("e\xcc\x81\r\n\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0"
                        "\x9f\x91\xa7");
        terminal.move(swiftedit::TerminalMotion::left);
        check(terminal.selection().caret == 5, "Family emoji is one movement");
        terminal.move(swiftedit::TerminalMotion::left);
        check(terminal.selection().caret == 3, "CRLF is one movement");
        terminal.erase(true);
        check(terminal.session().text().starts_with("\r\n"),
              "Backspace removes complete combining grapheme");
        check(terminal.undo(), "Terminal edit uses Session undo");
        terminal.move(swiftedit::TerminalMotion::document_start);
        terminal.move(swiftedit::TerminalMotion::right, true);
        check(terminal.selected_text() == "e\xcc\x81", "Selection preserves complete grapheme");
        terminal.insert("Q");
        check(terminal.session().text().starts_with("Q\r\n"),
              "Selection replacement is one source edit");
        check(terminal.undo() && terminal.session().text().starts_with("e\xcc\x81"),
              "Replacement undo restores source");
        terminal.reset(true);
        terminal.insert("abcd\nx\nabcdef");
        terminal.move(swiftedit::TerminalMotion::document_start);
        terminal.move(swiftedit::TerminalMotion::end);
        terminal.move(swiftedit::TerminalMotion::down);
        check(terminal.selection().caret == 6, "Vertical movement clamps to short line");
        terminal.move(swiftedit::TerminalMotion::down);
        check(terminal.selection().caret == 11,
              "Vertical movement preserves desired grapheme column");
        terminal.move(swiftedit::TerminalMotion::up, true, 2);
        check(terminal.selected_text() == "\nx\nabcd",
              "Page-style selection traverses logical rows");
        terminal.move(swiftedit::TerminalMotion::left);
        check(terminal.selection().caret == 4 && terminal.selected_range().length == 0,
              "Left collapses selection to start");
        const swiftedit::SourceClipboard cut = terminal.cut();
        check(cut.bytes() == "abcd\n" && terminal.session().text() == "x\nabcdef",
              "Cut without selection removes one full logical line");
        terminal.paste(cut);
        check(terminal.session().text() == "abcd\nx\nabcdef", "Internal cut text pastes exactly");
        terminal.reset(true);
        const std::string malformed("\xff\xcc\x81Z", 4);
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("swiftedit-terminal-" + std::to_string(test_process_id()));
        check(std::filesystem::create_directory(dir), "Unique terminal fixture");
        struct Cleanup {
            std::filesystem::path path{};
            ~Cleanup() {
                std::error_code error{};
                std::filesystem::remove_all(path, error);
            }
        } cleanup{dir};
        const std::filesystem::path path = dir / "malformed.txt";
        {
            std::ofstream file(path, std::ios::binary);
            file.write(malformed.data(), static_cast<std::streamsize>(malformed.size()));
            check(static_cast<bool>(file), "Malformed fixture written");
        }
        terminal.open(path);
        terminal.move(swiftedit::TerminalMotion::document_start);
        terminal.erase(false);
        check(
            terminal.session().text() == std::string("\xcc\x81Z", 3),
            "Illegal byte is independently deletable without swallowing following combining text");
        check(terminal.undo() && terminal.session().text() == malformed,
              "Illegal bytes survive undo");
        terminal.select_all();
        check(terminal.selected_text() == malformed, "Copy returns exact source bytes");
        const swiftedit::SourceClipboard raw_copy = terminal.copy();
        const swiftedit::SourceClipboard raw_cut = terminal.cut();
        check(terminal.session().text().empty(), "Cut captures malformed source before deletion");
        terminal.paste(raw_cut);
        check(terminal.session().text() == malformed, "Malformed cut/paste preserves exact bytes");
        const bool paste_undone = terminal.undo();
        check(paste_undone && terminal.session().text().empty(), "Paste is one undo step");
        terminal.paste(raw_copy);
        bool external_refused = false;
        try {
            terminal.insert(malformed);
        } catch (const std::exception &) {
            external_refused = true;
        }
        check(external_refused && terminal.session().text() == malformed,
              "External malformed payload remains refused");
        terminal.move(swiftedit::TerminalMotion::document_end);
        terminal.insert("changed");
        const std::string dirty = terminal.session().text();
        const swiftedit::DocumentStamp before_copy = terminal.session().stamp();
        const swiftedit::TerminalSelection copy_selection = terminal.selection();
        const std::filesystem::path text_copy = dir / "malformed.1.txt";
        terminal.save_text_copy(text_copy);
        check(notepad::read_file(text_copy).bytes == std::string(" \xcc\x81Zchanged"),
              "Text Copy replaces each malformed byte while preserving Unicode");
        check(terminal.session().text() == dirty && terminal.session().dirty() &&
                  terminal.session().path() == path &&
                  terminal.session().stamp().identity == before_copy.identity &&
                  terminal.session().stamp().revision == before_copy.revision &&
                  terminal.selection().anchor == copy_selection.anchor &&
                  terminal.selection().caret == copy_selection.caret,
              "Text Copy does not alter the open document, dirty state or selection");
        bool copy_overwrite_refused = false;
        try {
            terminal.save_text_copy(text_copy);
        } catch (const std::exception &) {
            copy_overwrite_refused = true;
        }
        check(copy_overwrite_refused && notepad::read_file(path).bytes == malformed,
              "Text Copy refuses an existing destination and leaves the source file intact");
        check(terminal.undo() && terminal.session().text() == malformed,
              "Text Copy does not establish a new undo boundary");
        check(terminal.redo() && terminal.session().text() == dirty,
              "Text Copy preserves redo history");
        bool discard_refused = false;
        try {
            terminal.reset();
        } catch (const std::exception &) {
            discard_refused = true;
        }
        check(discard_refused && terminal.session().text() == dirty,
              "New document requires explicit dirty discard");
        terminal.reset(true);
        terminal.insert("\xf0\x9f\x98\x80");
        terminal.select_all();
        terminal.insert("ab");
        check(terminal.undo() && terminal.selection().caret == 0,
              "Undo clamps a caret inside a restored multibyte grapheme to its start");
        terminal.move(swiftedit::TerminalMotion::document_end);
        terminal.enter();
        check(terminal.session().text().ends_with(notepad::native_newline()),
              "New terminal documents use native Enter endings");
        const std::filesystem::path saved = dir / "saved.txt";
        terminal.save_as(saved);
        check(!terminal.session().dirty() && !terminal.undo(),
              "Terminal save establishes shared undo boundary");
        check(swiftedit::terminal_glyph("\xe4\xb8\xad", 0).cells == 2,
              "CJK uses two terminal cells");
        check(swiftedit::terminal_glyph("e\xcc\x81", 0).cells == 1,
              "Combining mark does not add a cell");
        check(swiftedit::terminal_glyph("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9", 0).cells ==
                  2,
              "Joined emoji occupies two cells");
        check(swiftedit::terminal_glyph("\t", 3).text == " ", "Tabs use four-cell stops");
        const swiftedit::TerminalGlyph escape = swiftedit::terminal_glyph("\x1b", 0);
        check(escape.label && escape.text == "[U+001B]", "Escape control is visible inert text");
        check(swiftedit::terminal_glyph("\xe2\x80\xae", 0).text == "[U+202E]",
              "Bidi override is visibly labeled");
        check(swiftedit::terminal_glyph(std::string("\xff", 1), 0).text == "[BYTE FF]",
              "Malformed byte is visibly labeled");
        terminal.reset();
        terminal.insert("A\xe4\xb8\xad\tZ");
        terminal.move(swiftedit::TerminalMotion::document_start);
        terminal.move(swiftedit::TerminalMotion::right);
        terminal.move(swiftedit::TerminalMotion::right, true);
        const swiftedit::TerminalRow row = swiftedit::terminal_row(terminal, 0, 0, 8);
        check(row.total_cells == 5 && row.caret_column == 3 && row.runs.size() == 4 &&
                  row.runs[1].selected && row.runs[1].cells == 2 && row.runs[2].cells == 1,
              "Source selection and caret map to terminal cell columns");
        const swiftedit::TerminalRow clipped = swiftedit::terminal_row(terminal, 0, 2, 2);
        check(clipped.runs[0].text == " " && clipped.runs[0].source.length == 3 &&
                  clipped.clipped_left && clipped.clipped_right && clipped.caret_column == 1,
              "Partial wide glyph never emits a broken character or changes its source range");
        swiftedit::Session paged_source{};
        paged_source.replace_ranges({{0, 0}}, "ABCD\r\nEF\nG", paged_source.stamp());
        swiftedit::TerminalPager pager{};
        pager.reset(paged_source);
        const swiftedit::TerminalPageFrame first_page = pager.frame(paged_source, 4, 1);
        check(first_page.next.offset == 4 && first_page.next.after_wrap,
              "Page carries full-row wrap boundary");
        check(first_page.runs[0].source_offset == 0 && first_page.runs[0].source_length == 1 &&
                  first_page.runs[3].source_offset == 3 && pager.source_offset() == 0,
              "Read-only runs expose exact source ranges for selection highlighting");
        pager.next();
        const swiftedit::TerminalPageFrame second_page = pager.frame(paged_source, 4, 1);
        check(second_page.runs.size() == 2 && second_page.runs[0].text == "E" &&
                  second_page.next.offset == 9,
              "Newline after full row does not create an extra empty screen row");
        check(second_page.runs[0].source_offset == 6 && second_page.runs[0].source_length == 1,
              "Selection source mapping includes skipped CRLF bytes without displaying them");
        pager.previous();
        check(pager.frame(paged_source, 4, 1).runs[0].text == "A",
              "Previous restores exact page cursor");
        pager.first();
        const swiftedit::TerminalPageFrame row_frame = pager.frame(paged_source, 4, 3);
        check(row_frame.row_starts.size() == 3, "Pager records visual row source starts");
        pager.down();
        check(pager.frame(paged_source, 4, 3).runs[0].text == "E",
              "Down scrolls one row instead of a page, skipping no newline after exact wrap");
        pager.down();
        check(pager.frame(paged_source, 4, 3).runs[0].text == "G",
              "Down can reveal the last row even when the frame already reaches EOF");
        pager.down();
        check(pager.frame(paged_source, 4, 3).runs[0].text == "G",
              "Down at the last row does not scroll into empty space");
        pager.up();
        check(pager.frame(paged_source, 4, 3).runs[0].text == "E",
              "Up restores exactly one prior visual row");
        pager.first();
        static_cast<void>(pager.frame(paged_source, 4, 2));
        pager.next();
        check(pager.frame(paged_source, 4, 2).runs[0].text == "G",
              "Page Down continues to advance by a complete frame");
        pager.up();
        check(pager.frame(paged_source, 4, 2).runs[0].text == "E",
              "Up after Page Down returns one row, not the entire previous page");
        pager.previous();
        check(pager.frame(paged_source, 4, 2).runs[0].text == "A",
              "Page Up after row navigation restores the earlier page start");
        paged_source.replace_ranges({{0, paged_source.text().size()}}, "\x1bZ",
                                    paged_source.stamp());
        bool old_page_refused = false;
        try {
            static_cast<void>(pager.frame(paged_source, 4, 1));
        } catch (const std::exception &) {
            old_page_refused = true;
        }
        check(old_page_refused, "Pager cache rejects changed source");
        pager.reset(paged_source);
        const swiftedit::TerminalPageFrame label_first = pager.frame(paged_source, 4, 1);
        check(label_first.runs[0].text == "[U+0" && label_first.next.offset == 0 &&
                  label_first.next.label_cell == 4,
              "Long inert label can continue without consuming its source byte early");
        pager.next();
        check(pager.frame(paged_source, 4, 1).runs[0].text == "01B]",
              "Label continuation is exact");
        check(pager.frame(paged_source, 4, 1).runs[0].source_offset == 0 &&
                  pager.frame(paged_source, 4, 1).runs[0].source_length == 1,
              "Every label fragment maps to the same atomic source control byte");
        pager.next();
        check(pager.frame(paged_source, 4, 1).runs[0].text == "Z",
              "Following content is not skipped");
        pager.first();
        static_cast<void>(pager.frame(paged_source, 4, 3));
        pager.down();
        check(pager.frame(paged_source, 4, 3).runs[0].text == "01B]",
              "Row scrolling retains the exact inert label continuation");
        pager.up();
        check(pager.frame(paged_source, 4, 3).runs[0].text == "[U+0",
              "Reverse row scrolling restores the first label fragment");
        swiftedit::TerminalPageEnd label_end(paged_source, 4, 2);
        check(label_end.step(paged_source), "Small end scan completes in one bounded step");
        pager.finish_end(paged_source, label_end);
        const swiftedit::TerminalPageFrame end_labels = pager.frame(paged_source, 4, 2);
        check(!end_labels.more && end_labels.runs.front().text == "01B]" &&
                  end_labels.runs.back().text == "Z",
              "Final viewport retains label continuation and last source character");
        pager.up();
        check(pager.frame(paged_source, 4, 2).runs.front().text == "[U+0",
              "End navigation retains preceding rows for Up");
        paged_source.replace_ranges({{0, 0}}, "changed", paged_source.stamp());
        bool stale_end_refused = false;
        try {
            pager.finish_end(paged_source, label_end);
        } catch (const std::exception &) {
            stale_end_refused = true;
        }
        check(stale_end_refused, "Completed end scan cannot publish against changed source");
        swiftedit::Session streamed{};
        const std::string long_line(swiftedit::maximum_page * 2 + 37, 'x');
        streamed.replace_ranges({{0, 0}}, long_line, streamed.stamp());
        const std::size_t expected_end = ((long_line.size() - 1) / 80 - 23) * 80;
        for (const std::size_t budget : {std::size_t(8192), swiftedit::maximum_page}) {
            swiftedit::TerminalPageEnd scan(streamed, 80, 24, budget);
            while (!scan.step(streamed)) {}
            check(scan.result(streamed).offset == expected_end,
                  "Read chunks must not introduce artificial wrapped rows");
        }
        std::string combined = "e";
        for (std::size_t index = 0; index < 6000; ++index)
            combined += "\xcc\x81";
        combined += "\r\nABCD\r\n\x1bZ";
        streamed.replace_ranges({{0, streamed.text().size()}}, combined, streamed.stamp());
        const swiftedit::TerminalPageFrame combined_page = swiftedit::terminal_page(streamed, {}, 4, 3);
        check(!combined_page.graphemes.empty() && combined_page.graphemes.front().length == 12001,
              "Visible page grows its context to preserve a grapheme larger than 8 KiB");
        check(combined_page.row_starts.size() >= 3,
              "Context growth fills later rows after the long first grapheme");
        swiftedit::TerminalPageEnd reference_end(streamed, 4, 3, swiftedit::maximum_page);
        while (!reference_end.step(streamed)) {}
        const swiftedit::TerminalPageCursor expected_cursor = reference_end.result(streamed);
        for (const std::size_t budget : {std::size_t(1), std::size_t(31), std::size_t(8192)}) {
            swiftedit::TerminalPageEnd scan(streamed, 4, 3, budget);
            while (!scan.step(streamed)) {}
            check(scan.result(streamed) == expected_cursor,
                  "Adaptive context preserves long graphemes, CRLF and control-label geometry");
        }
        swiftedit::Session rewind_source{};
        rewind_source.replace_ranges({{0, 0}}, std::string(100000, 'x'), rewind_source.stamp());
        const swiftedit::DocumentStamp rewind_stamp = rewind_source.stamp();
        swiftedit::TerminalPager rewind_pager{};
        rewind_pager.reset(rewind_source);
        swiftedit::TerminalPageEnd rewind_end(rewind_source, 1, 3);
        while (!rewind_end.step(rewind_source)) {}
        rewind_pager.finish_end(rewind_source, rewind_end);
        for (std::size_t row = 0; row < 32768; ++row)
            rewind_pager.up();
        static_cast<void>(rewind_pager.frame(rewind_source, 1, 3));
        const std::uint64_t before_rewind = rewind_pager.source_offset();
        check(before_rewind > 3, "Rewind fixture exhausts bounded history away from source start");
        std::unique_ptr<swiftedit::TerminalPageEnd> rewind =
            rewind_pager.prepare_previous(rewind_source, true);
        check(bool(rewind), "Page Up reconstructs rows after retained history expires");
        check(!(*rewind).step(rewind_source) && rewind_pager.source_offset() == before_rewind,
              "Pending reconstruction preserves the visible page");
        rewind.reset();
        check(rewind_pager.source_offset() == before_rewind,
              "Cancelling reconstruction does not publish intermediate rows");
        rewind = rewind_pager.prepare_previous(rewind_source, true);
        while (!(*rewind).step(rewind_source)) {}
        rewind_pager.finish_end(rewind_source, *rewind);
        check(rewind_pager.source_offset() == before_rewind - 3 &&
                  rewind_source.stamp().revision == rewind_stamp.revision &&
                  rewind_source.text() == std::string(100000, 'x'),
              "Reconstruction returns exactly one earlier viewport without changing source");
        static_cast<void>(rewind_pager.frame(rewind_source, 1, 3));
        check(!rewind_pager.prepare_previous(rewind_source, false),
              "Reconstruction replenishes bounded preceding history for immediate Up");
        swiftedit::TerminalPageEnd one_row(rewind_source, 1, 1,
                                          swiftedit::TerminalPageCursor{before_rewind, 0, true});
        while (!one_row.step(rewind_source)) {}
        check(one_row.result(rewind_source).offset == before_rewind - 1,
              "Row reconstruction returns the immediately preceding visual row");
        swiftedit::Session label_rewind{};
        label_rewind.replace_ranges({{0, 0}}, "\x1bZ", label_rewind.stamp());
        swiftedit::TerminalPageEnd partial_label(label_rewind, 4, 1,
                                                swiftedit::TerminalPageCursor{0, 4, true});
        while (!partial_label.step(label_rewind)) {}
        check(partial_label.result(label_rewind) == swiftedit::TerminalPageCursor{},
              "Reconstruction distinguishes label fragments sharing a source byte");
        swiftedit::Session resized_source{};
        resized_source.replace_ranges({{0, 0}}, std::string(200, 'x'), resized_source.stamp());
        swiftedit::TerminalPager resized_pager{};
        resized_pager.reset(resized_source);
        static_cast<void>(resized_pager.frame(resized_source, 10, 2));
        resized_pager.next();
        static_cast<void>(resized_pager.frame(resized_source, 10, 2));
        resized_pager.next();
        static_cast<void>(resized_pager.frame(resized_source, 10, 3));
        resized_pager.previous();
        check(resized_pager.source_offset() == 10,
              "Height change uses the new page size instead of obsolete page jumps");
        static_cast<void>(resized_pager.frame(resized_source, 7, 3));
        check(resized_pager.source_offset() == 10,
              "Width change preserves the current source anchor");
        std::unique_ptr<swiftedit::TerminalPageEnd> resized_previous =
            resized_pager.prepare_previous(resized_source, false);
        check(bool(resized_previous), "Width change discards stale wrapped-row history");
        while (!(*resized_previous).step(resized_source)) {}
        resized_pager.finish_end(resized_source, *resized_previous);
        check(resized_pager.source_offset() == 7,
              "Previous row is reconstructed using the new wrap width");
        static_cast<void>(resized_pager.frame(resized_source, 7, 3));
        check(!resized_pager.prepare_previous(resized_source, false),
              "Publishing matching-width reconstruction retains its earlier rows");
        resized_pager.up();
        check(resized_pager.source_offset() == 0, "Rebuilt history reaches source start");
        swiftedit::Session horizontal{};
        horizontal.replace_ranges({{0, 0}}, "e\xcc\x81\r\n\x1bZ", horizontal.stamp());
        const swiftedit::TerminalPageFrame horizontal_frame = swiftedit::terminal_page(horizontal, {}, 4, 3);
        check(swiftedit::terminal_page_horizontal(horizontal_frame, 0, true) == 3 &&
                  swiftedit::terminal_page_horizontal(horizontal_frame, 3, false) == 0,
              "Horizontal movement preserves a combining grapheme");
        check(swiftedit::terminal_page_horizontal(horizontal_frame, 3, true) == 5 &&
                  swiftedit::terminal_page_horizontal(horizontal_frame, 5, false) == 3,
              "Horizontal movement treats CRLF as one source grapheme");
        check(swiftedit::terminal_page_horizontal(horizontal_frame, 5, true) == 6,
              "Horizontal movement crosses a displayed control label atomically");
        const std::optional<swiftedit::TerminalPageCaret> control_caret =
            swiftedit::terminal_page_caret(horizontal_frame, 5, 4, 3);
        check(control_caret && (*control_caret).row == 1 && (*control_caret).column == 0,
              "Caret maps the control's source start to the first visible label cell");
        check(!swiftedit::terminal_page_caret(horizontal_frame, 6, 4, 3),
              "Caret after a wrapped label outside the viewport needs revealing");
        const swiftedit::TerminalPageFrame partial_control =
            swiftedit::terminal_page(horizontal, {5, 4, true}, 4, 2);
        check(swiftedit::terminal_page_horizontal(partial_control, 6, false) == 5 &&
                  !swiftedit::terminal_page_caret(partial_control, 5, 4, 2),
              "Partial label rows retain atomic movement without a false source-start caret");
        swiftedit::Session newline_caret{};
        newline_caret.replace_ranges({{0, 0}}, "ABCD\r\n", newline_caret.stamp());
        const swiftedit::TerminalPageFrame newline_frame = swiftedit::terminal_page(newline_caret, {}, 4, 3);
        const std::optional<swiftedit::TerminalPageCaret> eof_caret =
            swiftedit::terminal_page_caret(newline_frame, 6, 4, 3);
        check(eof_caret && (*eof_caret).row == 1 && (*eof_caret).column == 0,
              "Caret at EOF after wrap plus CRLF occupies the next empty row");
        swiftedit::Session wide_caret{};
        wide_caret.replace_ranges({{0, 0}}, "ABC\xe7\x95\x8c", wide_caret.stamp());
        const swiftedit::TerminalPageFrame wide_clipped = swiftedit::terminal_page(wide_caret, {}, 4, 1);
        check(!swiftedit::terminal_page_caret(wide_clipped, 3, 4, 1),
              "Caret before a wide grapheme wrapped below the viewport is not on the preceding row");
        const swiftedit::TerminalPageFrame wide_visible = swiftedit::terminal_page(wide_caret, {}, 4, 2);
        const std::optional<swiftedit::TerminalPageCaret> wide_start =
            swiftedit::terminal_page_caret(wide_visible, 3, 4, 2);
        check(wide_start && (*wide_start).row == 1 && (*wide_start).column == 0,
              "Caret before a wrapped wide grapheme follows its actual row");
        swiftedit::Session blank_caret{};
        blank_caret.replace_ranges({{0, 0}}, "ABCD\n\nZ", blank_caret.stamp());
        const swiftedit::TerminalPageFrame blank_frame = swiftedit::terminal_page(blank_caret, {}, 4, 4);
        const std::optional<swiftedit::TerminalPageCaret> blank_start =
            swiftedit::terminal_page_caret(blank_frame, 5, 4, 4);
        check(blank_start && (*blank_start).row == 1 && (*blank_start).column == 0,
              "Blank-line caret survives a preceding wrap plus newline");
        check(swiftedit::terminal_page_target(wide_visible, 1, 1, 4, 2) == 3,
              "Vertical target inside a wide glyph stays at its source boundary");
        check(swiftedit::terminal_page_target(wide_visible, 0, 3, 4, 2) == 2,
              "Wrapped wide-glyph boundary is not owned by the preceding row");
        check(swiftedit::terminal_page_target(blank_frame, 1, 3, 4, 4) == 5,
              "Vertical target on a blank row clamps to its exact source anchor");
        check(swiftedit::terminal_page_target(newline_frame, 1, 3, 4, 3) == 6,
              "Vertical target reaches empty EOF after wrapped CRLF");
        check(!swiftedit::terminal_page_target(wide_visible, 2, 0, 4, 2),
              "Vertical target refuses a row outside the viewport");
        // Compare every legal caret in all six-byte CR/LF/text arrangements
        // with the editable text model's established logical-line commands.
        for (std::size_t code = 0; code < 729; ++code) {
            std::size_t digits = code;
            std::string arrangement{};
            const char alphabet[] = {'x', '\r', '\n'};
            for (std::size_t index = 0; index < 6; ++index) {
                arrangement += alphabet[digits % 3];
                digits /= 3;
            }
            const std::filesystem::path arrangement_path = dir / "line-arrangement.txt";
            {
                std::ofstream output(arrangement_path, std::ios::binary);
                output << arrangement;
            }
            swiftedit::TerminalBuffer reference{};
            reference.open(arrangement_path);
            for (std::size_t caret = 0; caret <= arrangement.size(); ++caret) {
                if (caret > 0 && caret < arrangement.size() &&
                    arrangement[caret - 1] == '\r' && arrangement[caret] == '\n')
                    continue;
                reference.move_to(caret, reference.session().stamp(), false);
                reference.move(swiftedit::TerminalMotion::home, false);
                swiftedit::TerminalLineBoundary home(reference.session(), caret, false);
                check(home.step(reference.session()) &&
                      home.result(reference.session()) == reference.selection().caret,
                      "Paged Home agrees with editable logical-line boundaries");
                reference.move_to(caret, reference.session().stamp(), false);
                reference.move(swiftedit::TerminalMotion::end, false);
                swiftedit::TerminalLineBoundary end(reference.session(), caret, true);
                check(end.step(reference.session()) &&
                      end.result(reference.session()) == reference.selection().caret,
                      "Paged End agrees with editable logical-line boundaries");
            }
        }
        swiftedit::Session lines{};
        const std::string long_lines = "a\r\n" + std::string(20000, 'x') + "\rZ\n";
        lines.replace_ranges({{0, 0}}, long_lines, lines.stamp());
        swiftedit::TerminalLineBoundary line_end(lines, 3, true);
        check(!line_end.step(lines), "Long-line End yields after a bounded read");
        check(!line_end.step(lines), "Long-line End remains cooperative across chunks");
        check(line_end.step(lines) && line_end.result(lines) == 20003,
              "End stops before the source separator");
        swiftedit::TerminalLineBoundary line_home(lines, 20003, false);
        check(!line_home.step(lines), "Long-line Home yields after a bounded backward read");
        while (!line_home.step(lines)) {}
        check(line_home.result(lines) == 3, "Home returns after the complete CRLF");
        swiftedit::TerminalLineBoundary crlf_end(lines, 0, true);
        check(crlf_end.step(lines) && crlf_end.result(lines) == 1,
              "End excludes both bytes of CRLF");
        swiftedit::TerminalLineBoundary empty_home(lines, lines.size(), false);
        check(empty_home.step(lines) && empty_home.result(lines) == lines.size(),
              "Home preserves the empty trailing line");
        swiftedit::TerminalLineBoundary stale_line(lines, 3, true);
        lines.reset();
        bool stale_line_rejected = false;
        try { static_cast<void>(stale_line.step(lines)); }
        catch (const std::exception &) { stale_line_rejected = true; }
        check(stale_line_rejected, "Line navigation rejects a replaced source");
        const std::filesystem::path large_path = dir / "large.txt";
        {
            std::ofstream file(large_path, std::ios::binary);
            file << std::string(swiftedit::maximum_page - 2, 'a') << "e\xcc\x81";
            file.seekp(swiftedit::editable_limit - 1);
            file.put('z');
            check(static_cast<bool>(file), "Large terminal fixture written");
        }
        swiftedit::Session large{};
        large.open(large_path);
        check(large.read_only(), "Actual large terminal fixture uses bounded file handle");
        swiftedit::TerminalPager large_pager{};
        large_pager.reset(large);
        const swiftedit::TerminalPageFrame large_first = large_pager.frame(large, 1000, 300);
        check(large_first.next.offset == swiftedit::maximum_page - 2,
              "Incomplete trailing scalar and its preceding grapheme are deferred together");
        const swiftedit::DocumentStamp large_stamp = large.stamp();
        large_pager.down();
        check(large_pager.frame(large, 1000, 300).row_starts[0].offset == 1000,
              "Actual read-only file scrolls one visual row with bounded reads");
        large_pager.up();
        check(large_pager.frame(large, 1000, 300).row_starts[0].offset == 0 &&
                  large.stamp().identity == large_stamp.identity &&
                  large.stamp().revision == large_stamp.revision && !large.dirty(),
              "Read-only row navigation preserves document identity, revision and cleanliness");
        large_pager.next();
        const swiftedit::TerminalPageFrame &large_second = large_pager.frame(large, 1000, 300);
        check(large_second.runs[0].text == "e\xcc\x81",
              "Next bounded read reconstructs full combining grapheme");
        swiftedit::TerminalPageEnd large_end(large, 1000, 20);
        const std::uint64_t before_end = large_pager.source_offset();
        check(!large_end.step(large) && large_pager.source_offset() == before_end,
              "Pending end scan leaves the visible page unchanged");
        bool premature_end_refused = false;
        try {
            large_pager.finish_end(large, large_end);
        } catch (const std::exception &) {
            premature_end_refused = true;
        }
        check(premature_end_refused && large_pager.source_offset() == before_end,
              "Incomplete end scan cannot publish a partial destination");
        std::size_t end_steps = 1;
        while (!large_end.step(large)) {
            ++end_steps;
            check(end_steps < 10000, "Bounded end scan must make forward progress");
        }
        large_pager.finish_end(large, large_end);
        const swiftedit::TerminalPageFrame final_page = large_pager.frame(large, 1000, 20);
        check(!final_page.more && final_page.runs.back().text == "z" &&
                  final_page.next.offset == large.size() && !large.dirty(),
              "Actual large-file end scan reaches final byte without source mutation");
        large_pager.previous();
        check(large_pager.source_offset() < final_page.row_starts.front().offset,
              "Page Up works immediately after end navigation");
        const std::filesystem::path huge_cluster_path = dir / "huge-cluster.txt";
        {
            std::ofstream file(huge_cluster_path, std::ios::binary);
            file << 'e';
            for (std::size_t i = 0; i < 35000; ++i)
                file << "\xcc\x81";
            file.seekp(swiftedit::editable_limit - 1);
            file.put('z');
        }
        swiftedit::Session huge_cluster{};
        huge_cluster.open(huge_cluster_path);
        bool incomplete_refused = false;
        try {
            static_cast<void>(swiftedit::terminal_page(huge_cluster, {}, 80, 20));
        } catch (const std::exception &) {
            incomplete_refused = true;
        }
        check(incomplete_refused && huge_cluster.read_only(),
              "Oversized unknown grapheme is explicit unavailable, never sliced");
        terminal.reset(true);
        terminal.insert("one axc two abc");
        terminal.move(swiftedit::TerminalMotion::document_start);
        swiftedit::SearchPattern flagged("a?c");
        flagged.toggle(1);
        swiftedit::TerminalSearch search{};
        search.begin(terminal, flagged, true);
        std::size_t steps = 0;
        while (search.state() == swiftedit::TerminalSearchState::pending && steps < 1000) {
            static_cast<void>(search.step(terminal, 1));
            ++steps;
        }
        check(steps > 1 && search.state() == swiftedit::TerminalSearchState::found &&
                  terminal.selected_text() == "axc",
              "Incremental terminal search selects flagged grapheme match");
        search.begin(terminal, swiftedit::SearchPattern("one"));
        for (std::size_t i = 0;
             i < 100 && search.state() == swiftedit::TerminalSearchState::pending; ++i)
            static_cast<void>(search.step(terminal, 4));
        check(search.state() == swiftedit::TerminalSearchState::found &&
                  terminal.selected_text() == "one",
              "Terminal Find wraps at end of document");
        search.begin(terminal, swiftedit::SearchPattern("missing"));
        terminal.move(swiftedit::TerminalMotion::right);
        check(search.step(terminal) == swiftedit::TerminalSearchState::cancelled,
              "Moving selection revokes deferred search publication");
        search.begin(terminal, swiftedit::SearchPattern("abc"));
        terminal.insert("changed");
        check(search.step(terminal) == swiftedit::TerminalSearchState::cancelled,
              "Editing source revokes deferred search publication");
        search.begin(terminal, swiftedit::SearchPattern("missing"));
        search.cancel();
        check(search.step(terminal) == swiftedit::TerminalSearchState::cancelled,
              "Explicit cancellation cannot publish a later match");
        swiftedit::TerminalQuery query{};
        query.insert("a?c");
        query.move(false);
        query.move(false);
        query.toggle();
        check(query.text() == "a?c" && query.pattern().slots()[1].wildcard,
              "Terminal wildcard flag leaves punctuation source intact");
        query.home();
        query.insert("X");
        check(query.pattern().slots()[2].wildcard,
              "Unchanged suffix retains wildcard flag after query insertion");
        query.end();
        query.erase(true);
        check(query.text() == "Xa?" && query.pattern().slots()[2].wildcard,
              "Query grapheme deletion preserves other flags");
        query.toggle();
        check(!query.pattern().slots()[2].wildcard, "Toggle at query end addresses last slot");
        swiftedit::TerminalQuery repeated{};
        repeated.insert("aa");
        repeated.home();
        repeated.toggle();
        repeated.insert("a");
        check(!repeated.pattern().slots()[0].wildcard && repeated.pattern().slots()[1].wildcard &&
                  !repeated.pattern().slots()[2].wildcard,
              "Inserted duplicate must not steal original occurrence wildcard");
        repeated.erase(false);
        check(repeated.text() == "aa" && !repeated.pattern().slots()[0].wildcard &&
                  !repeated.pattern().slots()[1].wildcard,
              "Deleting flagged occurrence must not transfer wildcard to identical neighbor");
        repeated.end();
        repeated.toggle();
        repeated.home();
        repeated.erase(false);
        check(repeated.text() == "a" && repeated.pattern().slots()[0].wildcard,
              "Deleting unflagged duplicate preserves surviving occurrence flag");
        swiftedit::TerminalQuery combined_query{};
        combined_query.insert("a");
        combined_query.toggle();
        combined_query.insert("\xcc\x81");
        check(combined_query.pattern().slots().size() == 1 && !combined_query.pattern().slots()[0].wildcard,
              "Combining insertion changes grapheme and clears its old wildcard");
        terminal.reset(true);
        terminal.insert("cat cat cat");
        swiftedit::TerminalReplace replacement{};
        replacement.begin(terminal, swiftedit::SearchPattern("cat"), "dog", true);
        check(replacement.step(terminal, 1) == swiftedit::TerminalReplaceState::pending &&
                  terminal.session().text() == "cat cat cat",
              "Partial replacement remains private");
        replacement.cancel();
        check(terminal.session().text() == "cat cat cat", "Cancel preserves source");
        replacement.begin(terminal, swiftedit::SearchPattern("cat"), "dog", true);
        for (std::size_t i = 0;
             i < 100 && replacement.state() == swiftedit::TerminalReplaceState::pending; ++i)
            static_cast<void>(replacement.step(terminal, 2));
        check(replacement.state() == swiftedit::TerminalReplaceState::complete &&
                  replacement.count() == 3 && terminal.session().text() == "dog dog dog",
              "Prepared replacements publish together");
        check(terminal.undo() && terminal.session().text() == "cat cat cat",
              "One undo restores entire Replace All");
        replacement.begin(terminal, swiftedit::SearchPattern("cat"), "dog", true);
        terminal.move(swiftedit::TerminalMotion::document_end);
        terminal.insert("!");
        check(replacement.step(terminal) == swiftedit::TerminalReplaceState::cancelled &&
                  terminal.session().text() == "cat cat cat!",
              "Source edit revokes replacement authority");
        const std::filesystem::path crcr_path = dir / "existing-crcr.txt";
        {
            std::ofstream file(crcr_path, std::ios::binary);
            file << "cat\r\rcat";
        }
        terminal.reset(true);
        terminal.open(crcr_path);
        terminal.select_all();
        const swiftedit::SourceClipboard crcr_copy = terminal.copy();
        terminal.paste(crcr_copy);
        check(terminal.session().text() == "cat\r\rcat",
              "CRCR source clipboard remains byte-faithful");
        swiftedit::Session destination{};
        destination.paste_range({0, 0}, raw_copy, destination.stamp());
        check(destination.text() == malformed, "Clipboard survives source document replacement");
        const swiftedit::DocumentStamp old_destination = destination.stamp();
        destination.paste_range({0, malformed.size()}, crcr_copy, destination.stamp());
        bool stale_paste_refused = false;
        try {
            destination.paste_range({0, 0}, raw_copy, old_destination);
        } catch (const std::exception &) {
            stale_paste_refused = true;
        }
        check(stale_paste_refused && destination.text() == "cat\r\rcat",
              "Stale destination refuses clipboard publication");
        bool range_paste_refused = false;
        try {
            destination.paste_range({destination.text().size(), 1}, raw_copy, destination.stamp());
        } catch (const std::exception &) {
            range_paste_refused = true;
        }
        check(range_paste_refused && destination.text() == "cat\r\rcat",
              "Out-of-bounds clipboard publication preserves destination");
        replacement.begin(terminal, swiftedit::SearchPattern("cat"), "dog", true);
        while (replacement.state() == swiftedit::TerminalReplaceState::pending)
            static_cast<void>(replacement.step(terminal, 4));
        check(replacement.count() == 2 && terminal.session().text() == "dog\r\rdog",
              "Session-issued replacement preserves pre-existing CRCR source bytes");
        check(terminal.undo() && terminal.session().text() == "cat\r\rcat",
              "CRCR replacement remains one undoable edit");
        bool metadata_refused = false;
        try {
            replacement.begin(terminal, swiftedit::SearchPattern("cat"), "\r\rL9\r\r", true);
        } catch (const std::exception &) {
            metadata_refused = true;
        }
        check(metadata_refused && terminal.session().text() == "cat\r\rcat",
              "Caller-inserted metadata stays forbidden despite source-preserving operation");
        swiftedit::Session near_limit{};
        near_limit.replace_ranges({{0, 0}}, std::string(swiftedit::editable_limit - 2, 'x'),
                                  near_limit.stamp());
        const swiftedit::DocumentStamp before_capacity = near_limit.stamp();
        bool capacity_refused = false;
        try {
            near_limit.paste_range({0, 0}, raw_copy, before_capacity);
        } catch (const std::exception &) {
            capacity_refused = true;
        }
        check(capacity_refused && near_limit.revision() == before_capacity.revision &&
                  near_limit.size() == swiftedit::editable_limit - 2,
              "Oversized paste preserves source and revision");
        const bool original_insert_undone = near_limit.undo();
        check(original_insert_undone && near_limit.text().empty(),
              "Refused paste does not add an undo step");
        bool stale_copy_refused = false;
        try {
            static_cast<void>(near_limit.copy_range({0, 0}, before_capacity));
        } catch (const std::exception &) {
            stale_copy_refused = true;
        }
        check(stale_copy_refused, "Clipboard capture requires the observed source revision");
        swiftedit::Session issued{};
        issued.replace_ranges({{0, 0}}, "cat", issued.stamp());
        std::unique_ptr<swiftedit::SessionReplacement> plan =
            issued.prepare_replacement(swiftedit::SearchPattern("cat"), "dog");
        bool early_refused = false;
        try {
            static_cast<void>(issued.commit_replacement(*plan));
        } catch (const std::exception &) {
            early_refused = true;
        }
        check(early_refused && issued.text() == "cat",
              "Incomplete plan grants no commit authority");
        while (!(*plan).step(1)) {
        }
        swiftedit::Session other{};
        bool foreign_plan_refused = false;
        try {
            static_cast<void>(other.commit_replacement(*plan));
        } catch (const std::exception &) {
            foreign_plan_refused = true;
        }
        check(foreign_plan_refused && other.text().empty(),
              "Plan is bound to issuing Session identity");
        check(issued.commit_replacement(*plan) == 1 && issued.text() == "dog",
              "Issuing Session can commit complete plan");
        bool reuse_refused = false;
        try {
            static_cast<void>(issued.commit_replacement(*plan));
        } catch (const std::exception &) {
            reuse_refused = true;
        }
        check(reuse_refused, "Plan cannot publish twice");
        std::unique_ptr<swiftedit::SessionReplacement> same =
            issued.prepare_replacement(swiftedit::SearchPattern("dog"), "dog");
        while (!(*same).step(16)) {
        }
        const swiftedit::DocumentStamp before_same = issued.stamp();
        check(issued.commit_replacement(*same) == 1 &&
                  issued.stamp().revision == before_same.revision,
              "Identical result preserves revision and undo boundary");
        bool same_reuse_refused = false;
        try {
            static_cast<void>(issued.commit_replacement(*same));
        } catch (const std::exception &) {
            same_reuse_refused = true;
        }
        check(same_reuse_refused, "One-use consent holds even when revision did not change");
        std::unique_ptr<swiftedit::SessionReplacement> stale_plan =
            issued.prepare_replacement(swiftedit::SearchPattern("dog"), "cat");
        while (!(*stale_plan).step(16)) {
        }
        issued.replace_ranges({{0, 0}}, "new ", issued.stamp());
        bool stale_plan_refused = false;
        try {
            static_cast<void>(issued.commit_replacement(*stale_plan));
        } catch (const std::exception &) {
            stale_plan_refused = true;
        }
        check(stale_plan_refused && issued.text() == "new dog",
              "Completed plan cannot overwrite a later revision");
        std::cout << "Terminal navigation and shared edit tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
