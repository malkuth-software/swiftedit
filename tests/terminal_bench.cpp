#include "terminal_row.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    const std::chrono::duration<double, std::milli> duration = Clock::now() - start;
    return duration.count();
}
double percentile(const std::vector<double> &sorted, std::size_t percent) {
    const std::size_t rank = (sorted.size() * percent + 99) / 100;
    return sorted[rank - 1];
}
void report(const char *fixture, const char *operation, std::size_t bytes,
            std::vector<double> samples, std::ofstream &raw) {
    if (raw.is_open()) {
        for (std::size_t index = 0; index < samples.size(); ++index)
            raw << fixture << ',' << operation << ',' << bytes << ',' << index << ','
                << samples[index] << '\n';
        if (!raw)
            throw std::runtime_error("Cannot write benchmark samples.");
    }
    std::sort(samples.begin(), samples.end());
    std::cout << fixture << ',' << operation << ',' << bytes << ',' << samples.size() << ','
              << percentile(samples, 50) << ',' << percentile(samples, 95) << ','
              << percentile(samples, 99) << ',' << samples.back() << '\n';
}
swiftedit::TerminalRow draw(swiftedit::TerminalRowCache &cache, swiftedit::TerminalBuffer &buffer,
                            std::size_t column, bool indexed) {
    swiftedit::TerminalRow result =
        indexed ? cache.row(buffer, 0, column, 80) : swiftedit::terminal_row(buffer, 0, column, 80);
    return result;
}
void measure(const char *fixture, const std::string &source, std::size_t repetitions, bool indexed,
             std::ofstream &raw) {
    const std::size_t bytes = source.size();
    swiftedit::TerminalBuffer buffer{};
    buffer.insert(source);
    buffer.move(swiftedit::TerminalMotion::document_start);
    swiftedit::TerminalRowCache cache{};
    const Clock::time_point cold_start = Clock::now();
    const swiftedit::TerminalRow cold = draw(cache, buffer, 0, indexed);
    report(fixture, "first-row-after-navigation", bytes, {elapsed(cold_start)}, raw);
    std::vector<double> samples{};
    samples.reserve(repetitions);
    std::size_t observed = 0;
    for (std::size_t index = 0; index < repetitions; ++index) {
        const std::size_t column = index * (cold.total_cells - 80) / (repetitions - 1);
        const Clock::time_point start = Clock::now();
        const swiftedit::TerminalRow row = draw(cache, buffer, column, indexed);
        samples.push_back(elapsed(start));
        observed += row.runs.size();
        if (row.total_cells != cold.total_cells)
            throw std::runtime_error("Benchmark row width changed.");
    }
    report(fixture, "warm-horizontal-row", bytes, samples, raw);
    samples.clear();
    for (std::size_t index = 0; index < 11; ++index) {
        const Clock::time_point start = Clock::now();
        buffer.insert("y");
        const swiftedit::TerminalRow row = draw(cache, buffer, 0, indexed);
        samples.push_back(elapsed(start));
        observed += row.runs.size();
    }
    report(fixture, "insert-and-row", bytes, samples, raw);
    if (!observed)
        throw std::runtime_error("Benchmark produced no rows.");
}
void measure_provider(std::ofstream &raw) {
    const std::size_t bytes = 1024 * 1024;
    gui_forms::TextStore text(std::string(bytes, 'x'));
    std::vector<double> samples{};
    samples.reserve(31);
    for (std::size_t index = 0; index < 31; ++index) {
        const Clock::time_point start = Clock::now();
        const gui_forms::TextEditResult result =
            text.replace({gui_forms::Utf8Offset(0), gui_forms::Utf8Offset(0)}, "y");
        static_cast<void>(result);
        const double milliseconds = elapsed(start);
        samples.push_back(milliseconds);
    }
    report("ascii-1m", "public-textstore-replace", bytes, samples, raw);
}
} // namespace
int main(int argc, char **argv) {
    try {
        bool indexed = true;
        std::string sample_path{};
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--reference" && indexed)
                indexed = false;
            else if (argument == "--samples" && sample_path.empty() && index + 1 < argc) {
                ++index;
                sample_path = argv[index];
            } else
                throw std::runtime_error(
                    "Usage: swiftedit-terminal-bench [--reference] [--samples path]");
        }
        std::ofstream raw{};
        if (!sample_path.empty()) {
            raw.open(sample_path, std::ios::trunc);
            if (!raw)
                throw std::runtime_error("Cannot create benchmark sample file.");
            raw << std::fixed << std::setprecision(6);
            raw << "fixture,operation,source_bytes,sample,elapsed_ms\n";
        }
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "fixture,operation,source_bytes,samples,p50_ms,p95_ms,p99_ms,worst_ms\n";
        measure_provider(raw);
        measure("ascii-100k", std::string(100 * 1024, 'x'), 101, indexed, raw);
        measure("ascii-1m", std::string(1024 * 1024, 'x'), 31, indexed, raw);
        std::string mixed{};
        for (std::size_t index = 0; index < 8192; ++index)
            mixed += "a\t\xe7\x95\x8c"
                     "e\xcc\x81\x1b\xf0\x9f\x98\x80";
        measure("mixed", mixed, 101, indexed, raw);
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
