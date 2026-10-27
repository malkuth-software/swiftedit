#pragma once
#include "session.hpp"
#include <gui_forms/document_view.hpp>

namespace swiftedit {
struct DisplaySelection {
    gui_forms::DisplayByteOffset begin{};
    gui_forms::DisplayByteOffset end{};
};
// Interactive selection policy for editable sessions. Owns ordered ranges and
// a document stamp, never a borrowed Session or generated display labels.
// Character lengths are source graphemes; malformed bytes each count as one.
// Copy retains separate parts, leaving clipboard joining policy to the view.
class SelectionSet final {
public:
    SelectionSet(const Session &, const std::vector<SourceRange> &);
    // Positions belong to one current public view page. Mapping does not grant
    // grapheme legality: the source constructor performs that validation too.
    // Borrows end on return; retained selections own only source ranges/stamp.
    SelectionSet(const Session &, const gui_forms::DocumentViewState &,
                 const gui_forms::DocumentPageRequest &, const std::vector<DisplaySelection> &);
    [[nodiscard]] bool can_rewrite() const { return equal_lengths_; }
    [[nodiscard]] const std::vector<SourceRange> &ranges() const { return ranges_; }
    [[nodiscard]] std::vector<SourceClipboard> copy(const Session &) const;
    void rewrite(Session &, const std::string_view replacement) const;

private:
    void require_current(const Session &) const;
    DocumentStamp stamp_{};
    std::vector<SourceRange> ranges_{};
    bool equal_lengths_{true};
};
} // namespace swiftedit
