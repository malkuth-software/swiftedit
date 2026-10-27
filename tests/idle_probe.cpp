#include "editor.hpp"
#include "native_font_check.hpp"
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>

namespace gf = gui_forms;

// CPU observations are diagnostic; hidden-dialog quietness is a regression gate.
// Application::run owns native windows synchronously. Hooks
// borrow this stack owner until run returns; the timer is stopped before close.
class IdleProbe final {
public:
    struct Ready {
        IdleProbe *owner{};
        std::size_t index{};
        void operator()(gf::Window &window, gf::ApplicationWindowHandle handle) const {
            (*owner).ready(index, window, handle);
        }
    };
    struct Tick {
        IdleProbe *owner{};
        void operator()() const { (*owner).tick(); }
    };
    struct ObservedWindow {
        std::string name{};
        gf::Window *model{};
        std::function<void(gf::Window &, gf::ApplicationWindowHandle)> ready{};
    };
    void run() {
        editor_ = gf::make_control<notepad::Editor>(gf::StableId("idle.editor"));
        std::vector<gf::ApplicationWindow> windows = (*editor_).application_windows({});
        observed_.reserve(windows.size());
        for (std::size_t index = 0; index < windows.size(); ++index) {
            observed_.push_back({windows[index].stable_id, windows[index].model.get(),
                                 std::move(windows[index].options.ready)});
            windows[index].options.ready = Ready{this, index};
        }
        const gf::ApplicationResult result = gf::Application::run(std::move(windows));
        subscription_ = {};
        timer_.reset();
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (failure_)
            std::rethrow_exception(failure_);
        if (!result.accepted() || !complete_)
            throw std::runtime_error("Idle probe did not finish its observations.");
    }

private:
    void ready(std::size_t index, gf::Window &window, gf::ApplicationWindowHandle handle) {
        require_native_fonts(window);
        if (observed_[index].ready)
            observed_[index].ready(window, handle);
        if (index == 0)
            main_handle_ = handle;
        ++ready_count_;
        if (ready_count_ == observed_.size()) {
            gf::Window &main = *observed_[0].model;
            if (!main.request_focus((*editor_).text_control()))
                throw std::runtime_error("Could not focus the blank document.");
            timer_ = std::make_unique<gf::Timer>(main, std::chrono::milliseconds(3000));
            subscription_ = (*timer_).tick().subscribe(Tick{this});
            (*timer_).start();
        }
    }
    void tick() {
        try {
            if (!measuring_) {
                for (const ObservedWindow &window : observed_)
                    (*window.model).reset_activity_metrics();
                cpu_start_ = std::clock();
                if (cpu_start_ == std::clock_t(-1))
                    throw std::runtime_error("Process CPU clock is unavailable.");
                started_ = std::chrono::steady_clock::now();
                measuring_ = true;
                (*timer_).set_interval(std::chrono::milliseconds(5000));
                return;
            }
            const std::clock_t cpu_end = std::clock();
            if (cpu_end == std::clock_t(-1) || cpu_end < cpu_start_)
                throw std::runtime_error("Process CPU clock observation is invalid.");
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - started_;
            const double cpu = static_cast<double>(cpu_end - cpu_start_) / CLOCKS_PER_SEC;
            const char *labels[] = {"focused", "focus-cleared"};
            std::cout << "idle-probe|phase=" << labels[phase_] << "|elapsed=" << elapsed.count()
                      << "|cpu_seconds=" << cpu
                      << "|one_core_percent=" << 100.0 * cpu / elapsed.count() << '\n';
            for (const ObservedWindow &window : observed_) {
                const gf::MetricsSnapshot metrics = (*window.model).metrics().snapshot();
                std::cout << "idle-window|phase=" << labels[phase_] << "|id=" << window.name
                          << "|occluded=" << (*window.model).occluded()
                          << "|focused=" << static_cast<bool>((*window.model).focused_control())
                          << "|metrics=" << metrics.to_json() << '\n';
            }
            std::cout.flush();
            // Report every window before rejecting an active hidden child, so a
            // failed run retains the complete topology for diagnosis.
            for (std::size_t index = 1; index < observed_.size(); ++index) {
                const ObservedWindow &window = observed_[index];
                const gf::MetricsSnapshot metrics = (*window.model).metrics().snapshot();
                if (!(*window.model).occluded() || metrics.paint_passes != 0 ||
                    metrics.frame_deadlines_fired != 0)
                    throw std::runtime_error("Hidden dialog was not idle: " + window.name);
            }
            measuring_ = false;
            ++phase_;
            if (phase_ == 1) {
                if (!(*observed_[0].model).request_focus({}))
                    throw std::runtime_error("Could not clear document focus.");
            } else {
                complete_ = true;
                (*timer_).stop();
                static_cast<void>(main_handle_.request_close());
                return;
            }
            (*timer_).set_interval(std::chrono::milliseconds(2000));
        } catch (...) {
            failure_ = std::current_exception();
            (*timer_).stop();
            static_cast<void>(main_handle_.request_close());
        }
    }
    std::shared_ptr<notepad::Editor> editor_{};
    std::vector<ObservedWindow> observed_{};
    gf::ApplicationWindowHandle main_handle_{};
    std::unique_ptr<gf::Timer> timer_{};
    gf::SubscriptionToken subscription_{};
    std::exception_ptr failure_{};
    std::chrono::steady_clock::time_point started_{};
    std::clock_t cpu_start_{};
    std::size_t ready_count_{}, phase_{};
    bool measuring_{}, complete_{};
};

int main() {
    try {
        IdleProbe probe{};
        probe.run();
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
