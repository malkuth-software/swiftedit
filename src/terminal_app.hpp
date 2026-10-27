#pragma once
#include "terminal_row.hpp"
#include "terminal_wrap_view.hpp"
#include "terminal_page.hpp"
#include "terminal_reveal.hpp"
#include "terminal_search.hpp"
#include "terminal_query.hpp"
#include "date_time.hpp"
#include "session_word_count.hpp"
#include "session_copy.hpp"
#include "session_text_copy.hpp"
#include <algorithm>
#include <iostream>
#include "terminal_console.hpp"
#include "characters.hpp"
#include <stdexcept>

namespace swiftedit {
inline std::filesystem::path terminal_path(std::string_view text) {
    const std::u8string_view encoded(reinterpret_cast<const char8_t *>(text.data()), text.size());
    const std::filesystem::path result(encoded);
    return result;
}
inline void position(std::string &output, std::size_t row, std::size_t column) {
    output += "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(column + 1) + "H";
}
inline void status_line(std::string &output, std::size_t row, std::string_view source, std::size_t width) {
    position(output, row, 0);
    const std::string safe = swiftedit::escape_field(source);
    output.append(safe, 0, std::min(width, safe.size()));
}
enum class PageNavigation { end, previous, left, up };
enum class Prompt { none, save_path, text_copy_path, open_path, exit_choice, paste_choice, find, replacement };
class Terminal {
public:
    // The caller owns the OS console and must keep it alive through this
    // editor's lifetime. The host restores its original state on destruction.
    explicit Terminal(TerminalConsole &console) : console_(console) {}
#ifdef SWIFTEDIT_TERMINAL_SMOKE
    std::size_t search_hits() const { return search_hits_; }
    std::size_t replacement_count() const { return replacement_.count(); }
    std::size_t viewport_width() const { return width_; }
    bool wraps() const { return wrap_; }
    std::size_t wrap_cancellations() const { return wrap_cancellations_; }
    std::size_t text_copy_saves() const { return text_copy_saves_; }
    std::size_t completed_counts() const { return completed_counts_; }
    std::uint64_t last_word_count() const { return last_word_count_; }
    std::size_t cancelled_counts() const { return cancelled_counts_; }
    std::size_t completed_copies() const { return completed_copies_; }
    std::size_t cancelled_copies() const { return cancelled_copies_; }
    std::string_view clipboard_bytes() const { return clipboard_.bytes(); }
    bool document_dirty() const { return buffer_.session().dirty(); }
    bool wildcard_enabled() const {
        const swiftedit::SearchPattern pattern = query_.pattern();
        return pattern.slots()[0].wildcard;
    }
#endif
    void open(const std::filesystem::path &path) {
        search_.cancel();
        replacement_.cancel();
        counting_.reset();
        copying_.reset();
        ending_.reset();
        line_boundary_.reset();
        text_copy_.reset();
        page_anchor_ = 0;
        page_caret_ = 0;
        no_wrap_pending_.reset();
        no_wrap_move_.reset();
        no_wrap_visible_.reset();
        read_no_wrap_ = false;
        no_wrap_suspended_ = false;
        page_column_.reset();
        buffer_.open(path);
        pager_.reset(buffer_.session());
        top_ = 0;
        left_ = 0;
        status_ = "Opened";
    }
    int run(const std::filesystem::path &path) {
        if (!path.empty())
            open(path);
        console_.start();
        bool redraw = true;
        std::optional<TerminalInput> repeated_navigation{};
        while (!done_) {
            if (redraw) {
                try {
                    draw();
                } catch (const swiftedit::TerminalWrapInterrupt &cancelled) {
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                    ++wrap_cancellations_;
#endif
                    wrap_suspended_ = true;
                    status_ = cancelled.what();
                    draw();
                } catch (const std::exception &failure) {
                    status_ = failure.what();
                    std::string fallback = "\x1b[?25l\x1b[0m\x1b[2J";
                    status_line(fallback, 0, status_, width_);
                    status_line(fallback, 1,
                                "Display unavailable. Ctrl+S Save, Ctrl+X Exit; edits retained.",
                                width_);
                    console_.write(fallback);
                }
                redraw = false;
            }
            try {
                if (no_wrap_move_) {
                    if ((*no_wrap_move_).step(buffer_.session())) {
                        page_caret_ = (*no_wrap_move_).result(buffer_.session());
                        if (!no_wrap_extend_)
                            page_anchor_ = page_caret_;
                        begin_no_wrap_reveal();
                        no_wrap_move_.reset();
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (no_wrap_pending_) {
                    if ((*no_wrap_pending_).step(buffer_.session())) {
                        no_wrap_visible_ = std::move(no_wrap_pending_);
                        no_wrap_caret_ = page_caret_;
                        no_wrap_anchor_ = page_anchor_;
                        no_wrap_width_ = width_;
                        no_wrap_rows_ = rows_;
                        status_ = "No wrap";
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (text_copy_) {
                    if ((*text_copy_).state() == swiftedit::TextCopyState::publishing) {
                        const std::chrono::milliseconds wait(console_.input_ready() ? 0 : 8);
                        if ((*text_copy_).publication_ready(wait)) {
                            (*text_copy_).finish_publication();
                            text_copy_.reset();
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                            ++text_copy_saves_;
#endif
                            status_ = "Text copy saved; open document unchanged";
                            if (exit_after_publication_)
                                done_ = true;
                            redraw = true;
                            continue;
                        }
                        if (!console_.input_ready())
                            continue;
                    } else if ((*text_copy_).state() == swiftedit::TextCopyState::ready) {
                        if (!console_.input_ready()) {
                            (*text_copy_).begin_publication(buffer_.session());
                            status_ = "Publishing text copy... Ctrl+X exits after completion";
                            redraw = true;
                            continue;
                        }
                    } else {
                        const bool ready = (*text_copy_).step(buffer_.session());
                        if (ready)
                            continue;
                        if (!console_.input_ready())
                            continue;
                    }
                }
                if (line_boundary_) {
                    if ((*line_boundary_).step(buffer_.session())) {
                        const std::uint64_t target = (*line_boundary_).result(buffer_.session());
                        const swiftedit::TerminalPageFrame &frame =
                            pager_.frame(buffer_.session(), width_, rows_);
                        if (!swiftedit::terminal_page_caret(frame, target, width_, rows_))
                            pager_.reveal(buffer_.session(), target);
                        page_caret_ = target;
                        if (!line_extend_)
                            page_anchor_ = target;
                        line_boundary_.reset();
                        status_ = "Read-only line boundary reached";
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (ending_) {
                    if ((*ending_).step(buffer_.session())) {
                        std::optional<std::uint64_t> left_target{};
                        bool reveal_left = false;
                        if (page_navigation_ == PageNavigation::left) {
                            const swiftedit::TerminalPageCursor first = (*ending_).result(buffer_.session());
                            const swiftedit::TerminalPageFrame frame =
                                swiftedit::terminal_page(buffer_.session(), first, width_, rows_);
                            left_target = swiftedit::terminal_page_horizontal(frame, end_caret_, false);
                            if (!left_target)
                                throw std::runtime_error("Previous source grapheme could not be located.");
                            reveal_left = !swiftedit::terminal_page_caret(frame, *left_target, width_, rows_);
                        }
                        pager_.finish_end(buffer_.session(), *ending_);
                        if (reveal_left)
                            pager_.reveal(buffer_.session(), *left_target);
                        page_caret_ = page_navigation_ == PageNavigation::left ? *left_target
                                     : page_navigation_ == PageNavigation::previous ? pager_.source_offset() : buffer_.session().size();
                        if (page_navigation_ == PageNavigation::up) {
                            const swiftedit::TerminalPageFrame &frame = pager_.frame(buffer_.session(), width_, rows_);
                            const std::optional<std::uint64_t> target = swiftedit::terminal_page_target(
                                frame, 0, page_column_.value_or(0), width_, rows_);
                            page_caret_ = target.value_or(end_caret_);
                        }
                        if (page_navigation_ == PageNavigation::previous)
                            page_target(end_row_);
                        page_anchor_ = end_extend_ ? end_anchor_ : page_caret_;
                        ending_.reset();
                        status_ = page_navigation_ == PageNavigation::left || page_navigation_ == PageNavigation::up ? "Read-only caret moved"
                                  : page_navigation_ == PageNavigation::previous ? "Earlier read-only page" : "End of read-only document";
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (copying_) {
                    if ((*copying_).state() == swiftedit::CopyState::running)
                        (*copying_).step(buffer_.session(), swiftedit::maximum_page);
                    if ((*copying_).state() == swiftedit::CopyState::complete) {
                        clipboard_ = (*copying_).take();
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                        ++completed_copies_;
#endif
                        status_ = "Copied " + std::to_string(clipboard_.bytes().size()) + " bytes";
                        copying_.reset();
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (counting_) {
                    if ((*counting_).state() == swiftedit::WordCountState::running)
                        (*counting_).step(buffer_.session(), swiftedit::maximum_page);
                    if ((*counting_).state() == swiftedit::WordCountState::complete) {
                        const std::uint64_t words = (*counting_).result();
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                        ++completed_counts_;
                        last_word_count_ = words;
#endif
                        status_ = "Document: " + std::to_string(words) + " words";
                        counting_.reset();
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (replacement_.state() == swiftedit::TerminalReplaceState::pending) {
                    const swiftedit::TerminalReplaceState state = replacement_.step(buffer_);
                    if (state != swiftedit::TerminalReplaceState::pending) {
                        status_ = state == swiftedit::TerminalReplaceState::complete
                                      ? "Replaced " + std::to_string(replacement_.count()) +
                                            " matches; Ctrl+Z undoes"
                                      : "Replacement cancelled";
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
                if (search_.state() == swiftedit::TerminalSearchState::pending) {
                    const swiftedit::TerminalSearchState state = search_.step(buffer_);
                    if (state != swiftedit::TerminalSearchState::pending) {
                        if (state == swiftedit::TerminalSearchState::found && buffer_.session().read_only()) {
                            const swiftedit::PagedSearchMatch match = *search_.paged_match();
                            pager_.reveal(buffer_.session(), match.offset);
                            page_anchor_ = match.offset;
                            page_caret_ = match.offset + match.length;
                        }
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                        if (state == swiftedit::TerminalSearchState::found)
                            ++search_hits_;
#endif
                        status_ = state == swiftedit::TerminalSearchState::found ? "Found"
                                  : state == swiftedit::TerminalSearchState::not_found
                                      ? "Not found"
                                      : "Search cancelled";
                        redraw = true;
                        continue;
                    }
                    if (!console_.input_ready())
                        continue;
                }
            } catch (const std::exception &failure) {
                cancel_no_wrap_work();
                search_.cancel();
                replacement_.cancel();
                counting_.reset();
                copying_.reset();
                ending_.reset();
                line_boundary_.reset();
                text_copy_.reset();
                exit_after_publication_ = false;
                status_ = failure.what();
                redraw = true;
                continue;
            }
            TerminalInput input{};
            if (repeated_navigation && !ending_ && !line_boundary_ && !no_wrap_move_ && !no_wrap_pending_) {
                if (console_.cancel_requested()) {
                    repeated_navigation.reset();
                    input.key = terminal_key::escape;
                    input.pressed = true;
                } else {
                    input = std::move(*repeated_navigation);
                    repeated_navigation.reset();
                }
            } else {
                // A real queued event may cancel active reconstruction. It also
                // supersedes the unexecuted repetitions of that navigation.
                repeated_navigation.reset();
                input = console_.read();
            }
            if (!input.error.empty()) {
                high_surrogate_ = 0;
                status_ = input.error;
                redraw = true;
                continue;
            }
            if (input.resized) {
                cancel_no_wrap_work();
                no_wrap_suspended_ = false;
                if (line_boundary_) {
                    line_boundary_.reset();
                    status_ = "Navigation cancelled because the terminal resized";
                }
                if (ending_) {
                    ending_.reset();
                    status_ = "Navigation cancelled because the terminal resized";
                }
                redraw = true;
                continue;
            }
            if (!input.pressed && !input.pasted)
                continue;
            redraw = true;
            const TerminalInput &event = input;
            const std::size_t repeats = std::min<std::size_t>(event.repeats, 1000);
            const bool navigation = buffer_.session().read_only() && prompt_ == Prompt::none &&
                event.key >= terminal_key::left && event.key <= terminal_key::page_down;
            const std::size_t slice = navigation ? std::min<std::size_t>(repeats, 16) : repeats;
            std::size_t executed = 0;
            for (; executed < slice && !done_;) {
                ++executed;
                try {
                    key(event);
                } catch (const swiftedit::TerminalWrapInterrupt &cancelled) {
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                    ++wrap_cancellations_;
#endif
                    wrap_suspended_ = true;
                    status_ = cancelled.what();
                } catch (const std::exception &failure) {
                    status_ = failure.what();
                }
                if (navigation && (ending_ || line_boundary_ || no_wrap_move_ || no_wrap_pending_))
                    break;
            }
            if (navigation && executed < repeats && !done_) {
                repeated_navigation = event;
                (*repeated_navigation).repeats = repeats - executed;
            }
        }
        return 0;
    }

private:
    void cancel_no_wrap_work() {
        if (!no_wrap_pending_ && !no_wrap_move_)
            return;
        no_wrap_pending_.reset();
        no_wrap_move_.reset();
        no_wrap_suspended_ = true;
        if (no_wrap_visible_) {
            page_caret_ = no_wrap_caret_;
            page_anchor_ = no_wrap_anchor_;
        } else {
            read_no_wrap_ = false;
        }
    }
    void begin_no_wrap_reveal() {
        const std::uint64_t left = no_wrap_visible_ ? (*no_wrap_visible_).left(buffer_.session()) : 0;
        const std::optional<std::uint64_t> top = no_wrap_visible_
            ? std::optional<std::uint64_t>((*no_wrap_visible_).top(buffer_.session())) : std::nullopt;
        no_wrap_pending_ = std::make_unique<swiftedit::TerminalNoWrapReveal>(
            buffer_.session(), page_caret_, left, width_, rows_, top, no_wrap_visible_.get());
        no_wrap_suspended_ = false;
        status_ = "Preparing no-wrap view... Any key cancels";
    }
    void draw() {
        console_.allow_wrap_cancel(prompt_ == Prompt::none);
        const TerminalSize size = console_.size();
        const std::size_t old_width = width_;
        const std::size_t old_rows = rows_;
        width_ = std::min<std::size_t>(std::max<std::size_t>(size.columns, 1), 1000);
        const std::size_t height =
            std::min<std::size_t>(std::max<std::size_t>(size.rows, 1), 300);
        rows_ = height > 4 ? height - 4 : 1;
        if (width_ != old_width || rows_ != old_rows)
            page_column_.reset();
        std::string screen = "\x1b[?25l\x1b[0m\x1b[2J";
        const std::u8string path = buffer_.session().path().u8string();
        std::string title = "SwiftEdit  ";
        title.append(reinterpret_cast<const char *>(path.data()), path.size());
        if (path.empty())
            title += "Untitled";
        if (buffer_.session().dirty())
            title += " *";
        status_line(screen, 0, title, width_);
        if (height < 5 || width_ < 20) {
            status_line(screen, height - 1, "Enlarge terminal; Ctrl+X exits", width_);
            console_.write(screen);
            return;
        }
        std::optional<std::size_t> cursor_column{};
        std::size_t cursor_row = 1;
        if (buffer_.session().read_only() && read_no_wrap_) {
            if (!no_wrap_suspended_ && !no_wrap_pending_ && !no_wrap_move_ &&
                (!no_wrap_visible_ || no_wrap_caret_ != page_caret_ ||
                 no_wrap_width_ != width_ || no_wrap_rows_ != rows_))
                begin_no_wrap_reveal();
            if (no_wrap_visible_ && no_wrap_width_ == width_ && no_wrap_rows_ == rows_) {
                const swiftedit::TerminalHorizontalPage &page = (*no_wrap_visible_).viewport(buffer_.session());
                const std::vector<swiftedit::TerminalHorizontalFrame> &frames = page.result(buffer_.session());
                const swiftedit::TerminalPageCaret caret = (*no_wrap_visible_).caret(buffer_.session());
                cursor_column = caret.column;
                cursor_row = caret.row + 1;
                const bool pending = no_wrap_pending_ || no_wrap_move_;
                const std::uint64_t anchor = pending ? no_wrap_anchor_ : page_anchor_;
                const std::uint64_t selected_caret = pending ? no_wrap_caret_ : page_caret_;
                const std::uint64_t first = std::min(anchor, selected_caret);
                const std::uint64_t last = std::max(anchor, selected_caret);
                for (std::size_t row = 0; row < frames.size(); ++row) {
                    for (const swiftedit::TerminalPageRun &run : frames[row].runs) {
                        position(screen, row + 1, run.column);
                        const bool selected = run.source_offset < last &&
                            run.source_offset + run.source_length > first;
                        screen += selected ? "\x1b[7m" : "\x1b[0m";
                        screen += run.text;
                    }
                }
                if (!pending)
                    no_wrap_anchor_ = page_anchor_;
            }
        } else if (buffer_.session().read_only()) {
            const swiftedit::TerminalPageFrame &page =
                pager_.frame(buffer_.session(), width_, rows_);
            const std::optional<swiftedit::TerminalPageCaret> caret =
                swiftedit::terminal_page_caret(page, page_caret_, width_, rows_);
            if (caret) {
                cursor_column = (*caret).column;
                cursor_row = (*caret).row + 1;
            }
            for (const swiftedit::TerminalPageRun &run : page.runs) {
                position(screen, run.row + 1, run.column);
                const std::uint64_t first = std::min(page_anchor_, page_caret_);
                const std::uint64_t last = std::max(page_anchor_, page_caret_);
                const bool selected = run.source_offset < last &&
                    run.source_offset + run.source_length > first;
                if (selected)
                    screen += "\x1b[7m";
                screen += run.text;
                if (selected)
                    screen += "\x1b[0m";
            }
        } else if (wrap_ && wrap_suspended_) {
            // Leave the interrupted view unpublished until the next command.
            // Source, save/exit commands and the input loop remain live.
        } else if (wrap_) {
            const std::vector<swiftedit::TerminalWrappedRow> &wrapped =
                wrap_view_.frame(buffer_, width_, rows_);
            for (std::size_t row = 0; row < wrapped.size(); ++row) {
                const swiftedit::TerminalRow &visible = wrapped[row].display;
                for (const swiftedit::TerminalRun &run : visible.runs) {
                    position(screen, row + 1, run.column);
                    screen += run.selected ? "\x1b[7m" : "\x1b[0m";
                    screen += run.text;
                }
                if (visible.caret_column) {
                    cursor_column = visible.caret_column;
                    cursor_row = row + 1;
                }
            }
        } else {
            const std::size_t caret_line = buffer_.line_index();
            if (caret_line < top_)
                top_ = caret_line;
            if (caret_line >= top_ + rows_)
                top_ = caret_line - rows_ + 1;
            swiftedit::TerminalRow caret = row_cache_.row(buffer_, caret_line, left_, width_);
            if (!caret.caret_column) {
                // Reveal by source range, not byte-as-cell assumptions.
                const swiftedit::TerminalSelection selection = buffer_.selection();
                const std::size_t column =
                    row_cache_.source_column(buffer_, caret_line, selection.caret);
                left_ = column < left_ ? column : column >= width_ ? column - width_ + 1 : 0;
            }
            for (std::size_t row = 0; row < rows_ && top_ + row < buffer_.line_count(); ++row) {
                const swiftedit::TerminalRow visible =
                    row_cache_.row(buffer_, top_ + row, left_, width_);
                for (const swiftedit::TerminalRun &run : visible.runs) {
                    position(screen, row + 1, run.column);
                    screen += run.selected ? "\x1b[7m" : "\x1b[0m";
                    screen += run.text;
                }
                if (visible.caret_column) {
                    cursor_column = visible.caret_column;
                    cursor_row = row + 1;
                }
            }
        }
        screen += "\x1b[0m";
        std::string message = status_;
        if (prompt_ == Prompt::save_path)
            message = "Save new file: " + input_ + "  [Enter / Esc]";
        if (prompt_ == Prompt::text_copy_path)
            message = "Save Text Copy: " + input_ + "  [Enter / Esc]";
        if (prompt_ == Prompt::open_path)
            message = "Open file: " + input_ + "  [Enter / Esc]";
        if (prompt_ == Prompt::find)
            message = "Find: " + query_.display() + "  [Ctrl+? wildcard, Enter / Esc]";
        if (prompt_ == Prompt::replacement)
            message = "Replace ALL with: " + input_ + "  [Enter applies / Esc cancels]";
        if (prompt_ == Prompt::exit_choice)
            message = "Save changes before exit? Y=Save N=Discard Esc=Cancel";
        if (prompt_ == Prompt::paste_choice)
            message = "Paste " + std::to_string((*pending_paste_).text.size()) +
                      " bytes? Y=Paste N=Cancel Esc=Cancel";
        status_line(screen, height - 3, message, width_);
        if (prompt_ != Prompt::none && !status_.empty())
            status_line(screen, height - 2, status_, width_);
        else
            status_line(
                screen, height - 2,
                "^S Save  ^W Find  F3 Next  ^H Replace All  ^R Open  ^X Exit  ^Z Undo  ^Y Redo",
                width_);
        if (buffer_.session().read_only())
            status_line(screen, height - 1,
                        "Read-only: Shift+arrows/Pg Select  ^A All  ^C Copy  F6 Words  F2 Wrap",
                        width_);
        else
            status_line(
                screen, height - 1,
                "F6 Words  ^C Copy  ^K Cut  ^U Paste  ^T Text Copy  F2 Wrap  Shift+arrows Select",
                width_);
        if (prompt_ == Prompt::none && cursor_column) {
            position(screen, cursor_row, *cursor_column);
            screen += "\x1b[?25h";
        }
        console_.write(screen);
    }
    void save() {
        if (buffer_.session().path().empty()) {
            prompt_ = Prompt::save_path;
            status_.clear();
            input_ = swiftedit::suggested_name(buffer_.session().text());
            return;
        }
        buffer_.save();
        status_ = "Saved";
        if (exit_after_save_)
            done_ = true;
    }
    void text_copy() {
        const std::filesystem::path source =
            buffer_.session().path().empty()
                ? terminal_path(swiftedit::suggested_name(buffer_.session().text()))
                : buffer_.session().path();
        const std::u8string name = swiftedit::versioned_name(source).u8string();
        std::string prepared(reinterpret_cast<const char *>(name.data()), name.size());
        input_ = std::move(prepared);
        exit_after_save_ = false;
        prompt_ = Prompt::text_copy_path;
        status_ = "Each illegal byte becomes a space. Source unchanged. New file only.";
    }
    void character(char16_t value) {
        if (!value)
            return;
        if (value >= 0xd800 && value <= 0xdbff) {
            high_surrogate_ = value;
            return;
        }
        char32_t scalar = value;
        const char16_t high = high_surrogate_;
        high_surrogate_ = 0;
        if (high) {
            if (value < 0xdc00 || value > 0xdfff)
                throw std::runtime_error("Incomplete Unicode keyboard input.");
            scalar = 0x10000 + (static_cast<char32_t>(high) - 0xd800) * 0x400 +
                     (static_cast<char32_t>(value) - 0xdc00);
        }
        const std::string text = swiftedit::character_utf8(scalar);
        if (prompt_ == Prompt::find) {
            query_.insert(text);
        } else if (prompt_ == Prompt::save_path || prompt_ == Prompt::text_copy_path ||
                   prompt_ == Prompt::open_path || prompt_ == Prompt::replacement) {
            const std::size_t limit = 32768;
            if (input_.size() + text.size() > limit)
                throw std::runtime_error("Prompt input exceeds its UTF-8 byte limit.");
            input_ += text;
        } else
            buffer_.insert(text);
    }
    void move(swiftedit::TerminalMotion motion, bool extend, std::size_t count = 1) {
        if (wrap_)
            wrap_view_.move(buffer_, motion, extend, count, width_);
        else
            buffer_.move(motion, extend, count);
    }
    void paste(const std::string_view text) {
        if (buffer_.session().read_only())
            throw std::runtime_error("Large-file pages are read-only.");
        const swiftedit::SourceRange selection = buffer_.selected_range();
        const std::uint64_t retained = buffer_.session().size() - selection.length;
        if (text.size() >= swiftedit::editable_limit - retained)
            throw std::runtime_error("Paste exceeds the editable document limit. Document unchanged.");
        if (text.size() <= swiftedit::paste_confirmation_bytes) {
            buffer_.insert(text);
            return;
        }
        PendingPaste prepared{std::string(text), buffer_.session().stamp(), buffer_.selection()};
        pending_paste_ = std::move(prepared);
        prompt_ = Prompt::paste_choice;
        status_ = "Large paste requires confirmation; document unchanged.";
    }
    void confirm_paste() {
        PendingPaste prepared = std::move(*pending_paste_);
        pending_paste_.reset();
        prompt_ = Prompt::none;
        const swiftedit::DocumentStamp stamp = buffer_.session().stamp();
        const swiftedit::TerminalSelection selection = buffer_.selection();
        if (stamp.identity != prepared.stamp.identity || stamp.revision != prepared.stamp.revision ||
            selection.anchor != prepared.selection.anchor || selection.caret != prepared.selection.caret)
            throw std::runtime_error("Paste cancelled because the document or selection changed.");
        buffer_.insert(prepared.text);
        status_ = "Pasted as one edit";
    }
    void page_target(std::size_t row) {
        const swiftedit::TerminalPageFrame &frame = pager_.frame(buffer_.session(), width_, rows_);
        for (;;) {
            const std::optional<std::uint64_t> target = swiftedit::terminal_page_target(
                frame, row, page_column_.value_or(0), width_, rows_);
            if (target) {
                page_caret_ = *target;
                return;
            }
            if (!row)
                return;
            --row;
        }
    }
    void page_jump(const bool down, const bool extend) {
        const swiftedit::TerminalPageFrame &initial = pager_.frame(buffer_.session(), width_, rows_);
        std::optional<swiftedit::TerminalPageCaret> position =
            swiftedit::terminal_page_caret(initial, page_caret_, width_, rows_);
        if (!position) {
            pager_.reveal(buffer_.session(), page_caret_);
            const swiftedit::TerminalPageFrame &visible = pager_.frame(buffer_.session(), width_, rows_);
            position = swiftedit::terminal_page_caret(visible, page_caret_, width_, rows_);
        }
        if (!position)
            throw std::runtime_error("Read-only caret has no visible source boundary.");
        if (!page_column_)
            page_column_ = (*position).column;
        std::size_t target_row = (*position).row;
        if (!down) {
            ending_ = pager_.prepare_previous(buffer_.session(), true);
            if (ending_) {
                page_navigation_ = PageNavigation::previous;
                end_extend_ = extend;
                end_anchor_ = page_anchor_;
                end_caret_ = page_caret_;
                end_row_ = target_row;
                status_ = "Finding earlier page... Any key cancels";
                return;
            }
        }
        const std::uint64_t before = pager_.source_offset();
        if (down)
            pager_.next();
        else
            pager_.previous();
        if (pager_.source_offset() == before)
            target_row = down ? rows_ - 1 : 0;
        page_target(target_row);
        if (!extend)
            page_anchor_ = page_caret_;
        status_ = "Read-only page moved";
    }
    void page_vertical(const bool down, const bool extend) {
        const swiftedit::TerminalPageFrame &initial = pager_.frame(buffer_.session(), width_, rows_);
        std::optional<swiftedit::TerminalPageCaret> position =
            swiftedit::terminal_page_caret(initial, page_caret_, width_, rows_);
        if (!position) {
            pager_.reveal(buffer_.session(), page_caret_);
            const swiftedit::TerminalPageFrame &visible = pager_.frame(buffer_.session(), width_, rows_);
            position = swiftedit::terminal_page_caret(visible, page_caret_, width_, rows_);
        }
        if (!position)
            throw std::runtime_error("Read-only caret has no visible source boundary.");
        if (!page_column_)
            page_column_ = (*position).column;
        std::size_t target_row = (*position).row;
        if (down) {
            if (target_row + 1 < rows_)
                ++target_row;
            else
                pager_.down();
        } else if (target_row) {
            --target_row;
        } else {
            ending_ = pager_.prepare_previous(buffer_.session(), false);
            if (ending_) {
                page_navigation_ = PageNavigation::up;
                end_extend_ = extend;
                end_anchor_ = page_anchor_;
                end_caret_ = page_caret_;
                status_ = "Finding previous grapheme... Any key cancels";
                return;
            }
            pager_.up();
        }
        const swiftedit::TerminalPageFrame &frame = pager_.frame(buffer_.session(), width_, rows_);
        const std::optional<std::uint64_t> target = swiftedit::terminal_page_target(
            frame, target_row, *page_column_, width_, rows_);
        if (target)
            page_caret_ = *target;
        if (!extend)
            page_anchor_ = page_caret_;
        status_ = "Read-only caret moved";
    }
    void page_horizontal(const bool right, const bool extend) {
        std::optional<std::uint64_t> target{};
        if (!extend && page_anchor_ != page_caret_)
            target = right ? std::max(page_anchor_, page_caret_) : std::min(page_anchor_, page_caret_);
        else if ((right && page_caret_ == buffer_.session().size()) || (!right && !page_caret_))
            target = page_caret_;
        else {
            const swiftedit::TerminalPageFrame &frame = pager_.frame(buffer_.session(), width_, rows_);
            target = swiftedit::terminal_page_horizontal(frame, page_caret_, right);
            if (!target && right) {
                const swiftedit::TerminalPageFrame next =
                    swiftedit::terminal_page(buffer_.session(), {page_caret_, 0, false}, width_, rows_);
                target = swiftedit::terminal_page_horizontal(next, page_caret_, true);
                if (!target)
                    throw std::runtime_error("Next source grapheme could not be located.");
            } else if (!target) {
                ending_ = std::make_unique<swiftedit::TerminalPageEnd>(
                    buffer_.session(), width_, 1, swiftedit::TerminalPageCursor{page_caret_, 0, false});
                page_navigation_ = PageNavigation::left;
                end_extend_ = extend;
                end_anchor_ = page_anchor_;
                end_caret_ = page_caret_;
                status_ = "Finding previous grapheme... Any key cancels";
                return;
            }
        }
        const swiftedit::TerminalPageFrame &frame = pager_.frame(buffer_.session(), width_, rows_);
        if (!swiftedit::terminal_page_caret(frame, *target, width_, rows_))
            pager_.reveal(buffer_.session(), *target);
        page_caret_ = *target;
        if (!extend)
            page_anchor_ = page_caret_;
        status_ = "Read-only caret moved";
    }
    void key(const TerminalInput &event) {
        const bool ctrl = event.control;
        const bool alt = event.alt;
        const bool shift = event.shift;
        const std::uint32_t key = event.key;
        if (no_wrap_pending_ || no_wrap_move_) {
            cancel_no_wrap_work();
            status_ = "No-wrap navigation cancelled";
            if (key == terminal_key::escape)
                return;
        }
        if (key != terminal_key::up && key != terminal_key::down &&
            key != terminal_key::page_up && key != terminal_key::page_down)
            page_column_.reset();
        if (text_copy_ && (*text_copy_).state() == swiftedit::TextCopyState::publishing) {
            if (ctrl && !alt && key == 'X')
                exit_after_publication_ = true;
            status_ = exit_after_publication_ ? "Finishing text copy before exit..."
                                             : "Text copy publication has started; waiting for completion";
            return;
        }
        if (text_copy_) {
            text_copy_.reset();
            status_ = "Text copy cancelled; destination unchanged";
            if (key == terminal_key::escape)
                return;
        }
        if (line_boundary_) {
            line_boundary_.reset();
            status_ = "Read-only navigation cancelled";
            if (key == terminal_key::escape)
                return;
        }
        if (ending_) {
            ending_.reset();
            status_ = "Read-only navigation cancelled";
            if (key == terminal_key::escape)
                return;
        }
        const bool preserving_command =
            ctrl && !alt && (key == 'X' || key == 'S' || key == 'R' || key == 'T');
        if (key != terminal_key::escape && prompt_ == Prompt::none && !preserving_command) {
            wrap_suspended_ = false;
            no_wrap_suspended_ = false;
        }
        if (key != 0 && event.text_unit < 32)
            high_surrogate_ = 0;
        if (copying_) {
            (*copying_).cancel();
            copying_.reset();
#ifdef SWIFTEDIT_TERMINAL_SMOKE
            ++cancelled_copies_;
#endif
            status_ = "Copy cancelled; previous clipboard retained";
            if (key == terminal_key::escape)
                return;
        }
        if (counting_) {
            (*counting_).cancel();
            counting_.reset();
#ifdef SWIFTEDIT_TERMINAL_SMOKE
            ++cancelled_counts_;
#endif
            status_ = "Word count cancelled";
            if (key == terminal_key::escape)
                return;
        }
        if (replacement_.state() == swiftedit::TerminalReplaceState::pending) {
            replacement_.cancel();
            status_ = "Replacement cancelled";
            if (key == terminal_key::escape)
                return;
        }
        if (search_.state() == swiftedit::TerminalSearchState::pending) {
            search_.cancel();
            status_ = "Search cancelled";
            if (key == terminal_key::escape)
                return;
        }
        if (event.pasted) {
            high_surrogate_ = 0;
            if (prompt_ == Prompt::none)
                paste(event.paste);
            else if (prompt_ == Prompt::find)
                query_.insert(event.paste);
            else if (prompt_ == Prompt::exit_choice || prompt_ == Prompt::paste_choice)
                status_ = "Type Y or N for this choice; pasted text is not a command.";
            else {
                if (event.paste.size() > 32768 - input_.size())
                    throw std::runtime_error("Prompt input exceeds its UTF-8 byte limit.");
                input_ += event.paste;
            }
            return;
        }
        if (prompt_ != Prompt::none) {
            if (key == terminal_key::escape) {
                pending_paste_.reset();
                prompt_ = Prompt::none;
                exit_after_save_ = false;
                high_surrogate_ = 0;
                status_ = "Cancelled";
            } else if (prompt_ == Prompt::paste_choice) {
                if (!ctrl && !alt && key == 'Y')
                    confirm_paste();
                else if (!ctrl && !alt && key == 'N') {
                    pending_paste_.reset();
                    prompt_ = Prompt::none;
                    status_ = "Paste cancelled; document unchanged";
                }
            } else if (prompt_ == Prompt::exit_choice) {
                if (key == 'N')
                    done_ = true;
                else if (key == 'Y') {
                    prompt_ = Prompt::none;
                    exit_after_save_ = true;
                    save();
                }
            } else if (key == terminal_key::enter && prompt_ == Prompt::find) {
                swiftedit::SearchPattern pattern = query_.pattern();
                if (replace_query_) {
                    prompt_ = Prompt::replacement;
                    input_.clear();
                    status_.clear();
                } else {
                    search_.begin(buffer_, std::move(pattern), false, page_caret_);
                    prompt_ = Prompt::none;
                    status_ = "Searching... Escape cancels";
                }
            } else if (key == terminal_key::enter && prompt_ == Prompt::replacement) {
                swiftedit::SearchPattern pattern = query_.pattern();
                replacement_.begin(buffer_, std::move(pattern), input_);
                prompt_ = Prompt::none;
                status_ = "Preparing Replace All... Escape cancels before publication";
            } else if (prompt_ == Prompt::find && key == terminal_key::left) {
                query_.move(false);
            } else if (prompt_ == Prompt::find && key == terminal_key::right) {
                query_.move(true);
            } else if (prompt_ == Prompt::find && key == terminal_key::home) {
                query_.home();
            } else if (prompt_ == Prompt::find && key == terminal_key::end) {
                query_.end();
            } else if (prompt_ == Prompt::find && key == terminal_key::question && ctrl && shift) {
                query_.toggle();
            } else if (prompt_ == Prompt::find && (key == terminal_key::backspace || key == terminal_key::erase)) {
                query_.erase(key == terminal_key::backspace);
            } else if (key == terminal_key::enter) {
                if (input_.empty())
                    throw std::runtime_error("Enter a filename or press Escape.");
                const std::filesystem::path path = terminal_path(input_);
                if (prompt_ == Prompt::save_path) {
                    buffer_.save_as(path);
                    status_ = "Saved";
                    if (exit_after_save_)
                        done_ = true;
                } else if (prompt_ == Prompt::text_copy_path) {
                    if (buffer_.session().read_only()) {
                        text_copy_ = std::make_unique<swiftedit::SessionTextCopy>(buffer_.session(), path);
                        status_ = "Saving text copy... Any key cancels";
                    } else {
                        buffer_.save_text_copy(path);
#ifdef SWIFTEDIT_TERMINAL_SMOKE
                        ++text_copy_saves_;
#endif
                        status_ = "Text copy saved; open document unchanged";
                    }
                } else
                    open(path);
                prompt_ = Prompt::none;
            } else if (key == terminal_key::backspace) {
                if (!input_.empty()) {
                    const gui_forms::TextStore text(input_);
                    const gui_forms::Utf8Offset previous =
                        text.previous_grapheme_boundary(text.utf8_size());
                    input_.resize(previous.value());
                }
            } else if ((!ctrl || alt) && event.text_unit >= 32)
                character(event.text_unit);
            return;
        }
        if (buffer_.session().read_only()) {
            if (read_no_wrap_ && !ctrl && !alt &&
                (key == terminal_key::up || key == terminal_key::down ||
                 key == terminal_key::page_up || key == terminal_key::page_down)) {
                const swiftedit::TerminalPageCaret caret = (*no_wrap_visible_).caret(buffer_.session());
                if (!page_column_)
                    page_column_ = static_cast<std::size_t>((*no_wrap_visible_).left(buffer_.session()) + caret.column);
                const bool down = key == terminal_key::down || key == terminal_key::page_down;
                const std::size_t count = key == terminal_key::page_up || key == terminal_key::page_down ? rows_ : 1;
                no_wrap_move_ = std::make_unique<swiftedit::TerminalNoWrapMove>(
                    buffer_.session(), page_caret_, *page_column_, down, count);
                no_wrap_extend_ = shift;
                status_ = "Moving by logical lines... Any key cancels";
                return;
            }
            if (!ctrl && !alt && (key == terminal_key::page_up || key == terminal_key::page_down)) {
                page_jump(key == terminal_key::page_down, shift);
                return;
            }
            if (!ctrl && !alt && (key == terminal_key::up || key == terminal_key::down)) {
                page_vertical(key == terminal_key::down, shift);
                return;
            }
            if (!ctrl && !alt && (key == terminal_key::home || key == terminal_key::end)) {
                line_boundary_ = std::make_unique<swiftedit::TerminalLineBoundary>(
                    buffer_.session(), page_caret_, key == terminal_key::end);
                line_extend_ = shift;
                status_ = "Finding line boundary... Any key cancels";
                return;
            }
            if (!ctrl && !alt && (key == terminal_key::left || key == terminal_key::right)) {
                page_horizontal(key == terminal_key::right, shift);
                return;
            }
            if (ctrl && !alt && key == terminal_key::end) {
                ending_ = std::make_unique<swiftedit::TerminalPageEnd>(buffer_.session(), width_, rows_);
                page_navigation_ = PageNavigation::end;
                end_extend_ = shift;
                end_anchor_ = page_anchor_;
                status_ = "Finding final page... Any key cancels";
                return;
            }
            if (ctrl && !alt && key == 'A') {
                page_anchor_ = 0;
                page_caret_ = buffer_.session().size();
                status_ = "Selected entire read-only document";
                return;
            }
            if (ctrl && !alt && key == 'C') {
                const std::uint64_t first = std::min(page_anchor_, page_caret_);
                const std::uint64_t last = std::max(page_anchor_, page_caret_);
                if (first == last)
                    throw std::runtime_error("Select rows with Shift+navigation or Ctrl+A first.");
                copying_ = std::make_unique<swiftedit::SessionCopy>(buffer_.session(), first,
                                                                  last - first);
                status_ = "Copying selection... Any key cancels";
                return;
            }
            if (key == terminal_key::down || key == terminal_key::up || key == terminal_key::page_down || key == terminal_key::page_up ||
                (key == terminal_key::home && ctrl)) {
                const std::uint64_t before = page_caret_;
                if (key == terminal_key::up || key == terminal_key::page_up) {
                    ending_ = pager_.prepare_previous(buffer_.session(), key == terminal_key::page_up);
                    if (ending_) {
                        page_navigation_ = PageNavigation::previous;
                        end_row_ = 0;
                        end_extend_ = shift;
                        end_anchor_ = page_anchor_ == page_caret_ ? before : page_anchor_;
                        status_ = "Finding earlier page... Any key cancels";
                        return;
                    }
                }
                if (key == terminal_key::down)
                    pager_.down();
                else if (key == terminal_key::up)
                    pager_.up();
                else if (key == terminal_key::page_down)
                    pager_.next();
                else if (key == terminal_key::page_up)
                    pager_.previous();
                else
                    pager_.first();
                if (!shift || page_anchor_ == page_caret_)
                    page_anchor_ = shift ? before : pager_.source_offset();
                page_caret_ = pager_.source_offset();
                const std::uint64_t selected = std::max(page_anchor_, page_caret_) -
                                               std::min(page_anchor_, page_caret_);
                status_ = shift ? "Selected " + std::to_string(selected) + " source bytes"
                                : "Read-only navigation";
                return;
            }
        }
        if (ctrl && !alt) {
            switch (key) {
            case 'A':
                buffer_.select_all();
                return;
            case 'T':
                text_copy();
                return;
            case 'H':
                replace_query_ = true;
                prompt_ = Prompt::find;
                query_.end();
                status_.clear();
                return;
            case 'W':
                replace_query_ = false;
                prompt_ = Prompt::find;
                query_.end();
                status_.clear();
                return;
            case 'X':
                if (buffer_.session().dirty())
                    prompt_ = Prompt::exit_choice;
                else
                    done_ = true;
                return;
            case 'S':
                exit_after_save_ = false;
                save();
                return;
            case 'R':
                if (buffer_.session().dirty())
                    throw std::runtime_error("Save changes before opening another file.");
                prompt_ = Prompt::open_path;
                status_.clear();
                input_.clear();
                return;
            case 'Z':
                static_cast<void>(buffer_.undo());
                return;
            case 'Y':
                static_cast<void>(buffer_.redo());
                return;
            case 'C':
                clipboard_ = buffer_.copy();
                status_ = "Copied selection";
                return;
            case 'K':
                clipboard_ = buffer_.cut();
                status_ = "Cut to terminal clipboard";
                return;
            case 'U':
                paste(clipboard_.bytes());
                return;
            }
        }
        switch (key) {
        case terminal_key::f2:
            if (buffer_.session().read_only()) {
                if (!read_no_wrap_) {
                    begin_no_wrap_reveal();
                    read_no_wrap_ = true;
                } else {
                    read_no_wrap_ = false;
                    no_wrap_visible_.reset();
                    pager_.reveal(buffer_.session(), page_caret_);
                    status_ = "Wrap to window";
                }
            } else {
                wrap_ = !wrap_;
                wrap_view_ = swiftedit::TerminalWrapView{&console_};
                status_ = wrap_ ? "Wrap to window" : "No wrap";
            }
            break;
        case terminal_key::left:
            move(swiftedit::TerminalMotion::left, shift);
            break;
        case terminal_key::right:
            move(swiftedit::TerminalMotion::right, shift);
            break;
        case terminal_key::up:
            move(swiftedit::TerminalMotion::up, shift);
            break;
        case terminal_key::down:
            move(swiftedit::TerminalMotion::down, shift);
            break;
        case terminal_key::home:
            move(ctrl ? swiftedit::TerminalMotion::document_start : swiftedit::TerminalMotion::home,
                 shift);
            break;
        case terminal_key::end:
            move(ctrl ? swiftedit::TerminalMotion::document_end : swiftedit::TerminalMotion::end,
                 shift);
            break;
        case terminal_key::page_up:
            move(swiftedit::TerminalMotion::up, shift, rows_);
            break;
        case terminal_key::page_down:
            move(swiftedit::TerminalMotion::down, shift, rows_);
            break;
        case terminal_key::backspace:
            buffer_.erase(true);
            break;
        case terminal_key::erase:
            buffer_.erase(false);
            break;
        case terminal_key::enter:
            buffer_.enter();
            break;
        case terminal_key::tab:
            buffer_.insert("\t");
            break;
        case terminal_key::f5: {
            const std::string timestamp = notepad::current_date_time();
            buffer_.insert(timestamp);
            break;
        }
        case terminal_key::f6:
            counting_ = std::make_unique<swiftedit::SessionWordCount>(buffer_.session());
            status_ = "Counting document words... Any key cancels";
            break;
        case terminal_key::f3: {
            swiftedit::SearchPattern pattern = query_.pattern();
            search_.begin(buffer_, std::move(pattern), false, page_caret_);
            status_ = "Searching... Escape cancels";
            break;
        }
        case terminal_key::f1:
            status_ = "Ctrl+S saves; Ctrl+X asks before discarding. Clipboard is private to this "
                      "terminal.";
            break;
        default:
            if ((!ctrl || alt) && event.text_unit >= 32)
                character(event.text_unit);
            break;
        }
    }
#ifdef SWIFTEDIT_TERMINAL_SMOKE
    std::size_t search_hits_{};
    std::size_t wrap_cancellations_{};
    std::size_t text_copy_saves_{};
    std::size_t completed_counts_{}, cancelled_counts_{};
    std::size_t completed_copies_{}, cancelled_copies_{};
    std::uint64_t last_word_count_{};
#endif
    TerminalConsole &console_;
    swiftedit::TerminalBuffer buffer_{};
    swiftedit::TerminalRowCache row_cache_{};
    swiftedit::TerminalWrapView wrap_view_{&console_};
    bool wrap_{}, wrap_suspended_{};
    swiftedit::TerminalPager pager_{};
    bool read_no_wrap_{}, no_wrap_extend_{}, no_wrap_suspended_{};
    std::unique_ptr<swiftedit::TerminalNoWrapReveal> no_wrap_pending_{}, no_wrap_visible_{};
    std::unique_ptr<swiftedit::TerminalNoWrapMove> no_wrap_move_{};
    std::uint64_t no_wrap_caret_{}, no_wrap_anchor_{};
    std::size_t no_wrap_width_{}, no_wrap_rows_{};
    swiftedit::TerminalSearch search_{};
    swiftedit::TerminalReplace replacement_{};
    std::unique_ptr<swiftedit::SessionWordCount> counting_{};
    std::unique_ptr<swiftedit::SessionCopy> copying_{};
    std::unique_ptr<swiftedit::TerminalPageEnd> ending_{};
    std::unique_ptr<swiftedit::SessionTextCopy> text_copy_{};
    bool exit_after_publication_{};
    std::unique_ptr<swiftedit::TerminalLineBoundary> line_boundary_{};
    bool line_extend_{};
    bool end_extend_{};
    PageNavigation page_navigation_{PageNavigation::end};
    std::uint64_t end_anchor_{}, end_caret_{};
    std::uint64_t page_anchor_{}, page_caret_{};
    std::optional<std::size_t> page_column_{};
    std::size_t end_row_{};
    bool replace_query_{};
    swiftedit::TerminalQuery query_{};
    Prompt prompt_{Prompt::none};
    struct PendingPaste {
        std::string text{};
        swiftedit::DocumentStamp stamp{};
        swiftedit::TerminalSelection selection{};
    };
    std::optional<PendingPaste> pending_paste_{};
    std::string input_{}, status_{"F1 Help"};
    swiftedit::SourceClipboard clipboard_{};
    std::size_t top_{}, left_{}, width_{80}, rows_{20};
    char16_t high_surrogate_{};
    bool done_{}, exit_after_save_{};
};
} // namespace swiftedit
