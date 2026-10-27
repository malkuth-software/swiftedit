#include "editor.hpp"
#include "native_font_check.hpp"
#include "new_window.hpp"
#include "platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace gf = gui_forms;
constexpr std::string_view parent_text = "unsaved \xc3\xa9 \xf0\x9f\x98\x80 parent document";
constexpr std::string_view copied_text = "\xc3\xa9 \xf0\x9f\x98\x80";
void require(const bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
void receipt(const char *name) {
    std::ofstream output(name, std::ios::binary);
    output << "complete\n";
    output.close();
    require(static_cast<bool>(output), "Write native lifecycle receipt");
}
// Application::run owns windows synchronously. All listeners borrow this stack
// owner until run returns; the timer stops before close and its token is revoked
// before destruction. The launched process has a separate copy of every owner.
class IndependentWindow final {
public:
    explicit IndependentWindow(const bool parent) : parent_(parent) {}
    struct Ready {
        IndependentWindow *owner{};
        std::size_t index{};
        void operator()(gf::Window &window, gf::ApplicationWindowHandle handle) const {
            (*owner).ready(index, window, handle);
        }
    };
    struct Closing {
        IndependentWindow *owner{};
        std::size_t index{};
        void operator()(gf::HostCloseRequest &request) const {
            if ((*owner).failure_)
                request.cancel = false;
            else if ((*owner).hooks_[index].closing)
                (*owner).hooks_[index].closing(request);
        }
    };
    struct Tick {
        IndependentWindow *owner{};
        void operator()() const { (*owner).tick(); }
    };
    struct Launch {
        IndependentWindow *owner{};
        void operator()() const {
            try {
                notepad::launch_window(notepad::executable_path());
            } catch (...) {
                // Preserve the failure without invoking a blocking native error
                // dialog in this unattended fixture. Production uses NewWindow.
                (*owner).failure_ = std::current_exception();
            }
        }
    };
    void run() {
        editor_ = gf::make_control<notepad::Editor>(gf::StableId("independent.editor"), Launch{this});
        std::vector<gf::ApplicationWindow> windows = (*editor_).application_windows({});
        hooks_.reserve(windows.size());
        for (std::size_t index = 0; index < windows.size(); ++index) {
            hooks_.push_back({std::move(windows[index].options.ready),
                              std::move(windows[index].options.closing)});
            windows[index].options.ready = Ready{this, index};
            windows[index].options.closing = Closing{this, index};
        }
        const gf::ApplicationResult result = gf::Application::run(std::move(windows));
        subscription_ = {};
        timer_.reset();
        if (failure_)
            std::rethrow_exception(failure_);
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        require(result.accepted() && complete_, "Independent native lifecycle completed");
    }
private:
    struct Hooks {
        std::function<void(gf::Window &, gf::ApplicationWindowHandle)> ready{};
        std::function<void(gf::HostCloseRequest &)> closing{};
    };
    void ready(const std::size_t index, gf::Window &window, gf::ApplicationWindowHandle handle) {
        require_native_fonts(window);
        if (hooks_[index].ready)
            hooks_[index].ready(window, handle);
        if (!index) {
            window_ = &window;
            handle_ = handle;
        }
        ++ready_count_;
        if (ready_count_ == hooks_.size()) {
            if (!parent_) {
                require((*(*editor_).text_control()).text().empty(), "Child starts blank");
                receipt("child.ready");
            }
            timer_ = std::make_unique<gf::Timer>(*window_, std::chrono::milliseconds(50));
            subscription_ = (*timer_).tick().subscribe(Tick{this});
            (*timer_).start();
        }
    }
    void tick() {
        try {
            if (failure_)
                std::rethrow_exception(failure_);
            require(++ticks_ < 200, "Independent native lifecycle timed out");
            gf::TextBox &text = *(*editor_).text_control();
            if (parent_ && ticks_ == 1) {
                text.replace_selection(parent_text);
                text.select(gf::Utf8Offset(8), gf::Utf8Offset(15));
                const gf::TextSelection selection = text.selection();
                clipboard_key(gf::PhysicalKey::c);
                const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
                (*editor_).execute("new-window");
                const std::chrono::duration<double, std::milli> launch_time =
                    std::chrono::steady_clock::now() - started;
                std::cout << "new-window-command|return_ms=" << launch_time.count() << '\n';
                require(!failure_, "Native child launch accepted");
                require(text.text() == parent_text && text.selection() == selection &&
                            (*editor_).document().dirty(text.text()), "Parent document preserved");
            }
            require(!std::filesystem::exists("child.failed"), "Child reported native failure");
            if (!parent_ && !pasted_) {
                clipboard_key(gf::PhysicalKey::v);
                require(text.text() == copied_text, "Native Unicode clipboard crosses independent processes");
                pasted_ = true;
                receipt("child.pasted");
            }
            const bool ready_to_close = parent_ ? std::filesystem::exists("child.pasted") :
                                                 std::filesystem::exists("parent.closed");
            if (!ready_to_close)
                return;
            if (!parent_) {
                require(text.text() == copied_text, "Child source remains independent after parent closes");
                text.select_all();
                text.replace_selection("child still editable");
                require(text.text() == "child still editable", "Child still accepts edits");
            }
            // Discard only this fixture's authored text before ordinary closing,
            // avoiding an unattended save dialog. Preservation was checked above.
            text.select_all();
            text.replace_selection("");
            complete_ = true;
            (*timer_).stop();
#ifdef __APPLE__
            gf::KeyEvent close{};
            close.action = gf::KeyAction::down;
            close.physical_key = gf::PhysicalKey::w;
            close.modifiers = gf::Modifier::meta;
            require((*window_).dispatch_key(close), "Command W closes this editor through its command");
#else
            static_cast<void>(handle_.request_close());
#endif
        } catch (...) {
            failure_ = std::current_exception();
            (*timer_).stop();
            static_cast<void>(handle_.request_close());
        }
    }
    void clipboard_key(const std::uint32_t physical_key) {
        require((*window_).request_focus((*editor_).text_control()),
                "Focus the document for its clipboard shortcut");
        gf::KeyEvent event{};
        event.action = gf::KeyAction::down;
        event.physical_key = physical_key;
#ifdef __APPLE__
        event.modifiers = gf::Modifier::meta;
#else
        event.modifiers = gf::Modifier::control;
#endif
        require((*window_).dispatch_key(event), "Native document clipboard shortcut handled");
    }
    bool parent_{}, complete_{}, pasted_{};
    unsigned ticks_{};
    std::size_t ready_count_{};
    std::shared_ptr<notepad::Editor> editor_{};
    std::vector<Hooks> hooks_{};
    gf::Window *window_{};
    gf::ApplicationWindowHandle handle_{};
    std::unique_ptr<gf::Timer> timer_{};
    gf::SubscriptionToken subscription_{};
    std::exception_ptr failure_{};
};

int main(const int argc, char **) {
    const bool parent = argc > 1;
    bool owned_fixture = false;
    try {
        const std::filesystem::path previous = std::filesystem::current_path();
        std::filesystem::path fixture = previous;
        if (parent) {
            fixture = std::filesystem::temp_directory_path() /
                ("swiftedit-window-native-" + std::to_string(test_process_id()));
            require(std::filesystem::create_directory(fixture), "Own unique native fixture");
            std::filesystem::current_path(fixture);
            receipt("fixture.owner");
        } else {
            require(previous.filename().string().starts_with("swiftedit-window-native-") &&
                        std::filesystem::is_regular_file("fixture.owner"), "Child owns fixture context");
        }
        owned_fixture = true;
        IndependentWindow test(parent);
        test.run();
        receipt(parent ? "parent.closed" : "child.closed");
        if (parent) {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(12);
            while (!std::filesystem::exists("child.closed") &&
                   !std::filesystem::exists("child.failed") &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            require(std::filesystem::exists("child.closed"), "Child survived parent GUI shutdown");
            std::filesystem::current_path(previous);
            std::error_code cleanup_failure{};
            do {
                cleanup_failure.clear();
                std::filesystem::remove_all(fixture, cleanup_failure);
                if (cleanup_failure)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } while (cleanup_failure && std::chrono::steady_clock::now() < deadline);
            require(!cleanup_failure, "Child released its fixture working directory");
            std::cout << "Two independent native editor windows completed\n";
        }
        return 0;
    } catch (const std::exception &failure) {
        if (!parent && owned_fixture) {
            std::ofstream output("child.failed");
            output << failure.what();
        }
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
