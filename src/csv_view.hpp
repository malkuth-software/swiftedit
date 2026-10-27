#pragma once
#include "csv.hpp"
#include "csv_calculator.hpp"
#include <gui_forms/gui_forms.hpp>
#include <map>
#include <set>

namespace notepad {
namespace gf = gui_forms;
class CsvView final : public gf::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit CsvView(gf::StableId);
    ~CsvView() override;
    void initialize_control_tree();
    void set_source(std::string_view);
    void set_context_command(const std::shared_ptr<gf::Command> &);
    void select_cell(swiftedit::CellAddress);
    void select_all();
    void commit_cell(std::string_view);
    void convert_to_value();
    void clear_cells();
    [[nodiscard]] std::string copy_cells() const;
    gf::Event<const std::string &> &changed() { return changed_; }
    void arrange(gf::Rect) override;
    void on_paint(gf::Painter &, gf::Rect) override;
    void on_pointer(gf::PointerEvent &) override;
    void on_key(gf::KeyEvent &) override;
    void on_focus_changed(bool) override;
    void on_frame(gf::FrameTime) override;
    void cancel_calculations();
    [[nodiscard]] bool calculations_pending() const { return next_calculation_ < calculations_.size(); }
    const std::string &status() const { return status_; }
    swiftedit::CellAddress selected() const { return caret_; }
    gf::ContextMenu &context_menu() const { return *context_; }

protected:
    void on_dispose() noexcept override;

private:
    struct CommitListener {
        std::weak_ptr<CsvView> owner{};
        void operator()(const std::string &) const;
    };
    struct ScrollListener {
        std::weak_ptr<CsvView> owner{};
        void operator()(const double &) const;
    };
    struct EntryListener {
        std::weak_ptr<CsvView> owner{};
        void operator()() const;
        void operator()(const std::string &) const;
    };
    struct CellDisplay {
        std::string text{}, detail{};
        std::optional<std::string> exact_value{};
        std::set<std::pair<std::size_t, std::size_t>> visible_references{};
        bool formula{}, error{}, code_background{};
    };
    std::weak_ptr<CsvView> observe();
    void prepare_view();
    void advance_view();
    void finish_conversion();
    void cancel_conversion() noexcept;
    void reveal_caret();
    void update_scrollbars();
    void update_field();
    void clear_hover();
    void publish(std::string);
    std::optional<swiftedit::CellAddress> hit(gf::Point) const;
    std::shared_ptr<const swiftedit::Csv> table_{};
    std::unique_ptr<swiftedit::CsvCalculator> calculator_{};
    bool calculation_requested_{};
    std::optional<swiftedit::CellAddress> conversion_{};
    std::string source_{};
    std::shared_ptr<gf::TextBox> entry_{};
    std::shared_ptr<gf::VScrollBar> vertical_{};
    std::shared_ptr<gf::HScrollBar> horizontal_{};
    std::unique_ptr<gf::ContextMenu> context_{};
    std::unique_ptr<gf::ToolTip> tooltip_{};
    std::optional<std::pair<std::size_t, std::size_t>> hovered_{};
    std::vector<gf::SubscriptionToken> subscriptions_{};
    gf::Event<const std::string &> changed_{};
    std::map<std::pair<std::size_t, std::size_t>, CellDisplay> cells_{};
    std::vector<swiftedit::CellAddress> calculations_{};
    std::map<std::string, swiftedit::CellAddress> formula_sources_{};
    std::size_t next_calculation_{};
    gf::FrameRequestToken calculation_frame_{};
    swiftedit::CellAddress anchor_{}, caret_{};
    std::size_t columns_{}, top_{}, left_{}, visible_rows_{1}, visible_columns_{1};
    std::size_t whole_columns_{1};
    std::size_t cached_rows_{}, cached_columns_{};
    bool cells_dirty_{true};
    bool dragging_{};
    bool all_selected_{};
    std::string status_{"Enter edits the selected cell. Delete clears the selected rectangle."};
};
} // namespace notepad
