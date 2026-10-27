#include "session_search.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
double process_cpu_ms() {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("Cannot read benchmark process CPU time.");
    const std::uint64_t kernel_ticks = (static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32) |
                                       kernel.dwLowDateTime;
    const std::uint64_t user_ticks = (static_cast<std::uint64_t>(user.dwHighDateTime) << 32) |
                                     user.dwLowDateTime;
    const double result = static_cast<double>(kernel_ticks) / 10000.0 +
                          static_cast<double>(user_ticks) / 10000.0;
#else
    const std::clock_t ticks = std::clock();
    if (ticks == std::clock_t(-1) || ticks < 0)
        throw std::runtime_error("Benchmark process CPU clock is unavailable.");
    const double result = 1000.0 * static_cast<double>(ticks) / CLOCKS_PER_SEC;
#endif
    return result;
}
double milliseconds(const Clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
    const double result = elapsed.count();
    return result;
}
void report(const std::string_view fixture, const std::string_view operation,
            std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t p50 = (samples.size() * 50 + 99) / 100 - 1;
    const std::size_t p95 = (samples.size() * 95 + 99) / 100 - 1;
    const std::size_t p99 = (samples.size() * 99 + 99) / 100 - 1;
    std::cout << fixture << ',' << operation << ",samples=" << samples.size()
              << ",p50=" << samples[p50] << ",p95=" << samples[p95]
              << ",p99=" << samples[p99] << ",worst=" << samples.back() << '\n';
}
void measure(const std::filesystem::path &directory, const std::string &name,
             const std::string &pattern, std::ofstream &raw) {
    const std::filesystem::path path = directory / (name + ".txt");
    {
        std::ofstream file(path, std::ios::binary);
        std::string block{};
        while (block.size() < 32768)
            block += pattern;
        std::size_t remaining = swiftedit::editable_limit - 1;
        while (remaining) {
            const std::size_t count = std::min(remaining, block.size());
            file.write(block.data(), static_cast<std::streamsize>(count));
            remaining -= count;
        }
        file.put('Z');
        if (!file)
            throw std::runtime_error("Cannot write owned search benchmark fixture.");
    }
    swiftedit::Session session{};
    session.open(path);
    if (!session.read_only() || session.size() != swiftedit::editable_limit)
        throw std::runtime_error("Search benchmark did not enter paged read-only mode.");
    const swiftedit::DocumentStamp stamp = session.stamp();
    for (std::size_t mode = 0; mode < 2; ++mode) {
        const std::string label = name + (mode ? "-wildcard" : "-literal");
        swiftedit::SearchPattern query(mode ? "?Z" : "Z");
        if (mode)
            query.toggle(0);
        std::vector<double> slices{}, cpu_slices{}, totals{}, cpu_totals{}, cancellations{};
        slices.reserve(65536);
        cpu_slices.reserve(65536);
        for (std::size_t trial = 0; trial < 3; ++trial) {
            std::unique_ptr<swiftedit::SessionSearch> task =
                std::make_unique<swiftedit::SessionSearch>(session, query);
            std::vector<double> steps{}, cpu_steps{};
            steps.reserve(20000);
            cpu_steps.reserve(20000);
            const Clock::time_point total_start = Clock::now();
            const double total_cpu_start = process_cpu_ms();
            bool complete = false;
            while (!complete) {
                const Clock::time_point started = Clock::now();
                const double cpu_start = process_cpu_ms();
                complete = (*task).step(session, 4096);
                const double cpu_end = process_cpu_ms();
                steps.push_back(milliseconds(started));
                if (cpu_end < cpu_start)
                    throw std::runtime_error("Search benchmark CPU clock moved backwards.");
                cpu_steps.push_back(cpu_end - cpu_start);
                if (steps.size() > 20000)
                    throw std::runtime_error("Search benchmark exceeded fixture work bound.");
            }
            const std::optional<swiftedit::PagedSearchMatch> found = (*task).result(session);
            if (!found || (*found).offset + (*found).length != session.size() ||
                (mode == 0 && (*found).length != 1) || (mode == 1 && (*found).length < 2) ||
                session.dirty() || session.stamp().revision != stamp.revision)
                throw std::runtime_error("Search benchmark failed exact final-match/source checks.");
            const double total = milliseconds(total_start);
            const double total_cpu = process_cpu_ms() - total_cpu_start;
            if (total_cpu < 0)
                throw std::runtime_error("Search benchmark CPU clock moved backwards.");
            totals.push_back(total);
            cpu_totals.push_back(total_cpu);
            for (std::size_t index = 0; index < steps.size(); ++index) {
                slices.push_back(steps[index]);
                cpu_slices.push_back(cpu_steps[index]);
                raw << label << ',' << trial << ",step," << index << ',' << steps[index] << '\n';
                raw << label << ',' << trial << ",step-cpu," << index << ',' << cpu_steps[index] << '\n';
            }
            raw << label << ',' << trial << ",total,0," << total << '\n';
            raw << label << ',' << trial << ",total-cpu,0," << total_cpu << '\n';
            task = std::make_unique<swiftedit::SessionSearch>(session, query);
            for (std::size_t index = 0; index < 32; ++index)
                if ((*task).step(session, 4096))
                    throw std::runtime_error("Search cancellation fixture completed too soon.");
            const Clock::time_point cancel_start = Clock::now();
            task.reset();
            const double cancelled = milliseconds(cancel_start);
            cancellations.push_back(cancelled);
            raw << label << ',' << trial << ",cancel-release,0," << cancelled << '\n';
            if (session.dirty() || session.stamp().revision != stamp.revision)
                throw std::runtime_error("Search cancellation changed the source.");
        }
        report(label, "step", slices);
        report(label, "step-cpu", cpu_slices);
        report(label, "total", totals);
        report(label, "total-cpu", cpu_totals);
        report(label, "cancel-release", cancellations);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-search-bench new-output-directory");
        const std::filesystem::path directory(argv[1]);
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Search benchmark output directory must be new.");
        std::ofstream raw(directory / "samples.csv");
        if (!raw)
            throw std::runtime_error("Cannot create raw search benchmark samples.");
        raw << std::fixed << std::setprecision(6);
        raw << "fixture,trial,operation,sample,elapsed_ms\n";
        std::cout << std::fixed << std::setprecision(6);
        measure(directory, "ascii-16m", "x", raw);
        measure(directory, "controls-16m", std::string(1, '\0'), raw);
        measure(directory, "mixed-16m", "a\t\xe7\x95\x8c" "e\xcc\x81\r\n", raw);
        if (!raw)
            throw std::runtime_error("Cannot write raw search benchmark samples.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
