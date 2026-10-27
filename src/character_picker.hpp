#pragma once
#include "characters.hpp"
#include <gui_forms/gui_forms.hpp>

namespace notepad {
namespace gf = gui_forms;
class CharacterPicker final : public gf::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    CharacterPicker(gf::StableId, bool controls);
    void initialize_control_tree();
    void arrange(gf::Rect) override;
    void select_codepoint(char32_t);
    void insert_selected();
    void copy_selected();
    gf::Event<const std::string &> &inserted() { return inserted_; }
    gf::Event<> &closed() { return closed_; }
    std::shared_ptr<gf::Button> insert_button() const { return insert_; }
    std::shared_ptr<gf::Button> close_button() const { return close_; }
    std::string status() const { return (*detail_).text(); }

protected:
    void on_dispose() noexcept override;

private:
    enum class Action { previous, next, go, insert, copy, close };
    struct Click {
        std::weak_ptr<CharacterPicker> owner{};
        Action action{};
        void operator()(gf::ButtonBase &) const;
    };
    struct Selection {
        std::weak_ptr<CharacterPicker> owner{};
        void operator()(const gf::ListSelectionChange &) const;
    };
    void action(Action);
    void load_page(char32_t);
    void update_selection();
    swiftedit::CharacterInfo selected_info() const;
    bool controls_{};
    char32_t page_{};
    std::shared_ptr<gf::TextBox> codepoint_{};
    std::shared_ptr<gf::ListBox> list_{};
    std::shared_ptr<gf::Label> heading_{}, glyph_{}, detail_{};
    std::shared_ptr<gf::Button> previous_{}, next_{}, go_{}, insert_{}, copy_{}, close_{};
    std::vector<swiftedit::CharacterInfo> rows_{};
    std::vector<gf::SubscriptionToken> subscriptions_{};
    gf::Event<const std::string &> inserted_{};
    gf::Event<> closed_{};
};
} // namespace notepad
