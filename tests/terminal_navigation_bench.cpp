#include "terminal_app.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>

namespace {
using Clock = std::chrono::steady_clock;
struct Sample {
    std::size_t direction{};
    double milliseconds{};
};
// No native console is opened. Timing ends when the shared loop submits its
// complete screen string; OS input delivery and physical presentation are excluded.
class BenchConsole final : public swiftedit::TerminalConsole {
public:
    mutable std::vector<Sample> samples{};
    mutable std::size_t command{};
    mutable Clock::time_point started{};
    void start() override { samples.reserve(128); }
    swiftedit::TerminalSize size() const override { return {80, 24}; }
    void write(std::string_view screen) const override {
        if (screen.empty())
            throw std::runtime_error("Benchmark received an empty screen.");
        if (command == 1 || command == 2) {
            const Clock::time_point finished = Clock::now();
            const std::chrono::duration<double, std::milli> elapsed = finished - started;
            samples.push_back({command, elapsed.count()});
            started = Clock::now();
        }
    }
    bool input_ready() const override { return false; }
    swiftedit::TerminalInput read() const override {
        ++command;
        swiftedit::TerminalInput input{};
        input.pressed = true;
        if (command <= 2) {
            input.key = command == 1 ? swiftedit::terminal_key::down : swiftedit::terminal_key::up;
            input.repeats = 1000;
            started = Clock::now();
        } else if (command == 3) {
            input.key = 'X';
            input.control = true;
        } else
            throw std::runtime_error("Benchmark requested unexpected input.");
        return input;
    }
    bool cancel_requested() override { return false; }
    void allow_wrap_cancel(bool) override {}
};
double percentile(const std::vector<double> &sorted, const std::size_t percent) {
    const std::size_t rank = (sorted.size() * percent + 99) / 100;
    return sorted[rank - 1];
}
void measure(const std::filesystem::path &directory, const std::string &name,
             const std::string &line, std::ofstream &raw) {
    const std::filesystem::path path = directory / (name + ".txt");
    {
        std::ofstream fixture(path, std::ios::binary);
        std::size_t bytes = 0;
        while (bytes < swiftedit::editable_limit) {
            fixture.write(line.data(), static_cast<std::streamsize>(line.size()));
            bytes += line.size();
        }
        if (!fixture)
            throw std::runtime_error("Cannot write navigation fixture.");
    }
    std::vector<double> times{};
    for (std::size_t trial = 0; trial < 5; ++trial) {
        BenchConsole console{};
        swiftedit::Terminal terminal(console);
        if (terminal.run(path) || console.command != 3 || console.samples.size() != 126)
            throw std::runtime_error("Navigation benchmark did not complete both repeated movements.");
        for (std::size_t index = 0; index < console.samples.size(); ++index) {
            const Sample sample = console.samples[index];
            raw << name << ',' << trial << ',' << index << ',' << sample.direction << ','
                << sample.milliseconds << '\n';
            times.push_back(sample.milliseconds);
        }
    }
    std::sort(times.begin(), times.end());
    std::cout << name << ',' << times.size() << ',' << percentile(times, 50) << ','
              << percentile(times, 95) << ',' << percentile(times, 99) << ',' << times.back() << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-terminal-navigation-bench NEW_DIRECTORY");
        const std::filesystem::path directory(argv[1]);
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Benchmark requires a new output directory.");
        std::ofstream raw(directory / "samples.csv");
        raw << std::fixed << std::setprecision(6);
        raw << "fixture,trial,slice,direction,milliseconds\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "fixture,samples,p50_ms,p95_ms,p99_ms,worst_ms\n";
        measure(directory, "ascii", std::string(79, 'x') + "\n", raw);
        measure(directory, "unicode", "abc e\xcc\x81 \xe7\x95\x8c \xf0\x9f\x98\x80\n", raw);
        measure(directory, "controls", "abc\t\x1b\x01\xff\r\n", raw);
        raw.flush();
        if (!raw)
            throw std::runtime_error("Cannot preserve navigation samples.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
