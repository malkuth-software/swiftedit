#include "csv.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-csv-clear-bench samples.csv");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot create benchmark samples.");
        std::string source{}, expected{};
        source.reserve(900000);
        expected.reserve(100000);
        for (std::size_t row = 0; row < 1000; ++row) {
            if (row) {
                source += '\n';
                expected += '\n';
            }
            for (std::size_t column = 0; column < 100; ++column) {
                if (column) {
                    source += ',';
                    expected += ',';
                }
                source += "abcdefgh";
            }
        }
        const swiftedit::Csv table(source);
        output << "sample,milliseconds\n" << std::fixed << std::setprecision(6);
        std::vector<double> samples{};
        samples.reserve(31);
        for (std::size_t sample = 0; sample < 34; ++sample) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const std::string result = table.clear({0, 0}, {999, 99});
            const std::chrono::duration<double, std::milli> elapsed =
                std::chrono::steady_clock::now() - start;
            if (result != expected || table.cell({999, 99}).value != "abcdefgh")
                throw std::runtime_error("Clear changed table shape or source authority.");
            if (sample >= 3) {
                samples.push_back(elapsed.count());
                output << sample - 3 << ',' << elapsed.count() << '\n';
            }
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "bytes=" << source.size() << " cells=100000 samples=" << samples.size()
                  << " p50_ms=" << samples[15] << " p95_ms=" << samples[29]
                  << " p99_ms=" << samples[30] << " worst_ms=" << samples.back() << '\n';
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
