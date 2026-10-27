#include "terminal_wrap.hpp"
#include "terminal_wrap_view.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
void verify(swiftedit::TerminalBuffer &buffer, std::size_t line, std::size_t width,
            const std::vector<std::string> &expected) {
    const swiftedit::DocumentStamp stamp = buffer.session().stamp();
    const swiftedit::SourceRange logical = buffer.line_range(line);
    std::size_t offset = logical.offset;
    bool finished = false;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        check(!finished, "Unexpected extra expected visual row");
        const swiftedit::TerminalWrappedRow row =
            swiftedit::terminal_wrapped_row(buffer, line, offset, width);
        check(row.span.source.offset == offset, "Visual rows are contiguous source spans");
        check(buffer.session().text().substr(offset, row.span.source.length) == expected[index],
              "Wrapped row differs from expected word/grapheme boundary");
        check(row.span.logical_end || row.span.source.length != 0,
              "A continuing visual row must make source progress");
        const swiftedit::TerminalSelection original_selection = buffer.selection();
        for (std::size_t column = 0; column <= width; ++column) {
            const std::size_t target =
                swiftedit::terminal_wrap_source(buffer, line, offset, width, column);
            check(target >= offset && target <= offset + row.span.source.length,
                  "Display hit testing must stay within the source row");
            buffer.move_to(target, stamp);
            const swiftedit::TerminalWrappedRow mapped =
                swiftedit::terminal_wrapped_row(buffer, line, offset, width);
            check(mapped.display.caret_column.has_value(),
                  "Each mapped caret must actually belong to the requested visual row");
        }
        buffer.move_to(original_selection.anchor, stamp);
        buffer.move_to(original_selection.caret, stamp, true);
        for (const swiftedit::TerminalRun &run : row.display.runs) {
            check(run.column + run.cells <= width, "Rendered run exceeds viewport");
            check(run.source.offset >= offset &&
                      run.source.offset + run.source.length <= offset + row.span.source.length,
                  "Rendered grapheme escapes its visual row");
            check(run.text.find('\x1b') == std::string::npos &&
                      run.text.find('\t') == std::string::npos,
                  "Terminal controls must remain inert");
        }
        offset += row.span.source.length;
        finished = row.span.logical_end;
    }
    check(finished && offset == logical.offset + logical.length,
          "Wrapping must conserve every source byte in the logical line");
    check(buffer.session().stamp().identity == stamp.identity &&
              buffer.session().stamp().revision == stamp.revision,
          "Visual wrapping must not edit the document");
}
} // namespace
void verify_view() {
    swiftedit::TerminalBuffer buffer{};
    swiftedit::TerminalWrapView view{};
    buffer.insert("abcdef\r\nx\r\nabcdef");
    buffer.move_to(2, buffer.session().stamp());
    view.move(buffer, swiftedit::TerminalMotion::down, false, 1, 3);
    check(buffer.selection().caret == 5, "Down moves by a visual row");
    view.move(buffer, swiftedit::TerminalMotion::down, false, 1, 3);
    check(buffer.selection().caret == 6, "Down enters the empty full-width continuation");
    view.move(buffer, swiftedit::TerminalMotion::down, false, 1, 3);
    check(buffer.selection().caret == 9, "Short logical line clamps the desired display column");
    view.move(buffer, swiftedit::TerminalMotion::down, false, 1, 3);
    check(buffer.selection().caret == 13, "Longer row restores the desired display column");
    view.move(buffer, swiftedit::TerminalMotion::up, true, 4, 3);
    check(buffer.selection().caret == 2 && buffer.selection().anchor == 13,
          "Page-sized backwards navigation preserves the anchor across CRLF");
    const std::vector<swiftedit::TerminalWrappedRow> &first = view.frame(buffer, 3, 2);
    check(first.size() == 2 && first[0].display.caret_column == 2,
          "Viewport reveals caret above its old top");
    view.move(buffer, swiftedit::TerminalMotion::document_end, false, 1, 3);
    const std::vector<swiftedit::TerminalWrappedRow> &last = view.frame(buffer, 3, 2);
    check(last.size() == 2 && last[1].display.caret_column == 0,
          "Viewport reveals final continuation at its bottom");
    const std::vector<swiftedit::TerminalWrappedRow> &resized = view.frame(buffer, 4, 2);
    check(resized.size() == 2 && resized[1].display.caret_column == 2,
          "Width change recomputes geometry and retains the caret's screen row");
    view.move(buffer, swiftedit::TerminalMotion::home, false, 1, 4);
    check(buffer.selection().caret == 15, "Home uses visual row start");
    view.move(buffer, swiftedit::TerminalMotion::end, true, 1, 4);
    check(buffer.selection().anchor == 15 && buffer.selection().caret == 17,
          "Shift End uses visual row end");
    buffer.insert("Z");
    const std::vector<swiftedit::TerminalWrappedRow> &edited = view.frame(buffer, 4, 2);
    check(edited.size() == 2 && edited[1].display.caret_column == 1,
          "An edit invalidates old wrap spans while preserving the caret's screen row");
    check(buffer.undo(), "Undo the reflow edit");
    const std::vector<swiftedit::TerminalWrappedRow> &undone = view.frame(buffer, 4, 2);
    check(undone.size() == 2 && undone[1].display.caret_column.has_value(),
          "Undo retains the caret's screen row with rebuilt geometry");
    const std::vector<swiftedit::TerminalWrappedRow> &shorter = view.frame(buffer, 4, 1);
    check(shorter.size() == 1 && shorter[0].display.caret_column.has_value(),
          "Height reduction keeps the caret visible within the new viewport");
    buffer.reset(true);
    const std::vector<swiftedit::TerminalWrappedRow> &empty = view.frame(buffer, 4, 2);
    check(empty.size() == 1 && empty[0].display.caret_column == 0,
          "A new document invalidates retained viewport state");
    bool refused = false;
    try {
        static_cast<void>(view.frame(buffer, 4, 297));
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, "Visible row retention has an explicit upper bound");
}
class CancelWrap final : public swiftedit::TerminalWrapControl {
public:
    std::size_t calls{}, stop_at{2};
    bool cancel_requested() override {
        ++calls;
        const bool result = stop_at && calls >= stop_at;
        return result;
    }
};
void verify_cancellation() {
    swiftedit::TerminalBuffer buffer{};
    buffer.insert(std::string(100000, 'x'));
    buffer.move(swiftedit::TerminalMotion::document_end);
    const swiftedit::DocumentStamp stamp = buffer.session().stamp();
    CancelWrap cancellation{};
    swiftedit::TerminalWrapView view{&cancellation};
    bool cancelled = false;
    try {
        static_cast<void>(view.frame(buffer, 80, 24));
    } catch (const swiftedit::TerminalWrapInterrupt &) {
        cancelled = true;
    }
    check(cancelled && cancellation.calls == 2,
          "Long index preparation checks cancellation during source traversal");
    check(buffer.selection().caret == 100000 && buffer.selection().anchor == 100000 &&
              buffer.session().stamp().revision == stamp.revision,
          "Cancelled layout leaves source revision and selection unchanged");
    // Returning to the start must not first retry the cancelled end layout.
    view.move(buffer, swiftedit::TerminalMotion::document_start, false, 1, 80);
    check(buffer.selection().caret == 0 && cancellation.calls == 2,
          "Document Home bypasses expensive cancelled wrap preparation");
    cancellation.stop_at = 0;
    const std::vector<swiftedit::TerminalWrappedRow> &resumed = view.frame(buffer, 80, 24);
    check(resumed[0].display.caret_column == 0,
          "A cancelled preparation can recover at a different position");
    buffer.move(swiftedit::TerminalMotion::document_end);
    cancellation.calls = 0;
    cancellation.stop_at = 2;
    cancelled = false;
    try {
        view.move(buffer, swiftedit::TerminalMotion::up, true, 24, 80);
    } catch (const swiftedit::TerminalWrapInterrupt &) {
        cancelled = true;
    }
    check(cancelled && buffer.selection().caret == 100000 && buffer.selection().anchor == 100000,
          "Interrupted page movement never publishes a partial selection");
    cancellation.stop_at = 0;
    view.move(buffer, swiftedit::TerminalMotion::up, true, 24, 80);
    check(buffer.selection().caret == 98080 && buffer.selection().anchor == 100000,
          "A later page movement succeeds with intact source and anchor");
}
void verify_scroll() {
    swiftedit::TerminalBuffer buffer{};
    swiftedit::TerminalWrapView view{};
    buffer.insert(std::string(1000, 'x'));
    buffer.move_to(2, buffer.session().stamp());
    static_cast<void>(view.frame(buffer, 7, 4));
    std::size_t expected_top = 0;
    for (std::size_t row = 1; row <= 40; ++row) {
        view.move(buffer, swiftedit::TerminalMotion::down, false, 1, 7);
        const std::vector<swiftedit::TerminalWrappedRow> &frame = view.frame(buffer, 7, 4);
        expected_top = row > 3 ? row - 3 : 0;
        check(frame[0].span.source.offset == expected_top * 7,
              "Crossing the viewport bottom scrolls by one visual row");
        check(frame[row - expected_top].display.caret_column == 2,
              "Downward scrolling keeps the caret at its expected screen position");
    }
    for (std::size_t row = 40; row > 0;) {
        --row;
        view.move(buffer, swiftedit::TerminalMotion::up, false, 1, 7);
        const std::vector<swiftedit::TerminalWrappedRow> &frame = view.frame(buffer, 7, 4);
        expected_top = std::min(expected_top, row);
        check(frame[0].span.source.offset == expected_top * 7,
              "Crossing the viewport top scrolls by one visual row");
        check(frame[row - expected_top].display.caret_column == 2,
              "Upward scrolling preserves the desired column");
    }
}
void verify_checkpoints() {
    swiftedit::TerminalBuffer buffer{};
    swiftedit::TerminalWrapView view{};
    buffer.insert(std::string(12000, 'x'));
    const std::size_t positions[] = {4095, 4096, 4159, 4160, 8191, 8192, 12000};
    for (const std::size_t offset : positions) {
        buffer.move_to(offset, buffer.session().stamp());
        view.move(buffer, swiftedit::TerminalMotion::up, false, 1, 80);
        check(buffer.selection().caret == offset - 80,
              "Reverse movement across sparse checkpoint boundaries preserves column");
        view.move(buffer, swiftedit::TerminalMotion::down, false, 1, 80);
        check(buffer.selection().caret == offset,
              "Forward movement returns across sparse checkpoint boundaries");
    }
    buffer.reset(true);
    std::string source{};
    for (std::size_t line = 0; line < 325; ++line)
        source += "xxxxxxxxxxxxxxxxxxxx\n";
    buffer.insert(source);
    for (std::size_t visit = 0; visit < 326; ++visit) {
        const std::size_t line = visit == 325 ? 0 : visit;
        buffer.move_to(line * 21 + 10, buffer.session().stamp());
        view.move(buffer, swiftedit::TerminalMotion::up, false, 1, 5);
        check(buffer.selection().caret == line * 21 + 5,
              "Evicted logical-line indexes rebuild with correct source positions");
    }
    buffer.move_to(10, buffer.session().stamp());
    view.move(buffer, swiftedit::TerminalMotion::up, false, 1, 4);
    check(buffer.selection().caret == 6, "Resize invalidates existing sparse checkpoints");
}
int main() {
    try {
        verify_view();
        verify_cancellation();
        verify_scroll();
        verify_checkpoints();
        swiftedit::TerminalBuffer buffer{};
        verify(buffer, 0, 5, {""});
        buffer.insert("one two three");
        verify(buffer, 0, 8, {"one two ", "three"});
        verify(buffer, 0, 6, {"one ", "two ", "three"});
        check(swiftedit::terminal_wrap_source(buffer, 0, 0, 6, 5) == 3,
              "Past a soft wrap stays at the last caret owned by that row");
        check(swiftedit::terminal_wrap_source(buffer, 0, 8, 6, 100) == 13,
              "Past the final row maps to the logical end");
        const swiftedit::DocumentStamp navigation_stamp = buffer.session().stamp();
        buffer.move_to(8, navigation_stamp);
        buffer.move_to(4, navigation_stamp, true);
        buffer.move_to(0, navigation_stamp, true);
        check(buffer.selection().anchor == 8 && buffer.selection().caret == 0,
              "Repeated backwards visual moves preserve the selection anchor");
        buffer.move_to(10, navigation_stamp, true);
        check(buffer.selection().anchor == 8 && buffer.selection().caret == 10,
              "Visual movement can cross the original anchor");
        buffer.move_to(13, navigation_stamp);
        buffer.insert("!");
        bool stale_refused = false;
        try {
            buffer.move_to(0, navigation_stamp, true);
        } catch (const std::exception &) {
            stale_refused = true;
        }
        check(stale_refused && buffer.selection().anchor == 14 && buffer.selection().caret == 14,
              "Layout from before an edit cannot change selection");
        const swiftedit::DocumentStamp old_identity = buffer.session().stamp();
        buffer.reset(true);
        bool identity_refused = false;
        try {
            buffer.move_to(0, old_identity);
        } catch (const std::exception &) {
            identity_refused = true;
        }
        check(identity_refused, "Layout from a different document is refused");
        buffer.insert("abcdef");
        verify(buffer, 0, 3, {"abc", "def", ""});
        buffer.move(swiftedit::TerminalMotion::document_end);
        const swiftedit::TerminalWrappedRow full = swiftedit::terminal_wrapped_row(buffer, 0, 3, 3);
        const swiftedit::TerminalWrappedRow end = swiftedit::terminal_wrapped_row(buffer, 0, 6, 3);
        check(!full.display.caret_column && end.display.caret_column == 0,
              "Full-width end caret belongs to the empty continuation row");
        buffer.reset(true);
        buffer.insert("a\tb");
        verify(buffer, 0, 4, {"a\t", "b"});
        check(swiftedit::terminal_wrap_source(buffer, 0, 0, 4, 2) == 1,
              "Inside a tab maps to its source start");
        buffer.reset(true);
        buffer.insert("e\xcc\x81\xe7\x95\x8c");
        verify(buffer, 0, 2, {"e\xcc\x81", "\xe7\x95\x8c", ""});
        verify(buffer, 0, 1, {"e\xcc\x81", "\xe7\x95\x8c", ""});
        check(swiftedit::terminal_wrap_source(buffer, 0, 3, 2, 1) == 3,
              "Inside a wide character maps to its source start");
        check(swiftedit::terminal_wrap_source(buffer, 0, 6, 2, 50) == 6,
              "Empty full-width continuation owns the end position");
        const swiftedit::TerminalSelection before_invalid = buffer.selection();
        bool invalid_move_refused = false;
        try {
            buffer.move_to(1, buffer.session().stamp(), true);
        } catch (const std::exception &) {
            invalid_move_refused = true;
        }
        check(invalid_move_refused && buffer.selection().anchor == before_invalid.anchor &&
                  buffer.selection().caret == before_invalid.caret,
              "A move inside a combining grapheme leaves selection unchanged");
        bool refused = false;
        try {
            static_cast<void>(swiftedit::terminal_wrap_span(buffer, 0, 1, 5));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "A wrap start inside a combining grapheme is refused");
        buffer.reset(true);
        buffer.insert("x\r\ny\nz");
        verify(buffer, 0, 4, {"x"});
        verify(buffer, 1, 4, {"y"});
        verify(buffer, 2, 4, {"z"});
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("swiftedit-wrap-" + std::to_string(test_process_id()) + ".txt");
        check(!std::filesystem::exists(path), "Unique malformed fixture");
        struct Cleanup {
            std::filesystem::path path{};
            ~Cleanup() {
                std::error_code error{};
                std::filesystem::remove(path, error);
            }
        } cleanup{path};
        {
            std::ofstream output(path, std::ios::binary);
            output << "a\xff\x1b";
        }
        buffer.reset(true);
        buffer.open(path);
        verify(buffer, 0, 1, {"a", "\xff", "\x1b", ""});
        check(buffer.session().illegal_bytes() == 1 && !buffer.session().dirty(),
              "Malformed byte and control remain literal unmodified source");
        std::cout << "Terminal wrap layout passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
