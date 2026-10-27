#pragma once
#include "search.hpp"
#include <gui_forms/gui_forms.hpp>
#include <deque>

namespace notepad {
namespace gf = gui_forms;
// The private child supplies standard text editing/clipboard behavior. This
// control owns focus, hit testing and the visible, flagged-grapheme rendering.
// It also owns 64 query-history snapshots (4096 UTF-8 bytes each), so flags and
// selection are restored with text instead of relying on text-only child undo.
class QueryField final : public gf::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit QueryField(gf::StableId);
    void initialize_control_tree();
    void set_text(std::string);
    std::string_view text() const;
    swiftedit::SearchPattern pattern() const;
    std::uint64_t revision() const { return revision_; }
    void select(gf::Utf8Offset, gf::Utf8Offset);
    void toggle_slot(std::size_t);
    void on_paint(gf::Painter &, gf::Rect) override;
    void on_pointer(gf::PointerEvent &) override;
    void on_key(gf::KeyEvent &) override;
    void on_text_input(gf::TextInputEvent &) override;
    void on_focus_changed(bool) override;

protected:
    void on_dispose() noexcept override;

private:
    struct Snapshot {
        std::string text{};
        std::vector<bool> flags{};
        gf::TextSelection selection{};
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &);
    void synchronize(const Snapshot &, bool backward, bool deletion, bool replacement);
    void remember(const Snapshot &);
    bool history(bool redo);
    void changed();
    std::size_t hit(double) const;
    std::shared_ptr<gf::TextBox> edit_{};
    gf::TextStore store_{};
    std::vector<swiftedit::SearchSlot> slots_{};
    std::vector<double> edges_{};
    std::vector<std::string> labels_{};
    std::deque<Snapshot> undo_{}, redo_{};
    std::optional<std::size_t> hovered_{};
    double scroll_{};
    std::uint64_t revision_{1};
    bool focused_{}, dragging_{}, layout_dirty_{true};
};
} // namespace notepad
