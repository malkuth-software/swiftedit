#include "terminal_wrap_view.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
void measure_start(std::ofstream &raw) {
    swiftedit::TerminalBuffer buffer{};
    buffer.insert(std::string(1024 * 1024, 'x'));
    buffer.move(swiftedit::TerminalMotion::document_start);
    std::vector<double> samples{};
    samples.reserve(31);
    for (std::size_t index = 0; index < 31; ++index) {
        swiftedit::TerminalWrapView view{};
        const Clock::time_point start = Clock::now();
        const std::vector<swiftedit::TerminalWrappedRow> &rows = view.frame(buffer, 80, 24);
        const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
        samples.push_back(elapsed.count());
        if (rows.size() != 24 || rows[0].display.caret_column != 0)
            throw std::runtime_error("First-frame measurement lost its viewport or caret.");
    }
    for (std::size_t index = 0; index < samples.size(); ++index)
        raw << "ascii-1m,fresh-view-at-start," << index << ',' << samples[index] << '\n';
    std::sort(samples.begin(), samples.end());
    std::cout << "ascii-1m,fresh-view-at-start,p50=" << samples[15] << ",p95=" << samples[29]
              << ",p99=" << samples[30] << ",worst=" << samples.back() << '\n';
}
void measure(const char *name, const std::string &source, std::ofstream &raw) {
    swiftedit::TerminalBuffer buffer{};
    buffer.insert(source);
    buffer.move(swiftedit::TerminalMotion::document_end);
    swiftedit::TerminalWrapView view{};
    const Clock::time_point cold_start = Clock::now();
    static_cast<void>(view.frame(buffer, 80, 24));
    const std::chrono::duration<double, std::milli> cold = Clock::now() - cold_start;
    raw << name << ",first-frame,0," << cold.count() << '\n';
    std::vector<double> samples{};
    samples.reserve(31);
    for (std::size_t index = 0; index < 31; ++index) {
        const Clock::time_point start = Clock::now();
        view.move(buffer, swiftedit::TerminalMotion::up, false, 24, 80);
        const std::vector<swiftedit::TerminalWrappedRow> &rows = view.frame(buffer, 80, 24);
        const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
        samples.push_back(elapsed.count());
        bool caret_visible = false;
        for (const swiftedit::TerminalWrappedRow &row : rows) {
            if (row.display.caret_column)
                caret_visible = true;
        }
        if (!caret_visible || buffer.session().text() != source)
            throw std::runtime_error("Wrapped benchmark lost caret or changed source.");
    }
    for (std::size_t index = 0; index < samples.size(); ++index)
        raw << name << ",page-up-and-frame," << index << ',' << samples[index] << '\n';
    std::sort(samples.begin(), samples.end());
    std::cout << name << ",cold_ms=" << cold.count() << ",p50=" << samples[15]
              << ",p95=" << samples[29] << ",p99=" << samples[30] << ",worst=" << samples.back()
              << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-terminal-wrap-bench samples.csv");
        std::ofstream raw(argv[1]);
        if (!raw)
            throw std::runtime_error("Cannot create sample file.");
        raw << std::fixed << std::setprecision(6);
        raw << "fixture,operation,sample,elapsed_ms\n";
        measure_start(raw);
        measure("ascii-100k", std::string(100 * 1024, 'x'), raw);
        std::string mixed{};
        for (std::size_t index = 0; index < 8192; ++index)
            mixed += "a\t\xe7\x95\x8c"
                     "e\xcc\x81\x1b\xf0\x9f\x98\x80";
        measure("mixed-106496", mixed, raw);
        if (!raw)
            throw std::runtime_error("Cannot write samples.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
