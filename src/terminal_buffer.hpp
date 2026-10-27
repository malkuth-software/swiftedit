#pragma once
#include "session.hpp"
#include "session_replace.hpp"
#include <gui_forms/text.hpp>
#include <optional>

namespace swiftedit {
enum class TerminalMotion { left, right, up, down, home, end, document_start, document_end };
struct TerminalSelection {
    std::size_t anchor{}, caret{};
};
// Terminal editing authority; all mutations go through the shared Session.
// Unicode navigation metadata is lazy and revision-bound, never file content.
class TerminalBuffer {
public:
    void open(const std::filesystem::path &);
    void reset(bool discard = false);
    const Session &session() const { return session_; }
    TerminalSelection selection();
    SourceRange selected_range();
    std::string selected_text();
    void move(TerminalMotion, bool extend = false, std::size_t rows = 1);
    // Apply a layout-derived source position only to the observed document.
    // Invalid/stale positions are never applied; normal lazy synchronization
    // may first repair an existing selection after an edit. Extend retains
    // the current anchor, including a backwards selection.
    void move_to(std::size_t source_offset, DocumentStamp, bool extend = false);
    void select_all();
    void select_range(SourceRange, DocumentStamp);
    void insert(std::string_view);
    std::unique_ptr<SessionReplacement> prepare_replacement(SearchPattern, std::string, bool);
    std::size_t commit_replacement(SessionReplacement &);
    void enter();
    void erase(bool backward);
    SourceClipboard copy();
    SourceClipboard cut();
    void paste(const SourceClipboard &);
    bool undo();
    bool redo();
    void save();
    void save_as(const std::filesystem::path &);
    void save_text_copy(const std::filesystem::path &) const;
    std::size_t line_index();
    SourceRange line_range(std::size_t);
    std::size_t line_count();
    SourceRange grapheme_range(std::size_t byte_offset);

private:
    void synchronize();
    std::size_t line_at(std::size_t) const;
    std::size_t snap(std::size_t) const;
    Session session_{};
    std::unique_ptr<gui_forms::TextStore> navigation_{};
    DocumentStamp navigation_stamp_{};
    TerminalSelection selection_{};
    std::optional<std::size_t> desired_column_{};
    std::string newline_{notepad::native_newline()};
};
} // namespace swiftedit
