#include "editor.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-editor-navigation-bench samples.csv");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot write benchmark samples.");
        const std::shared_ptr<notepad::Editor> editor =
            gui_forms::make_control<notepad::Editor>(gui_forms::StableId("bench.editor"));
        gui_forms::Window window(editor, {800, 600});
        const std::shared_ptr<gui_forms::TextBox> text = (*editor).text_control();
        std::string source{};
        source.reserve(65 * 4096);
        for (std::size_t line = 0; line < 4096; ++line)
            source += "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ab\n";
        (*text).set_text(source);
        const std::shared_ptr<gui_forms::Label> status =
            std::dynamic_pointer_cast<gui_forms::Label>(window.find("notepad.status"));
        if (!status)
            throw std::runtime_error("Missing status label.");
        std::vector<double> samples{};
        samples.reserve(1000);
        output << "sample,milliseconds\n";
        for (std::size_t sample = 0; sample < 1100; ++sample) {
            const gui_forms::Utf8Offset position(source.size() - 2 - sample % 2);
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            (*text).select(position, position);
            const std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
            const std::chrono::duration<double, std::milli> elapsed = finish - start;
            const std::string expected =
                sample % 2 == 0 ? "Ln 4096, Col 64 |" : "Ln 4096, Col 63 |";
            if (!(*status).text().starts_with(expected))
                throw std::runtime_error("Navigation status correctness failed.");
            if (sample >= 100) {
                samples.push_back(elapsed.count());
                output << sample - 100 << ',' << elapsed.count() << '\n';
            }
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "bytes=" << source.size() << " samples=" << samples.size()
                  << " p50_ms=" << samples[499] << " p95_ms=" << samples[949]
                  << " p99_ms=" << samples[989] << " worst_ms=" << samples.back() << '\n';
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
