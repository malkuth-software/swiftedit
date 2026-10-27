#include "markdown_view.hpp"
#include "native_font_check.hpp"
#include <gui_forms/application.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef __APPLE__
#include "mac_view_snapshot.hpp"
#endif

namespace gf = gui_forms;
void report_samples(const std::string_view label, std::vector<double> samples) {
    if (samples.size() != 21)
        throw std::runtime_error("Markdown timing sample set is incomplete.");
    std::sort(samples.begin(), samples.end());
    // Nearest-rank percentiles for 21 observations: p99 equals the maximum.
    std::cout << "Markdown " << label << " ms; n=21; p50=" << samples[10]
              << "; p95=" << samples[19] << "; p99=" << samples[20]
              << "; max=" << samples.back() << '\n';
}
class MarkdownProbe final : public gf::Painter {
public:
    std::size_t words{};
    void save() override {}
    void restore() override {}
    void translate(gf::Point) override {}
    void clip_rect(gf::Rect) override {}
    void fill_rect(gf::Rect, gf::Color) override {}
    void stroke_rect(gf::Rect, gf::Color, double) override {}
    void draw_line(gf::Point, gf::Point, gf::Color, double) override {}
    void draw_image(gf::ImageId, gf::Rect, double) override {}
    void draw_text_utf8(gf::Point, std::string_view text, gf::FontSpec, gf::Color) override {
        if (text.starts_with("stressword"))
            ++words;
    }
};
// Synchronous Application::run owns the host. Named callbacks borrow this stack
// owner until shutdown; no native handle or timer survives run().
class MarkdownNative final {
public:
    explicit MarkdownNative(const bool distinct_words) : distinct_words_(distinct_words) {}
    struct Ready {
        MarkdownNative *owner{};
        void operator()(gf::Window &window, gf::ApplicationWindowHandle handle) const {
            (*owner).ready(window, handle);
        }
    };
    struct Tick {
        MarkdownNative *owner{};
        void operator()() const { (*owner).observe(); }
    };
    void run() {
        completion_samples_.reserve(21);
        layout_samples_.reserve(21);
        presentation_samples_.reserve(21);
        gap_samples_.reserve(21);
        stress_source_ = "> ";
        for (std::size_t word = 0; word < 10000; ++word) {
            stress_source_ += "stressword";
            if (distinct_words_)
                stress_source_ += std::to_string(word);
            stress_source_ += ' ';
            if (distinct_words_ && word % 20 == 19 && word + 1 < 10000)
                stress_source_ += "\n> ";
        }
        std::cout << "Markdown workload: " << (distinct_words_ ? "10000 distinct words" : "10000 repeated words")
                  << "; same-process samples: " << (distinct_words_ ? 1 : 21) << '\n';
        view_ = gf::make_control<notepad::MarkdownView>(gf::StableId("native.markdown"));
        (*view_).set_source(
            "# SwiftEdit Markdown\n\n"
            "Plain text with **bold**, *italic*, ~~strike~~ and `inline code`.\n\n"
            "> A quoted paragraph stays readable.\n\n"
            "- [x] Completed task\n- [ ] Remaining task\n\n"
            "| Item | Value |\n| --- | --- |\n| Alpha | 42 |\n| Beta | 7 |\n\n"
            "```text\ncode block <literal> & text\nsecond line\n```\n\n"
            "[Inert example link](https://example.invalid/?a=1&amp;b=2)\n\n"
            "![Image alt text only](https://example.invalid/image.png)\n");
        gf::ApplicationWindow entry{};
        entry.stable_id = "markdown.native";
        entry.model = std::make_unique<gf::Window>(view_, gf::Size{900, 800});
        entry.options.title = "SwiftEdit Markdown native regression";
        entry.options.initial_size = {900, 800};
        entry.options.ready = Ready{this};
        std::vector<gf::ApplicationWindow> windows{};
        windows.push_back(std::move(entry));
        const gf::ApplicationResult result = gf::Application::run(std::move(windows));
        subscription_ = {};
        timer_.reset();
        view_.reset();
        if (close_started_ != gf::FrameTime{}) {
            const std::chrono::duration<double, std::milli> elapsed = gf::FrameClock::now() - close_started_;
            std::cout << "Markdown pending-work close through owner release ms: " << elapsed.count() << '\n';
        }
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (failure_)
            std::rethrow_exception(failure_);
        if (!result.accepted() || !passed_ || !stress_passed_ || close_started_ == gf::FrameTime{})
            throw std::runtime_error("Native Markdown did not complete a visible paint.");
    }

private:
    enum class LayoutShutdown { none, await_cancel, verify_cancel, await_close };
    void begin_stress() {
        // Change the source each time so every observation prepares and lays
        // out a fresh model. The varying suffix remains below the viewport.
        const std::string source = stress_source_ + "\n\nSample " +
            std::to_string(completion_samples_.size());
        (*window_).reset_activity_metrics();
        maximum_tick_gap_ms_ = 0;
        stress_started_ = gf::FrameClock::now();
        last_stress_tick_ = stress_started_;
        (*view_).set_source(source);
    }
    void observe() {
        try {
            tick();
        } catch (...) {
            failure_ = std::current_exception();
            (*timer_).stop();
            static_cast<void>(handle_.request_close());
        }
    }
    void ready(gf::Window &window, gf::ApplicationWindowHandle handle) {
        require_native_fonts(window);
        window_ = &window;
        handle_ = handle;
        timer_ = std::make_unique<gf::Timer>(window, std::chrono::milliseconds(50));
        subscription_ = (*timer_).tick().subscribe(Tick{this});
        (*timer_).start();
    }
    void observe_layout_shutdown() {
        if (gf::FrameClock::now() - shutdown_started_ > std::chrono::seconds(15))
            throw std::runtime_error("Native Markdown layout shutdown readiness timeout.");
        if (layout_shutdown_ == LayoutShutdown::verify_cancel) {
            if ((*view_).layout_pending() || (*view_).preparation_pending() || (*view_).presentation_ready())
                throw std::runtime_error("Cancelled native Markdown resumed without a request.");
            const gf::MetricsSnapshot cancelled = (*window_).metrics().snapshot();
            if (cancelled.scheduled_frame_requests || cancelled.frame_deadlines_fired)
                throw std::runtime_error("Cancelled native Markdown retained scheduling.");
            std::cout << "Markdown routed Escape remained cancelled through the next UI tick.\n";
            (*view_).set_source(stress_source_ + " changed");
            layout_shutdown_ = LayoutShutdown::await_close;
            return;
        }
        if (!(*view_).layout_pending())
            return;
        if (layout_shutdown_ == LayoutShutdown::await_cancel) {
            if (!(*window_).request_focus(view_))
                throw std::runtime_error("Native Markdown could not focus for cancellation.");
            const gf::FrameTime started = gf::FrameClock::now();
            const bool handled = (*window_).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::escape});
            const std::chrono::duration<double, std::milli> elapsed = gf::FrameClock::now() - started;
            if (!handled || (*view_).layout_pending() || (*view_).preparation_pending())
                throw std::runtime_error("Routed Escape did not cancel native Markdown layout.");
            std::cout << "Markdown routed Escape handler ms: " << elapsed.count() << '\n';
            (*window_).reset_activity_metrics();
            layout_shutdown_ = LayoutShutdown::verify_cancel;
            return;
        }
        (*timer_).stop();
        close_started_ = gf::FrameClock::now();
        static_cast<void>(handle_.request_close());
    }
    void tick() {
        ++ticks_;
        if (layout_shutdown_ != LayoutShutdown::none) {
            observe_layout_shutdown();
            return;
        }
        const gf::MetricsSnapshot metrics = (*window_).metrics().snapshot();
        if (stress_passed_) {
            if (!idle_started_) {
                // Exclude the observer's interval change and the completing
                // scheduler callback before measuring a settled interval.
                (*window_).reset_activity_metrics();
                idle_started_ = true;
                return;
            }
            std::cout << "Markdown settled idle: " << metrics.to_json() << '\n';
            if (metrics.scheduled_frame_requests || metrics.frame_deadlines_fired)
                throw std::runtime_error("Completed Markdown retained calculation scheduling.");
            (*view_).set_source(stress_source_ + " changed");
            if (!(*view_).preparation_pending())
                throw std::runtime_error("Markdown close fixture did not create pending work.");
            if (distinct_words_) {
                layout_shutdown_ = LayoutShutdown::await_cancel;
                shutdown_started_ = gf::FrameClock::now();
                (*timer_).set_interval(std::chrono::milliseconds(10));
                return;
            }
            (*timer_).stop();
            close_started_ = gf::FrameClock::now();
            static_cast<void>(handle_.request_close());
            return;
        }
        if (stress_started_ != gf::FrameTime{}) {
            const gf::FrameTime now = gf::FrameClock::now();
            const std::chrono::duration<double, std::milli> gap = now - last_stress_tick_;
            maximum_tick_gap_ms_ = std::max(maximum_tick_gap_ms_, gap.count());
            last_stress_tick_ = now;
            const std::chrono::duration<double, std::milli> elapsed = now - stress_started_;
            if (metrics.paint_passes == 0 || !(*view_).presentation_ready()) {
                if (elapsed.count() < 15000)
                    return;
                throw std::runtime_error("Native Markdown stress paint readiness timeout.");
            }
            MarkdownProbe probe{};
            (*view_).on_paint(probe, (*view_).arranged_bounds());
            if (!probe.words || probe.words > 2000)
                throw std::runtime_error("Native Markdown stress viewport did not retain bounded text.");
            std::cout << "Markdown stress first paint observed ms: " << elapsed.count()
                      << "; visible words: " << probe.words << "; metrics: " << metrics.to_json() << '\n';
            const std::chrono::duration<double, std::milli> layout = (*view_).last_layout_duration();
            std::cout << "Markdown stress cumulative layout ms: " << layout.count()
                      << "; maximum observer gap ms: " << maximum_tick_gap_ms_ << '\n';
            const std::chrono::duration<double, std::milli> slice = (*view_).longest_layout_slice();
            std::cout << "Markdown longest layout slice ms: " << slice.count() << '\n';
            completion_samples_.push_back(elapsed.count());
            layout_samples_.push_back(layout.count());
            presentation_samples_.push_back(
                static_cast<double>(metrics.worst_present_duration_nanoseconds) / 1000000.0);
            gap_samples_.push_back(maximum_tick_gap_ms_);
            if (!distinct_words_ && completion_samples_.size() < 21) {
                begin_stress();
                return;
            }
            if (!distinct_words_) {
                report_samples("observed completion", completion_samples_);
                report_samples("cumulative layout", layout_samples_);
                report_samples("worst presentation per sample", presentation_samples_);
                report_samples("maximum observer gap per sample", gap_samples_);
            }
            stress_passed_ = true;
            (*timer_).set_interval(std::chrono::milliseconds(200));
            return;
        }
        if ((*window_).occluded() || metrics.paint_passes == 0 || !(*view_).presentation_ready()) {
            if (ticks_ < 100)
                return;
        } else {
            try {
                if ((*view_).block_count() < 8)
                    throw std::runtime_error("Markdown native fixture did not parse its blocks.");
#ifdef __APPLE__
                capture_native_view("SwiftEdit Markdown native regression", "markdown");
#endif
                passed_ = true;
                std::cout << "Native Markdown observed paint: " << metrics.to_json() << '\n';
                begin_stress();
                (*timer_).set_interval(std::chrono::milliseconds(10));
                return;
            } catch (...) {
                failure_ = std::current_exception();
            }
        }
        (*timer_).stop();
        static_cast<void>(handle_.request_close());
    }
    std::shared_ptr<notepad::MarkdownView> view_{};
    gf::ApplicationWindowHandle handle_{};
    gf::Window *window_{};
    std::unique_ptr<gf::Timer> timer_{};
    gf::SubscriptionToken subscription_{};
    std::exception_ptr failure_{};
    std::size_t ticks_{};
    bool distinct_words_{};
    bool passed_{}, stress_passed_{}, idle_started_{};
    std::string stress_source_{};
    gf::FrameTime stress_started_{};
    gf::FrameTime last_stress_tick_{};
    double maximum_tick_gap_ms_{};
    std::vector<double> completion_samples_{}, layout_samples_{}, presentation_samples_{}, gap_samples_{};
    gf::FrameTime close_started_{}, shutdown_started_{};
    LayoutShutdown layout_shutdown_{LayoutShutdown::none};
};
int main(const int argc, char *argv[]) {
    try {
        const bool distinct_words = argc == 2 && std::string_view(argv[1]) == "--distinct-words";
        if (argc > 2 || (argc == 2 && !distinct_words))
            throw std::runtime_error("Expected no argument or --distinct-words.");
        MarkdownNative test(distinct_words);
        test.run();
        std::cout << "Native Markdown paint and shutdown completed; pixels require inspection.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
