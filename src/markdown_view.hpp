#pragma once
#include "markdown.hpp"
#include "markdown_preparation.hpp"
#include <gui_forms/gui_forms.hpp>
#include <array>
#include <chrono>

namespace notepad {
namespace gf = gui_forms;
class MarkdownView final : public gf::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit MarkdownView(gf::StableId);
    ~MarkdownView() override;
    void initialize_control_tree();
    void set_source(std::string_view);
    void cancel_preparation();
    [[nodiscard]] bool preparation_pending() const { return preparation_pending_; }
    // Cumulative UI layout work across slices, including index construction.
    // Excludes waiting between frames, worker preparation and paint submission.
    [[nodiscard]] std::chrono::nanoseconds last_layout_duration() const { return layout_duration_; }
    [[nodiscard]] std::chrono::nanoseconds longest_layout_slice() const { return longest_layout_slice_; }
    [[nodiscard]] bool presentation_ready() const {
        const bool ready = !preparation_pending_ && source_prepared_ && !layout_dirty_ &&
            layout_painted_ && layout_error_.empty();
        return ready;
    }
    [[nodiscard]] bool layout_pending() const {
        const bool pending = source_prepared_ && layout_dirty_;
        return pending;
    }
    void on_frame(gf::FrameTime) override;
    gf::Size measure(gf::Size) override;
    void arrange(gf::Rect) override;
    void on_paint(gf::Painter &, gf::Rect) override;
    void on_pointer(gf::PointerEvent &) override;
    void on_key(gf::KeyEvent &) override;
    [[nodiscard]] const std::string &hovered_url() const { return hovered_url_; }
    [[nodiscard]] std::size_t block_count() const {
        const std::size_t count = blocks_.size();
        return count;
    }

protected:
    void on_dispose() noexcept override;

private:
    struct Run {
        gf::Rect bounds{};
        gf::FontSpec font{};
        std::string text{};
        // Borrows an immutable prepared span. Runs are cleared before a new
        // prepared model is adopted and never outlive the owning view.
        std::string_view url{};
        double baseline{}, underline{}, strike_y{};
        bool strike{}, code{}, border{}, rule{};
    };
    struct RunOrder {
        bool operator()(const Run &left, const Run &right) const;
    };
    // Synchronous cursor borrows the immutable layout during one paint/hover.
    // At most 250000 leaves need fewer than 20 pending traversal branches.
    class VisibleRuns final {
    public:
        VisibleRuns(const MarkdownView &, double top, double bottom);
        std::optional<std::size_t> next();
    private:
        struct Branch { std::size_t node{}, first{}, last{}; };
        const MarkdownView &owner_;
        double top_{}, bottom_{};
        std::array<Branch, 32> pending_{};
        std::size_t count_{};
    };
    struct ScrollListener {
        std::weak_ptr<MarkdownView> owner{};
        void operator()(const double &) const;
    };
    struct PreparationTick {
        std::weak_ptr<MarkdownView> owner{};
        void operator()(gf::FrameTime) const;
    };
    struct LayoutWork;
    std::unique_ptr<LayoutWork> layout_work_{};
    void advance_layout(gf::TextMetricsProvider &);
    void layout(gf::TextMetricsProvider &, double width);
    void update_scroll_ranges();
    void clear_hover();
    void schedule_preparation();
    std::unique_ptr<swiftedit::MarkdownPreparation> preparation_{};
    gf::FrameRequestToken preparation_frame_{};
    bool preparation_pending_{}, source_prepared_{true};
    std::string source_{};
    std::vector<swiftedit::MarkdownBlock> blocks_{};
    std::vector<Run> runs_{};
    std::vector<double> run_bottoms_{};
    std::size_t run_leaves_{};
    std::shared_ptr<gf::VScrollBar> vertical_{};
    std::shared_ptr<gf::HScrollBar> horizontal_{};
    std::vector<gf::SubscriptionToken> subscriptions_{};
    std::unique_ptr<gf::ToolTip> tooltip_{};
    double layout_width_{}, content_height_{}, content_width_{};
    double ruler_baseline_{};
    std::chrono::nanoseconds layout_duration_{}, longest_layout_slice_{};
    bool layout_dirty_{true}, layout_painted_{};
    std::string hovered_url_{};
    std::string layout_error_{};
};
} // namespace notepad
