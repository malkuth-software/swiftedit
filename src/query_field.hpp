#pragma once
#include "search.hpp"
#include <gui_forms/gui_forms.hpp>
#include <deque>

namespace notepad {
namespace gf = gui_forms;
// A composite field (GUI.Forms' composite focus): the private TextBox is the
// edit part. It takes the keyboard focus itself (focus_target), so the keys,
// the text input and the clipboard reach it with no forwarding, and the
// window reports this control as the active one. This control stays
// unfocusable and owns hit testing and the visible, flagged-grapheme
// rendering; the edit part is arranged to an empty box, so it holds focus and
// edits but paints nothing. Its edit filter reports the range of every user
// edit before it applies, so the wildcard flags follow the text.
// It also owns 64 query-history snapshots (4096 UTF-8 bytes each), so flags and
// selection are restored with text instead of relying on text-only child undo;
// it takes the undo and redo keys in the key preview, before the edit part.
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
    // Focus on the field goes to the edit part.
    [[nodiscard]] gf::Control::Ptr focus_target() const override;
    // The key preview (root to target) reaches this control before the edit
    // part: Control+Shift+/ toggles the flag at the caret, and Control or
    // Command with Z or Y (Shift+Z redoes) moves through this control's
    // history. Every other key goes on to the edit part.
    void on_key_preview(gf::KeyEvent &) override;
    // The routed form of the target phase: the mouse raisers feed it, and
    // tests drive it directly with a PointerEvent.
    void handle_pointer(gf::PointerEvent &);

protected:
    // The target phase: each raiser hands the routed form to handle_pointer.
    void on_mouse_down(gf::MouseEventArgs &arguments) override;
    void on_mouse_up(gf::MouseEventArgs &arguments) override;
    void on_mouse_move(gf::MouseEventArgs &arguments) override;
    void on_mouse_leave() override;
    void on_dispose() noexcept override;

private:
    struct Snapshot {
        std::string text{};
        std::vector<bool> flags{};
        gf::TextSelection selection{};
    };
    // One user edit the edit part's filter accepted, waiting for the text to
    // change: [start, start + removed) of before.text replaced by `inserted`
    // bytes.
    struct PendingEdit {
        Snapshot before{};
        std::size_t start{};
        std::size_t removed{};
        std::size_t inserted{};
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &);
    // Moves the flags across one edit of before.text into `value` and
    // records `before` in the history.
    void synchronize(const Snapshot &before, std::string_view value, std::size_t start, std::size_t removed,
                     std::size_t inserted);
    void remember(const Snapshot &);
    bool history(bool redo);
    void changed();
    std::size_t hit(double) const;
    // The edit part's filter and events (named targets; disconnected and
    // cleared in on_dispose).
    void filter_edit(gf::TextEdit &);
    void editor_text_changed(const std::string &);
    void editor_selection_changed(const gf::TextSelection &);
    void editor_focus_changed(bool);
    std::shared_ptr<gf::TextBox> edit_{};
    gf::SubscriptionToken editor_text_{};
    gf::SubscriptionToken editor_selection_{};
    gf::SubscriptionToken editor_focus_{};
    std::optional<PendingEdit> pending_{};
    gf::TextStore store_{};
    std::vector<swiftedit::SearchSlot> slots_{};
    std::vector<double> edges_{};
    std::vector<std::string> labels_{};
    std::deque<Snapshot> undo_{}, redo_{};
    std::optional<std::size_t> hovered_{};
    double scroll_{};
    std::uint64_t revision_{1};
    // Set while restore assigns the edit part's text, whose text_changed is
    // not a user edit.
    bool restoring_{};
    bool focused_{}, dragging_{}, layout_dirty_{true};
};
} // namespace notepad
