#include "csv_view.hpp"
#include "native_font_check.hpp"
#include <gui_forms/application.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef __APPLE__
#include "mac_view_snapshot.hpp"
#endif

namespace gf = gui_forms;
class CsvResultProbe final : public gf::Painter {
public:
    std::size_t results{};
    bool error{};
    void save() override {}
    void restore() override {}
    void translate(gf::Point) override {}
    void clip_rect(gf::Rect) override {}
    void fill_rect(gf::Rect, gf::Color) override {}
    void stroke_rect(gf::Rect, gf::Color, double) override {}
    void draw_line(gf::Point, gf::Point, gf::Color, double) override {}
    void draw_image(gf::ImageId, gf::Rect, double) override {}
    void draw_text_utf8(gf::Point, std::string_view text, gf::FontSpec, gf::Color) override {
        if (text == "8192")
            ++results;
        if (text == "#ERROR" || text == "...")
            error = true;
    }
};
// Application::run synchronously owns the window. Named hooks borrow this
// stack owner until shutdown; the measurement timer is stopped before close.
class CsvNative final {
public:
    struct Ready {
        CsvNative *owner{};
        void operator()(gf::Window &window, gf::ApplicationWindowHandle handle) const {
            (*owner).ready(window, handle);
        }
    };
    struct Tick {
        CsvNative *owner{};
        void operator()() const { (*owner).tick(); }
    };
    void run() {
        for (std::size_t row = 0; row < 4096; ++row) {
            if (row)
                stress_source_ += '\n';
            stress_source_ += '2';
            for (std::size_t column = 1; column < 8; ++column)
                stress_source_ += ",=SUM(A1:A4096)+" + std::to_string(row * 8 + column) + "*0";
        }
        view_ = gf::make_control<notepad::CsvView>(gf::StableId("native.csv"));
        gf::ApplicationWindow entry{};
        entry.stable_id = "csv.native";
        entry.model = std::make_unique<gf::Window>(view_, gf::Size{800, 600});
        entry.options.title = "SwiftEdit CSV native regression";
        entry.options.initial_size = {800, 600};
        entry.options.ready = Ready{this};
        std::vector<gf::ApplicationWindow> windows{};
        windows.push_back(std::move(entry));
        const gf::ApplicationResult result = gf::Application::run(std::move(windows));
        subscription_ = {};
        timer_.reset();
        view_.reset();
        if (close_started_ != gf::FrameTime{}) {
            const std::chrono::duration<double, std::milli> elapsed = gf::FrameClock::now() - close_started_;
            std::cout << "CSV active-work close through owner release ms: " << elapsed.count() << '\n';
        }
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (capture_failure_)
            std::rethrow_exception(capture_failure_);
        if (!result.accepted() || !passed_ || close_started_ == gf::FrameTime{})
            throw std::runtime_error("Native CSV completion or settled-idle check failed.");
    }

private:
    void ready(gf::Window &window, gf::ApplicationWindowHandle handle) {
        require_native_fonts(window);
        handle_ = handle;
        window_ = &window;
        window.perform_layout();
        std::string source{};
        for (std::size_t row = 0; row < 40; ++row) {
            if (row)
                source += '\n';
            source += "=" + std::to_string(1000 + row);
            // Visible evidence includes results, errors and inert code shading.
            // Keep the first column distinct to exercise deferred evaluation.
            if (row == 0)
                source += ",=1/0,```code```";
            else if (row == 1)
                source += ",=A2+1,plain text";
            else
                source += ",,";
        }
        (*view_).set_source(source);
        if (!(*view_).calculations_pending())
            throw std::runtime_error("Native fixture did not create deferred work.");
        timer_ = std::make_unique<gf::Timer>(window, std::chrono::milliseconds(50));
        subscription_ = (*timer_).tick().subscribe(Tick{this});
        (*timer_).start();
    }
    void tick() {
        ++ticks_;
        // No direct on_frame call: only the native scheduler may finish work.
        if (phase_ == 0 && !(*view_).calculations_pending()) {
            static_cast<void>((*window_).request_focus({}));
            (*timer_).set_interval(std::chrono::milliseconds(200));
            phase_ = 1;
        } else if (phase_ == 1) {
            (*window_).reset_activity_metrics();
            phase_ = 2;
        } else if (phase_ == 2) {
            const gf::MetricsSnapshot metrics = (*window_).metrics().snapshot();
            std::cout << "CSV settled idle metrics: " << metrics.to_json() << '\n';
            passed_ = !(*view_).calculations_pending() && metrics.frame_deadlines_fired == 0 &&
                      metrics.scheduled_frame_requests == 0;
#ifdef __APPLE__
            // Capture only after the measurement so its forced view redraw
            // cannot contaminate settled-idle counters.
            try {
                capture_native_view("SwiftEdit CSV native regression", "csv");
            } catch (...) {
                capture_failure_ = std::current_exception();
            }
#endif
            stress_started_ = gf::FrameClock::now();
            (*window_).reset_activity_metrics();
            (*view_).set_source(stress_source_);
            const std::chrono::duration<double, std::milli> prepared = gf::FrameClock::now() - stress_started_;
            std::cout << "CSV native stress source preparation ms: " << prepared.count() << '\n';
            if (!(*window_).request_focus(view_))
                throw std::runtime_error("Native CSV stress focus failed.");
            last_tick_ = gf::FrameClock::now();
            (*timer_).set_interval(std::chrono::milliseconds(10));
            phase_ = 3;
        } else if (phase_ == 3) {
            const gf::FrameTime now = gf::FrameClock::now();
            const std::chrono::duration<double, std::milli> gap = now - last_tick_;
            maximum_gap_ms_ = std::max(maximum_gap_ms_, gap.count());
            last_tick_ = now;
            const gf::FrameTime input_start = gf::FrameClock::now();
            const bool down = (*window_).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::down});
            const bool up = (*window_).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::up});
            const std::chrono::duration<double, std::milli> input = gf::FrameClock::now() - input_start;
            maximum_input_ms_ = std::max(maximum_input_ms_, input.count());
            if (!down || !up || (*view_).selected().row != 0)
                throw std::runtime_error("Native CSV routed navigation failed during calculation.");
            const std::chrono::duration<double, std::milli> elapsed = now - stress_started_;
            const gf::MetricsSnapshot activity = (*window_).metrics().snapshot();
            if (gap.count() > 50) {
                std::cout << "CSV delayed observation at ms: " << elapsed.count()
                          << "; gap ms: " << gap.count()
                          << "; pending: " << (*view_).calculations_pending()
                          << "; native metrics: " << activity.to_json() << '\n';
            }
            if (!(*view_).calculations_pending()) {
                CsvResultProbe probe{};
                (*view_).on_paint(probe, (*view_).arranged_bounds());
                if (probe.error || probe.results != 85)
                    throw std::runtime_error("Native CSV stress viewport did not retain 85 exact results.");
                std::cout << "CSV native stress completion observed ms: " << elapsed.count()
                          << "; maximum timer gap ms: " << maximum_gap_ms_
                          << "; maximum routed Down/Up ms: " << maximum_input_ms_ << '\n';
                std::cout << "CSV stress native metrics: " << activity.to_json() << '\n';
                (*view_).cancel_calculations();
                (*view_).set_source(stress_source_);
                if (!(*view_).calculations_pending())
                    throw std::runtime_error("Native CSV close probe did not restart pending work.");
                (*timer_).stop();
                close_started_ = gf::FrameClock::now();
                static_cast<void>(handle_.request_close());
            } else if (elapsed.count() > 5000) {
                throw std::runtime_error("Native CSV stress work exceeded readiness timeout.");
            }
        } else if (ticks_ >= 40) {
            (*timer_).stop();
            static_cast<void>(handle_.request_close());
        }
    }
    std::shared_ptr<notepad::CsvView> view_{};
    gf::ApplicationWindowHandle handle_{};
    gf::Window *window_{};
    std::unique_ptr<gf::Timer> timer_{};
    gf::SubscriptionToken subscription_{};
    std::size_t ticks_{}, phase_{};
    bool passed_{};
    std::string stress_source_{};
    gf::FrameTime stress_started_{}, last_tick_{}, close_started_{};
    double maximum_gap_ms_{}, maximum_input_ms_{};
    std::exception_ptr capture_failure_{};
};
int main() {
    try {
        CsvNative test{};
        test.run();
        std::cout << "Native CSV calculation frames completed and shut down cleanly.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
