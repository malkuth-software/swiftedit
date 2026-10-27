#include "terminal_app.hpp"
#include "platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

class ScriptConsole final : public swiftedit::TerminalConsole {
public:
    std::vector<swiftedit::TerminalInput> inputs{};
    mutable std::size_t cursor{}, writes{};
    mutable bool end_seen{}, copy_seen{};
    bool wait_for_end{}, wait_for_copy{}, wait_for_previous{};
    mutable bool previous_pending{}, previous_seen{}, search_pending{}, single_copy_seen{};
    bool wait_for_search{};
    mutable std::size_t found_count{};
    mutable bool three_copy_seen{}, two_copy_seen{}, caret_completed{};
    mutable bool line_pending{}, line_completed{};
    bool wait_for_line{}, wait_for_text_copy{}, wait_for_publication{};
    mutable bool publication_seen{};
    mutable bool text_copy_pending{}, text_copy_saved{};
    bool started{};
    std::size_t inspect_after{};
    std::string expected_cursor{};
    mutable bool cursor_checked{};
    std::string expected_copy{};
    mutable bool expected_copy_seen{};
    bool cancel_repeats{};
    bool wait_for_no_wrap{};
    mutable bool no_wrap_pending{};
    mutable std::size_t no_wrap_frames{};
    mutable std::size_t no_wrap_requests{};
    std::size_t resize_after{}, cancel_no_wrap_after{};
    std::string expected_no_wrap_cursor{};
    mutable bool no_wrap_cursor_seen{};
    mutable bool repeats_cancelled{};
    void start() override { started = true; }
    swiftedit::TerminalSize size() const override {
        if (resize_after && cursor >= resize_after)
            return {40, 12};
        return {80, 24};
    }
    void write(std::string_view text) const override {
        if (text.find("Preparing no-wrap view...") != std::string_view::npos)
            ++no_wrap_requests;
        if (text.find("Preparing no-wrap view...") != std::string_view::npos ||
            text.find("Moving by logical lines...") != std::string_view::npos)
            no_wrap_pending = true;
        if (text.find("No wrap") != std::string_view::npos) {
            no_wrap_pending = false;
            ++no_wrap_frames;
            if (resize_after && cursor >= resize_after && !expected_no_wrap_cursor.empty() &&
                text.find(expected_no_wrap_cursor + "\x1b[?25h") != std::string_view::npos)
                no_wrap_cursor_seen = true;
        }
        if (text.find("No-wrap navigation cancelled") != std::string_view::npos)
            no_wrap_pending = false;
        if (!expected_copy.empty() && text.find(expected_copy) != std::string_view::npos)
            expected_copy_seen = true;
        if (inspect_after && cursor == inspect_after) {
            if (text.find(expected_cursor + "\x1b[?25h") == std::string_view::npos)
                throw std::runtime_error("Read-only vertical navigation displayed the wrong caret.");
            cursor_checked = true;
        }
        if (text.empty() || !started)
            throw std::runtime_error("Unexpected empty or premature terminal frame.");
        ++writes;
        if (text.find("Publishing text copy...") != std::string_view::npos)
            publication_seen = true;
        if (text.find("Saving text copy...") != std::string_view::npos)
            text_copy_pending = true;
        if (text.find("Text copy saved; open document unchanged") != std::string_view::npos) {
            text_copy_pending = false;
            text_copy_saved = true;
        }
        if (text.find("Finding line boundary...") != std::string_view::npos)
            line_pending = true;
        if (text.find("Read-only line boundary reached") != std::string_view::npos) {
            line_pending = false;
            line_completed = true;
        }
        if (text.find("Finding previous grapheme...") != std::string_view::npos)
            previous_pending = true;
        if (text.find("Read-only caret moved") != std::string_view::npos && previous_pending) {
            caret_completed = true;
            previous_pending = false;
        }
        if (text.find("Copied 3 bytes") != std::string_view::npos)
            three_copy_seen = true;
        if (text.find("Copied 2 bytes") != std::string_view::npos)
            two_copy_seen = true;
        if (text.find("Searching...") != std::string_view::npos)
            search_pending = true;
        if (text.find("Found") != std::string_view::npos) {
            ++found_count;
            search_pending = false;
        }
        if (text.find("Copied 1 bytes") != std::string_view::npos)
            single_copy_seen = true;
        if (text.find("Finding earlier page...") != std::string_view::npos)
            previous_pending = true;
        if (text.find("Earlier read-only page") != std::string_view::npos) {
            previous_seen = true;
            previous_pending = false;
        }
        if (text.find("End of read-only document") != std::string_view::npos)
            end_seen = true;
        if (text.find("Copied 16777216 bytes") != std::string_view::npos)
            copy_seen = true;
    }
    bool input_ready() const override {
        const bool ready = cursor < inputs.size() && (!wait_for_end || end_seen) &&
                           (!wait_for_no_wrap || !no_wrap_pending || cursor == cancel_no_wrap_after) &&
                           (!wait_for_publication || publication_seen) && (!wait_for_text_copy || !text_copy_pending) && (!wait_for_line || !line_pending) && (!wait_for_copy || copy_seen) && (!wait_for_previous || !previous_pending) &&
                           (!wait_for_search || !search_pending);
        return ready;
    }
    swiftedit::TerminalInput read() const override {
        if (cursor == inputs.size())
            throw std::runtime_error("Terminal requested unexpected input.");
        const swiftedit::TerminalInput result = inputs[cursor];
        ++cursor;
        return result;
    }
    bool cancel_requested() override {
        if (cancel_repeats && cursor == 1 && !repeats_cancelled) {
            repeats_cancelled = true;
            return true;
        }
        return false;
    }
    void allow_wrap_cancel(bool) override {}
    void press(std::uint32_t key, char16_t text = 0, bool control = false, bool shift = false) {
        swiftedit::TerminalInput input{};
        input.key = key;
        input.text_unit = text;
        input.control = control;
        input.shift = shift;
        input.pressed = true;
        inputs.push_back(input);
    }
};
// Flush each boundary so a CI timeout identifies the active scenario rather
// than discarding every observation with the killed test process.
int run_case(swiftedit::Terminal &terminal, const std::filesystem::path &path,
             const std::string_view name) {
    std::cerr << "Starting terminal scenario: " << name << std::endl;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const int result = terminal.run(path);
    const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(finished - started).count();
    std::cerr << "Finished terminal scenario: " << name << " (" << elapsed << " s)" << std::endl;
    return result;
}
int main() {
    try {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-terminal-app-" + std::to_string(test_process_id()));
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Terminal fixture directory already exists.");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const std::filesystem::path path = directory / swiftedit::terminal_path("caf\xc3\xa9.txt");
        {
            std::ofstream output(path, std::ios::binary);
            output << "base\r\n";
        }
        ScriptConsole console{};
        console.inputs.push_back({}); // Non-key event must not redraw or mutate.
        swiftedit::TerminalInput resized{};
        resized.resized = true;
        console.inputs.push_back(resized);
        console.press('A', u'A');
        console.press(0, 0xd83d);
        console.press(0, 0xde00);
        console.press('Z', 0, true); // Undo supplementary scalar as one insertion.
        console.press('Y', 0, true);
        console.press(swiftedit::terminal_key::left);
        console.press(swiftedit::terminal_key::erase);
        console.press('S', 0, true);
        console.press('X', 0, true);
        swiftedit::Terminal terminal(console);
        const int result = run_case(terminal, path, "terminal");
        if (result != 0 || console.cursor != console.inputs.size() || console.writes == 0 ||
            notepad::read_file(path).bytes != "Abase\r\n")
            throw std::runtime_error("Shared terminal Unicode/navigation/save behavior failed.");
        ScriptConsole invalid{};
        invalid.press(0, 0xdc00); // A lone low surrogate must not become source text.
        invalid.press('Q', u'Q');
        invalid.press('S', 0, true);
        invalid.press('X', 0, true);
        swiftedit::Terminal recovered(invalid);
        if (run_case(recovered, path, "recovered") != 0 || notepad::read_file(path).bytes != "QAbase\r\n")
            throw std::runtime_error("Invalid input recovery changed source incorrectly.");
        ScriptConsole pasted{};
        swiftedit::TerminalInput paste{};
        paste.pasted = true;
        paste.paste = "\x13\x18line\r\n";
        pasted.inputs.push_back(paste);
        pasted.press('Z', 0, true);
        pasted.press('Y', 0, true);
        pasted.press('S', 0, true);
        pasted.press('X', 0, true);
        swiftedit::Terminal paste_terminal(pasted);
        if (run_case(paste_terminal, path, "paste_terminal") != 0 || pasted.cursor != pasted.inputs.size() ||
            notepad::read_file(path).bytes != paste.paste + "QAbase\r\n")
            throw std::runtime_error("Paste executed commands or failed atomic undo/redo.");
        const std::string original = notepad::read_file(path).bytes;
        ScriptConsole cancelled{};
        swiftedit::TerminalInput large{};
        large.pasted = true;
        large.paste.assign(500001, 'x');
        cancelled.inputs.push_back(large);
        swiftedit::TerminalInput false_confirmation{};
        false_confirmation.pasted = true;
        false_confirmation.paste = "Y\r\n";
        cancelled.inputs.push_back(false_confirmation);
        cancelled.press('Y', 0, true); // Ctrl+Y must not approve a paste.
        cancelled.press('S', 0, true); // Other commands must not bypass the choice.
        cancelled.press(swiftedit::terminal_key::escape);
        cancelled.press('X', 0, true);
        swiftedit::Terminal cancel_terminal(cancelled);
        if (run_case(cancel_terminal, path, "cancel_terminal") != 0 || cancelled.cursor != cancelled.inputs.size() ||
            notepad::read_file(path).bytes != original)
            throw std::runtime_error("Large paste cancellation or explicit confirmation failed.");
        ScriptConsole undone{};
        undone.inputs.push_back(large);
        undone.press('Y', u'y');
        undone.press('Z', 0, true);
        undone.press('X', 0, true); // Must exit cleanly without an unsaved-changes choice.
        swiftedit::Terminal undone_terminal(undone);
        if (run_case(undone_terminal, path, "undone_terminal") != 0 || undone.cursor != undone.inputs.size() ||
            notepad::read_file(path).bytes != original)
            throw std::runtime_error("One undo did not remove the entire confirmed paste.");
        ScriptConsole approved{};
        approved.inputs.push_back(large);
        approved.press('Y', u'y');
        approved.press('Z', 0, true);
        approved.press('Y', 0, true);
        approved.press('S', 0, true);
        approved.press('X', 0, true);
        swiftedit::Terminal approved_terminal(approved);
        if (run_case(approved_terminal, path, "approved_terminal") != 0 || approved.cursor != approved.inputs.size() ||
            notepad::read_file(path).bytes != large.paste + original)
            throw std::runtime_error("Confirmed large paste did not remain one undoable edit.");
        ScriptConsole private_clipboard{};
        private_clipboard.press('A', 0, true);
        private_clipboard.press('C', 0, true);
        private_clipboard.press(swiftedit::terminal_key::home, 0, true);
        private_clipboard.press('U', 0, true);
        private_clipboard.press('N', u'n');
        private_clipboard.press('S', 0, true);
        private_clipboard.press('X', 0, true);
        swiftedit::Terminal private_terminal(private_clipboard);
        if (run_case(private_terminal, path, "private_terminal") != 0 || private_clipboard.cursor != private_clipboard.inputs.size() ||
            notepad::read_file(path).bytes != large.paste + original)
            throw std::runtime_error("Private clipboard bypassed large paste confirmation.");
        ScriptConsole threshold{};
        large.paste.resize(500000);
        threshold.inputs.push_back(large);
        threshold.press('S', 0, true);
        threshold.press('X', 0, true);
        swiftedit::Terminal threshold_terminal(threshold);
        if (run_case(threshold_terminal, path, "threshold_terminal") != 0 || threshold.cursor != threshold.inputs.size() ||
            notepad::read_file(path).bytes != large.paste + std::string(500001, 'x') + original)
            throw std::runtime_error("Exactly 500000 paste bytes incorrectly required confirmation.");
        const std::filesystem::path large_path = directory / "large-read-only.txt";
        {
            std::ofstream output(large_path, std::ios::binary);
            output.seekp(swiftedit::editable_limit - 1);
            output.put('z');
        }
        const std::filesystem::path saved_copy = swiftedit::versioned_name(large_path);
        const std::filesystem::path vertical_path = directory / "vertical-read-only.txt";
        {
            std::ofstream output(vertical_path, std::ios::binary);
            output << "abcdef\nx\nabcdef\n";
            for (std::size_t row = 0; row < 30; ++row)
                output << "abcdef\n";
            output.seekp(swiftedit::editable_limit - 1);
            output.put('z');
        }
        ScriptConsole repeated_rows{};
        repeated_rows.press(swiftedit::terminal_key::down, 0, false, true);
        repeated_rows.inputs.back().repeats = 23;
        repeated_rows.expected_copy = "Copied 156 bytes";
        repeated_rows.press('C', 0, true);
        repeated_rows.press('X', 0, true);
        swiftedit::Terminal repeated_terminal(repeated_rows);
        if (run_case(repeated_terminal, vertical_path, "repeated_terminal") != 0 || !repeated_rows.expected_copy_seen)
            throw std::runtime_error("Navigation slicing lost repetitions or changed the selection anchor.");
        ScriptConsole cancelled_repeats{};
        cancelled_repeats.cancel_repeats = true;
        cancelled_repeats.press(swiftedit::terminal_key::down);
        cancelled_repeats.inputs.back().repeats = 1000;
        cancelled_repeats.inspect_after = 1;
        cancelled_repeats.expected_cursor = "\x1b[18;1H";
        cancelled_repeats.press('X', 0, true);
        swiftedit::Terminal cancelled_repeat_terminal(cancelled_repeats);
        if (run_case(cancelled_repeat_terminal, vertical_path, "cancelled_repeat_terminal") != 0 || !cancelled_repeats.repeats_cancelled || !cancelled_repeats.cursor_checked)
            throw std::runtime_error("Escape did not cancel repeated navigation after a bounded slice.");
        for (const std::size_t moves : {std::size_t{2}, std::size_t{23}}) {
            ScriptConsole vertical{};
            for (std::size_t column = 0; column < 3; ++column)
                vertical.press(swiftedit::terminal_key::right);
            for (std::size_t row = 0; row < moves; ++row)
                vertical.press(swiftedit::terminal_key::down);
            vertical.inspect_after = 3 + moves;
            vertical.expected_cursor = moves == 2 ? "\x1b[4;4H" : "\x1b[21;4H";
            vertical.press('X', 0, true);
            swiftedit::Terminal vertical_terminal(vertical);
            if (run_case(vertical_terminal, vertical_path, "vertical_terminal") != 0 || !vertical.cursor_checked)
                throw std::runtime_error("Read-only desired column was not restored after short lines or scrolling.");
        }
        ScriptConsole vertical_return{};
        for (std::size_t column = 0; column < 3; ++column)
            vertical_return.press(swiftedit::terminal_key::right);
        for (std::size_t row = 0; row < 23; ++row)
            vertical_return.press(swiftedit::terminal_key::down);
        for (std::size_t row = 0; row < 23; ++row)
            vertical_return.press(swiftedit::terminal_key::up, 0, false, true);
        vertical_return.inspect_after = 49;
        vertical_return.expected_cursor = "\x1b[2;4H";
        vertical_return.expected_copy = "Copied 156 bytes";
        vertical_return.press('C', 0, true);
        vertical_return.press('X', 0, true);
        swiftedit::Terminal return_terminal(vertical_return);
        if (run_case(return_terminal, vertical_path, "return_terminal") != 0 || !vertical_return.cursor_checked || !vertical_return.expected_copy_seen)
            throw std::runtime_error("Upward scrolling lost the desired column or selection caret.");
        ScriptConsole page_return{};
        for (std::size_t column = 0; column < 3; ++column)
            page_return.press(swiftedit::terminal_key::right);
        page_return.press(swiftedit::terminal_key::down);
        page_return.press(swiftedit::terminal_key::down);
        page_return.press(swiftedit::terminal_key::page_down);
        page_return.press(swiftedit::terminal_key::page_up, 0, false, true);
        page_return.inspect_after = 7;
        page_return.expected_cursor = "\x1b[4;4H";
        page_return.expected_copy = "Copied 140 bytes";
        page_return.press('C', 0, true);
        page_return.press('X', 0, true);
        swiftedit::Terminal page_return_terminal(page_return);
        if (run_case(page_return_terminal, vertical_path, "page_return_terminal") != 0 || !page_return.cursor_checked || !page_return.expected_copy_seen)
            throw std::runtime_error("Page navigation lost its screen row, desired column or selection anchor.");
        ScriptConsole copy_navigation{};
        copy_navigation.wait_for_text_copy = true;
        copy_navigation.press('T', 0, true);
        copy_navigation.press(swiftedit::terminal_key::enter);
        copy_navigation.press('X', 0, true);
        swiftedit::Terminal copy_terminal(copy_navigation);
        if (run_case(copy_terminal, large_path, "copy_terminal") != 0 || !copy_navigation.text_copy_saved)
            throw std::runtime_error("Read-only Save Text Copy did not complete cooperatively.");
        {
            swiftedit::PagedFile copied(saved_copy);
            if (copied.size() != swiftedit::editable_limit || copied.page(copied.size() - 1, 1).bytes != "z")
                throw std::runtime_error("Read-only text copy lost source bytes.");
        }
        if (!std::filesystem::remove(saved_copy))
            throw std::runtime_error("Could not remove the completed owned copy fixture.");
        const std::filesystem::path cancelled_copy_path = swiftedit::versioned_name(large_path);
        ScriptConsole cancelled_copy{};
        cancelled_copy.press('T', 0, true);
        cancelled_copy.press(swiftedit::terminal_key::enter);
        cancelled_copy.press(swiftedit::terminal_key::escape);
        cancelled_copy.press('X', 0, true);
        swiftedit::Terminal cancelled_copy_terminal(cancelled_copy);
        if (run_case(cancelled_copy_terminal, large_path, "cancelled_copy_terminal") != 0 || cancelled_copy.text_copy_saved ||
            std::filesystem::exists(cancelled_copy_path))
            throw std::runtime_error("Read-only text copy cancellation published output.");
        ScriptConsole exit_during_copy{};
        exit_during_copy.wait_for_publication = true;
        exit_during_copy.press('T', 0, true);
        exit_during_copy.press(swiftedit::terminal_key::enter);
        exit_during_copy.press('X', 0, true);
        swiftedit::Terminal exit_copy_terminal(exit_during_copy);
        if (run_case(exit_copy_terminal, large_path, "exit_copy_terminal") != 0 || !exit_during_copy.publication_seen ||
            !std::filesystem::exists(cancelled_copy_path) ||
            exit_during_copy.cursor != exit_during_copy.inputs.size())
            throw std::runtime_error("Exit during publication did not retain the worker to completion.");
        ScriptConsole end_navigation{};
        end_navigation.wait_for_end = true;
        end_navigation.wait_for_copy = true;
        end_navigation.press(swiftedit::terminal_key::end, 0, true, true);
        end_navigation.press('C', 0, true);
        end_navigation.press('X', 0, true);
        swiftedit::Terminal end_terminal(end_navigation);
        if (run_case(end_terminal, large_path, "end_terminal") != 0 || !end_navigation.end_seen || !end_navigation.copy_seen ||
            end_navigation.cursor != end_navigation.inputs.size())
            throw std::runtime_error("Read-only Ctrl+End failed to finish cooperatively.");
        ScriptConsole rewind_navigation{};
        rewind_navigation.wait_for_end = true;
        rewind_navigation.wait_for_previous = true;
        rewind_navigation.press(swiftedit::terminal_key::end, 0, true);
        for (std::size_t batch = 0; batch < 32; ++batch) {
            rewind_navigation.press(swiftedit::terminal_key::up);
            rewind_navigation.inputs.back().repeats = 1000;
        }
        rewind_navigation.press(swiftedit::terminal_key::up);
        rewind_navigation.inputs.back().repeats = 768;
        rewind_navigation.press(swiftedit::terminal_key::page_up, 0, false, true);
        rewind_navigation.press('X', 0, true);
        swiftedit::Terminal rewind_terminal(rewind_navigation);
        if (run_case(rewind_terminal, large_path, "rewind_terminal") != 0 || !rewind_navigation.previous_seen ||
            rewind_navigation.cursor != rewind_navigation.inputs.size())
            throw std::runtime_error("Read-only Page Up did not rebuild exhausted history cooperatively.");
        ScriptConsole repeated_rewind{};
        repeated_rewind.wait_for_end = true;
        repeated_rewind.wait_for_previous = true;
        repeated_rewind.inputs = rewind_navigation.inputs;
        // Cross history exhaustion within the last repeated Up event. Its
        // reconstruction replenishes history, so the following Page Up need
        // not reconstruct again; observe the Up completion instead.
        repeated_rewind.inputs[repeated_rewind.inputs.size() - 3].repeats = 900;
        swiftedit::Terminal repeated_rewind_terminal(repeated_rewind);
        if (run_case(repeated_rewind_terminal, large_path, "repeated_rewind_terminal") != 0 || !repeated_rewind.caret_completed ||
            repeated_rewind.cursor != repeated_rewind.inputs.size())
            throw std::runtime_error("Repeated Up cancelled its own pending reconstruction.");
        ScriptConsole paged_find{};
        paged_find.wait_for_search = true;
        paged_find.press('W', 0, true);
        paged_find.press('Z', u'z');
        paged_find.press(swiftedit::terminal_key::enter);
        paged_find.press(swiftedit::terminal_key::f3);
        paged_find.press('C', 0, true);
        paged_find.press('X', 0, true);
        swiftedit::Terminal find_terminal(paged_find);
        if (run_case(find_terminal, large_path, "find_terminal") != 0 || paged_find.found_count != 2 || !paged_find.single_copy_seen)
            throw std::runtime_error("Paged Find/F3 did not select, wrap and copy the exact final match.");
        ScriptConsole cancelled_find{};
        cancelled_find.press('W', 0, true);
        cancelled_find.press('Z', u'z');
        cancelled_find.press(swiftedit::terminal_key::enter);
        cancelled_find.press(swiftedit::terminal_key::escape);
        cancelled_find.press('X', 0, true);
        swiftedit::Terminal cancelled_find_terminal(cancelled_find);
        if (run_case(cancelled_find_terminal, large_path, "cancelled_find_terminal") != 0 || cancelled_find.found_count)
            throw std::runtime_error("Paged Find did not cancel before publishing a match.");
        const std::filesystem::path horizontal_path = directory / "horizontal-read-only.txt";
        {
            std::ofstream output(horizontal_path, std::ios::binary);
            output << "e\xcc\x81\r\n\x1bZ";
            output.seekp(swiftedit::editable_limit - 1);
            output.put('z');
        }
        ScriptConsole horizontal{};
        horizontal.press(swiftedit::terminal_key::right, 0, false, true);
        horizontal.press('C', 0, true);
        horizontal.press(swiftedit::terminal_key::right);
        horizontal.press(swiftedit::terminal_key::right, 0, false, true);
        horizontal.press('C', 0, true);
        horizontal.press(swiftedit::terminal_key::right);
        horizontal.press(swiftedit::terminal_key::right, 0, false, true);
        horizontal.press('C', 0, true);
        horizontal.press('X', 0, true);
        swiftedit::Terminal horizontal_terminal(horizontal);
        if (run_case(horizontal_terminal, horizontal_path, "horizontal_terminal") != 0 || !horizontal.three_copy_seen ||
            !horizontal.two_copy_seen || !horizontal.single_copy_seen)
            throw std::runtime_error("Shift+Right did not select atomic Unicode, CRLF and control source bytes.");
        ScriptConsole line_selection{};
        line_selection.wait_for_line = true;
        line_selection.press(swiftedit::terminal_key::end, 0, false, true);
        line_selection.press('C', 0, true);
        line_selection.press(swiftedit::terminal_key::home);
        line_selection.press(swiftedit::terminal_key::right, 0, false, true);
        line_selection.press('C', 0, true);
        line_selection.press('X', 0, true);
        swiftedit::Terminal line_terminal(line_selection);
        if (run_case(line_terminal, horizontal_path, "line_terminal") != 0 || !line_selection.line_completed ||
            !line_selection.three_copy_seen)
            throw std::runtime_error("Read-only Home/End failed to select the logical line.");
        ScriptConsole cancelled_line{};
        cancelled_line.press('A', 0, true);
        cancelled_line.press(swiftedit::terminal_key::right);
        cancelled_line.press(swiftedit::terminal_key::home);
        cancelled_line.press(swiftedit::terminal_key::escape);
        cancelled_line.press('X', 0, true);
        swiftedit::Terminal cancelled_line_terminal(cancelled_line);
        if (run_case(cancelled_line_terminal, horizontal_path, "cancelled_line_terminal") != 0 || cancelled_line.line_completed)
            throw std::runtime_error("Read-only Home did not cancel before publishing its result.");
        ScriptConsole distant_left{};
        distant_left.wait_for_previous = true;
        distant_left.press('A', 0, true);
        distant_left.press(swiftedit::terminal_key::right);
        distant_left.press(swiftedit::terminal_key::left, 0, false, true);
        distant_left.press('C', 0, true);
        distant_left.press('X', 0, true);
        swiftedit::Terminal left_terminal(distant_left);
        if (run_case(left_terminal, horizontal_path, "left_terminal") != 0 || !distant_left.caret_completed ||
            !distant_left.single_copy_seen)
            throw std::runtime_error("Shift+Left from an off-screen EOF did not reconstruct and copy the final grapheme.");
        ScriptConsole cancelled_left{};
        cancelled_left.press('A', 0, true);
        cancelled_left.press(swiftedit::terminal_key::right);
        cancelled_left.press(swiftedit::terminal_key::left, 0, false, true);
        cancelled_left.press(swiftedit::terminal_key::escape);
        cancelled_left.press('X', 0, true);
        swiftedit::Terminal cancelled_left_terminal(cancelled_left);
        if (run_case(cancelled_left_terminal, horizontal_path, "cancelled_left_terminal") != 0 || cancelled_left.caret_completed)
            throw std::runtime_error("Previous-grapheme scan did not cancel before publication.");
        ScriptConsole cancelled_end{};
        cancelled_end.press(swiftedit::terminal_key::end, 0, true);
        cancelled_end.press(swiftedit::terminal_key::escape);
        cancelled_end.press('X', 0, true);
        swiftedit::Terminal cancelled_end_terminal(cancelled_end);
        if (run_case(cancelled_end_terminal, large_path, "cancelled_end_terminal") != 0 || cancelled_end.end_seen ||
            cancelled_end.cursor != cancelled_end.inputs.size())
            throw std::runtime_error("Read-only Ctrl+End did not cancel on the next key.");
        const std::filesystem::path nowrap_path = directory / "no-wrap-read-only.txt";
        {
            std::ofstream output(nowrap_path, std::ios::binary);
            output << std::string(200, 'a') << "\r\nbcd\r\n";
            for (std::size_t row = 0; row < 100; ++row)
                output << "row\r\n";
            output.seekp(swiftedit::editable_limit - 1);
            output.put('z');
        }
        ScriptConsole nowrap{};
        nowrap.wait_for_no_wrap = true;
        nowrap.expected_copy = "Copied 202 bytes";
        nowrap.press(swiftedit::terminal_key::f2);
        nowrap.press(swiftedit::terminal_key::right);
        nowrap.press(swiftedit::terminal_key::right);
        nowrap.press(swiftedit::terminal_key::down, 0, false, true);
        nowrap.press('C', 0, true);
        nowrap.press(swiftedit::terminal_key::up);
        nowrap.press(swiftedit::terminal_key::f2);
        nowrap.press(swiftedit::terminal_key::right, 0, false, true);
        nowrap.press('C', 0, true);
        nowrap.press('X', 0, true);
        swiftedit::Terminal nowrap_terminal(nowrap);
        if (run_case(nowrap_terminal, nowrap_path, "nowrap_terminal") != 0 ||
            !nowrap.expected_copy_seen || !nowrap.single_copy_seen || nowrap.no_wrap_frames < 4)
            throw std::runtime_error("F2 no-wrap logical navigation lost source selection or wrap-back caret.");
        ScriptConsole cancelled_nowrap{};
        cancelled_nowrap.press(swiftedit::terminal_key::f2);
        cancelled_nowrap.press(swiftedit::terminal_key::escape);
        cancelled_nowrap.press('X', 0, true);
        swiftedit::Terminal cancelled_nowrap_terminal(cancelled_nowrap);
        if (run_case(cancelled_nowrap_terminal, nowrap_path, "cancelled_nowrap_terminal") != 0 ||
            cancelled_nowrap.no_wrap_frames)
            throw std::runtime_error("F2 no-wrap transition ignored cancellation before publication.");
        const std::filesystem::path wordwrap_path = directory / "word-wrap-read-only.txt";
        {
            std::ofstream output(wordwrap_path, std::ios::binary);
            output << "alpha " << std::string(78, 'b') << " end\r\n";
            for (std::size_t row = 0; row < 100; ++row)
                output << "row\r\n";
            output.seekp(swiftedit::editable_limit - 1);
            output.put('z');
        }
        ScriptConsole wordwrap{};
        wordwrap.expected_copy = "Copied 6 bytes";
        wordwrap.press(swiftedit::terminal_key::down, 0, false, true);
        wordwrap.press('C', 0, true);
        wordwrap.press(swiftedit::terminal_key::up);
        wordwrap.press(swiftedit::terminal_key::right, 0, false, true);
        wordwrap.press('C', 0, true);
        wordwrap.press('X', 0, true);
        swiftedit::Terminal wordwrap_terminal(wordwrap);
        if (run_case(wordwrap_terminal, wordwrap_path, "wordwrap_terminal") != 0 ||
            !wordwrap.expected_copy_seen || !wordwrap.single_copy_seen)
            throw std::runtime_error("Read-only word-wrap navigation did not follow the rendered source boundary.");
        ScriptConsole resized_nowrap{};
        resized_nowrap.wait_for_no_wrap = true;
        resized_nowrap.resize_after = 5;
        resized_nowrap.expected_no_wrap_cursor = "\x1b[2;40H";
        resized_nowrap.press(swiftedit::terminal_key::f2);
        resized_nowrap.press(swiftedit::terminal_key::right);
        resized_nowrap.inputs.back().repeats = 90;
        resized_nowrap.press(swiftedit::terminal_key::down);
        resized_nowrap.press(swiftedit::terminal_key::up);
        swiftedit::TerminalInput resize{};
        resize.resized = true;
        resized_nowrap.inputs.push_back(resize);
        resized_nowrap.press(swiftedit::terminal_key::left, 0, false, true);
        resized_nowrap.press('C', 0, true);
        resized_nowrap.press('X', 0, true);
        swiftedit::Terminal resized_nowrap_terminal(resized_nowrap);
        if (run_case(resized_nowrap_terminal, nowrap_path, "resized_nowrap_terminal") != 0 ||
            !resized_nowrap.no_wrap_cursor_seen || !resized_nowrap.single_copy_seen)
            throw std::runtime_error("No-wrap resize lost an off-screen source caret or atomic selection.");
        ScriptConsole page_nowrap{};
        page_nowrap.wait_for_no_wrap = true;
        page_nowrap.expected_copy = "Copied 297 bytes";
        page_nowrap.press(swiftedit::terminal_key::f2);
        page_nowrap.press(swiftedit::terminal_key::page_down, 0, false, true);
        page_nowrap.press('C', 0, true);
        page_nowrap.press(swiftedit::terminal_key::page_up);
        page_nowrap.press(swiftedit::terminal_key::right, 0, false, true);
        page_nowrap.press('C', 0, true);
        page_nowrap.press('X', 0, true);
        swiftedit::Terminal page_nowrap_terminal(page_nowrap);
        if (run_case(page_nowrap_terminal, nowrap_path, "page_nowrap_terminal") != 0 ||
            !page_nowrap.expected_copy_seen || !page_nowrap.single_copy_seen)
            throw std::runtime_error("No-wrap page navigation did not use logical lines or rewind accurately.");
        ScriptConsole cancelled_move{};
        cancelled_move.wait_for_no_wrap = true;
        cancelled_move.cancel_no_wrap_after = 3;
        cancelled_move.inspect_after = 4;
        cancelled_move.expected_cursor = "\x1b[2;2H";
        cancelled_move.press(swiftedit::terminal_key::f2);
        cancelled_move.press(swiftedit::terminal_key::right);
        cancelled_move.press(swiftedit::terminal_key::down);
        cancelled_move.press(swiftedit::terminal_key::escape);
        cancelled_move.press('X', 0, true);
        swiftedit::Terminal cancelled_move_terminal(cancelled_move);
        if (run_case(cancelled_move_terminal, nowrap_path, "cancelled_move_terminal") != 0 ||
            !cancelled_move.cursor_checked)
            throw std::runtime_error("Cancelled no-wrap movement changed the published caret.");
        ScriptConsole cancelled_resize{};
        cancelled_resize.wait_for_no_wrap = true;
        cancelled_resize.resize_after = 2;
        cancelled_resize.cancel_no_wrap_after = 2;
        cancelled_resize.press(swiftedit::terminal_key::f2);
        cancelled_resize.inputs.push_back(resize);
        cancelled_resize.press(swiftedit::terminal_key::escape);
        cancelled_resize.press('X', 0, true);
        swiftedit::Terminal cancelled_resize_terminal(cancelled_resize);
        if (run_case(cancelled_resize_terminal, nowrap_path, "cancelled_resize_terminal") != 0 ||
            cancelled_resize.no_wrap_requests != 2 || cancelled_resize.no_wrap_frames != 1)
            throw std::runtime_error("Cancelled resize restarted no-wrap preparation without a new command.");
        std::cout << "Shared terminal loop: Unicode, undo/redo, navigation, save, recovery and large paste passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
