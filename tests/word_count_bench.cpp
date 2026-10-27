#include "session_word_count.hpp"
#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-word-count-bench samples.csv");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot create benchmark samples.");
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("swiftedit-count-bench-" + std::to_string(test_process_id()));
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Benchmark fixture already exists.");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const std::filesystem::path path = directory / "count.txt";
        constexpr std::size_t blocks = 1024;
        const std::string pattern = "a\xc2\xa0"
                                    "b ";
        const std::size_t repetitions = swiftedit::maximum_page / pattern.size();
        std::string block{};
        block.reserve(swiftedit::maximum_page);
        for (std::size_t index = 0; index < repetitions; ++index)
            block += pattern;
        block.resize(swiftedit::maximum_page, ' ');
        {
            std::ofstream fixture(path, std::ios::binary);
            for (std::size_t index = 0; index < blocks; ++index)
                fixture.write(block.data(), static_cast<std::streamsize>(block.size()));
            fixture.close();
            if (!fixture)
                throw std::runtime_error("Cannot write benchmark fixture.");
        }
        swiftedit::Session session{};
        session.open(path);
        const std::uint64_t expected = static_cast<std::uint64_t>(repetitions) * blocks * 2;
        output << "budget,pass,step,milliseconds\n" << std::fixed << std::setprecision(6);
        for (const std::size_t budget : {std::size_t(4096), swiftedit::maximum_page}) {
            std::vector<double> samples{};
            const std::size_t steps = static_cast<std::size_t>(session.size() / budget);
            samples.reserve(steps * 3);
            double total_ms = 0;
            for (std::size_t pass = 0; pass < 4; ++pass) {
                swiftedit::SessionWordCount task(session);
                const std::chrono::steady_clock::time_point started =
                    std::chrono::steady_clock::now();
                while (task.state() == swiftedit::WordCountState::running) {
                    const std::chrono::steady_clock::time_point step_start =
                        std::chrono::steady_clock::now();
                    task.step(session, budget);
                    const std::chrono::duration<double, std::milli> elapsed =
                        std::chrono::steady_clock::now() - step_start;
                    if (pass != 0)
                        samples.push_back(elapsed.count());
                }
                const std::chrono::duration<double, std::milli> total =
                    std::chrono::steady_clock::now() - started;
                if (pass != 0)
                    total_ms += total.count();
                if (task.result() != expected || session.dirty())
                    throw std::runtime_error("Benchmark count or source state differs.");
            }
            for (std::size_t index = 0; index < samples.size(); ++index)
                output << budget << ',' << index / steps << ',' << index % steps << ','
                       << samples[index] << '\n';
            std::sort(samples.begin(), samples.end());
            const std::size_t count = samples.size();
            std::cout << "bytes=" << session.size() << " budget=" << budget << " samples=" << count
                      << " words=" << expected << " p50_ms=" << samples[(count * 50 + 99) / 100 - 1]
                      << " p95_ms=" << samples[(count * 95 + 99) / 100 - 1]
                      << " p99_ms=" << samples[(count * 99 + 99) / 100 - 1]
                      << " worst_ms=" << samples.back() << " mean_scan_ms=" << total_ms / 3 << '\n';
        }
        output.close();
        if (!output)
            throw std::runtime_error("Cannot finish benchmark samples.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
