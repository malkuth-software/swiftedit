#pragma once
#include "search.hpp"
#include <gui_forms/gui_forms.hpp>
#include <deque>

namespace notepad {
namespace gf = gui_forms;
// A composite field (GUI.Forms' composite focus) over GUI.Forms' content
// editing mode. The edit part is a ContentTextEditor: it takes the keyboard
// focus itself (focus_target), so the editing keys, the input method, the
// clipboard and accessibility are GUI.Forms'; the window reports this control
// as the active one. This control lays the query out (one cluster per
// grapheme: a flagged grapheme is drawn as a dot, so its width is not the
// literal's), submits that layout to the editor, and paints the text, the
// selection and the caret itself. The editor fills the field, draws nothing
// (no frames, set_draws_selection(false)) and is transparent to the pointer,
// so this control keeps hit testing, hover and the right-click toggle. The
// editor's input-method caret is the caret of the submitted layout, and its
// text box node is the field's one accessible node.
// The editor's edit filter reports the range of every user edit before it
// applies, so the wildcard flags follow the text, and refuses edits past the
// 4096-byte budget.
// It also owns 64 query-history snapshots (4096 UTF-8 bytes each), so flags and
// selection are restored with text instead of relying on text-only child undo;
// it takes the undo and redo keys in the key preview, before the edit part.
// Label widths are measured through the window (outside paint), when the
// text or the flags change and when the field is arranged.
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
    void arrange(gf::Rect final_bounds) override;
    void on_paint(gf::Painter &, gf::Rect) override;
    // What the editor said of the last layout submitted to it (accepted
    // unless the layout disagreed with the editor's text).
    [[nodiscard]] gf::ContentLayoutResult layout_result() const noexcept { return layout_result_; }
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
    // Measures each grapheme's label through the window, when the labels are
    // stale and the field is attached; leaves them stale otherwise.
    void prepare_labels();
    // Keeps the caret inside the text area; resubmits the layout when the
    // scroll moved.
    void update_scroll();
    // The layout of the current text for the editor's current revision, in
    // the editor's local coordinates (the field's: it is docked to fill).
    gf::ContentTextLayout content_layout() const;
    // Submits content_layout when the labels are measured, the editor's text
    // is this control's, and the editor does not already hold it.
    void publish_layout();
    // The edit part's filter and events (named targets; disconnected and
    // cleared in on_dispose).
    void filter_edit(gf::TextEdit &);
    void editor_text_changed(const std::string &);
    void editor_selection_changed(const gf::TextSelection &);
    void editor_focus_changed(bool);
    void editor_layout_requested(std::uint64_t);
    std::shared_ptr<gf::ContentTextEditor> edit_{};
    gf::SubscriptionToken editor_text_{};
    gf::SubscriptionToken editor_selection_{};
    gf::SubscriptionToken editor_focus_{};
    gf::SubscriptionToken editor_layout_{};
    std::optional<PendingEdit> pending_{};
    gf::TextStore store_{};
    std::vector<swiftedit::SearchSlot> slots_{};
    // Label edges from the text area's start: edges_[i] is grapheme i's left
    // edge, so it holds one more entry than slots_ once measured.
    std::vector<double> edges_{0};
    std::vector<std::string> labels_{};
    std::deque<Snapshot> undo_{}, redo_{};
    std::optional<std::size_t> hovered_{};
    double scroll_{};
    gf::ContentLayoutResult layout_result_{gf::ContentLayoutResult::accepted};
    std::uint64_t revision_{1};
    // Set while restore assigns the edit part's text, whose text_changed is
    // not a user edit.
    bool restoring_{};
    bool focused_{}, dragging_{}, layout_dirty_{true};
    // Whether the editor holds the layout of the current text, labels and
    // scroll (cleared by every change of them).
    bool layout_submitted_{};
};
} // namespace notepad
