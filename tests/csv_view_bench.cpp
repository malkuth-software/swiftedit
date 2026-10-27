#include "csv_view.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace gf = gui_forms;
class ResultPainter final : public gf::Painter {
public:
    std::size_t results{};
    std::string expected{"1024"};
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
        if (text == expected)
            ++results;
        if (text == "#ERROR")
            error = true;
    }
};
int main(int argc, char **argv) {
    try {
        if (argc != 2 && (argc != 3 || (std::string_view(argv[2]) != "--large" &&
                                       std::string_view(argv[2]) != "--distinct")))
            throw std::runtime_error("Usage: swiftedit-csv-view-bench samples.csv [--large|--distinct]");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot write benchmark samples.");
        output << "sample,milliseconds,initial_milliseconds,worst_slice_milliseconds,slices,paint_milliseconds\n"
               << std::fixed << std::setprecision(6);
        std::string source{};
        const bool distinct = argc == 3 && std::string_view(argv[2]) == "--distinct";
        const std::size_t rows = distinct ? 4096 : argc == 3 ? 8192 : 512;
        const std::string formula = ",=SUM(A1:A" + std::to_string(rows) + ")";
        for (std::size_t row = 0; row < rows; ++row) {
            if (row)
                source += '\n';
            source += '2';
            for (std::size_t column = 1; column < 8; ++column) {
                source += formula;
                if (distinct)
                    source += "+" + std::to_string(row * 8 + column) + "*0";
            }
        }
        const std::shared_ptr<notepad::CsvView> view =
            gf::make_control<notepad::CsvView>(gf::StableId("bench.csv"));
        gf::Window window(view, {800, 600});
        const std::chrono::steady_clock::time_point setup_start = std::chrono::steady_clock::now();
        (*view).set_source(source);
        const std::chrono::steady_clock::time_point source_ready = std::chrono::steady_clock::now();
        window.perform_layout();
        const std::chrono::steady_clock::time_point layout_ready = std::chrono::steady_clock::now();
        std::size_t setup_slices = 0;
        while ((*view).calculations_pending()) {
            (*view).on_frame(gf::FrameClock::now());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++setup_slices;
            if (std::chrono::steady_clock::now() - setup_start > std::chrono::seconds(5))
                throw std::runtime_error("Initial viewport calculation failed to complete.");
        }
        const std::chrono::duration<double, std::milli> setup_source = source_ready - setup_start;
        const std::chrono::duration<double, std::milli> setup_layout = layout_ready - source_ready;
        const std::chrono::duration<double, std::milli> setup_total =
            std::chrono::steady_clock::now() - setup_start;
        std::vector<double> samples{};
        samples.reserve(31);
        for (std::size_t sample = 0; sample < 36; ++sample) {
            gf::PointerEvent wheel{};
            wheel.action = gf::PointerAction::wheel;
            wheel.wheel_delta.y = sample % 2 == 0 ? -1 : 1;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            (*view).on_pointer(wheel);
            const std::chrono::duration<double, std::milli> initial =
                std::chrono::steady_clock::now() - start;
            double worst_slice = initial.count();
            std::size_t slices = 1;
            while ((*view).calculations_pending()) {
                const std::chrono::steady_clock::time_point slice_start = std::chrono::steady_clock::now();
                (*view).on_frame(gf::FrameClock::now());
                const std::chrono::duration<double, std::milli> slice_time =
                    std::chrono::steady_clock::now() - slice_start;
                worst_slice = std::max(worst_slice, slice_time.count());
                ++slices;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5))
                    throw std::runtime_error("Viewport calculation failed to complete.");
            }
            const std::chrono::duration<double, std::milli> elapsed =
                std::chrono::steady_clock::now() - start;
            ResultPainter painter{};
            painter.expected = std::to_string(rows * 2);
            const std::chrono::steady_clock::time_point paint_start = std::chrono::steady_clock::now();
            (*view).on_paint(painter, {0, 0, 800, 600});
            const std::chrono::duration<double, std::milli> paint_time =
                std::chrono::steady_clock::now() - paint_start;
            if (!wheel.handled || painter.error || painter.results != 85)
                throw std::runtime_error(
                    "CSV viewport did not display all 85 exact formula results.");
            if (sample >= 5) {
                samples.push_back(elapsed.count());
                output << sample - 5 << ',' << elapsed.count() << ',' << initial.count()
                       << ',' << worst_slice << ',' << slices << ',' << paint_time.count() << '\n';
            }
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "source_bytes=" << source.size() << " samples=" << samples.size()
                  << " setup_source_ms=" << setup_source.count()
                  << " setup_layout_ms=" << setup_layout.count()
                  << " setup_total_ms=" << setup_total.count()
                  << " p50_ms=" << samples[15] << " p95_ms=" << samples[29]
                  << " p99_ms=" << samples[30] << " worst_ms=" << samples.back() << '\n';
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
