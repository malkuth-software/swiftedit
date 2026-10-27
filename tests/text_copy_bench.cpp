#include "session_text_copy.hpp"
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
    std::string input{};
    input.reserve(swiftedit::editable_limit);
    while (input.size() < swiftedit::editable_limit)
        input.append(pattern, 0, std::min(pattern.size(), swiftedit::editable_limit - input.size()));
    const std::string expected = swiftedit::text_copy(input);
    const std::filesystem::path source = directory / (name + ".source");
    {
        std::ofstream file(source, std::ios::binary);
        file.write(input.data(), static_cast<std::streamsize>(input.size()));
        if (!file)
            throw std::runtime_error("Cannot create copy benchmark source.");
    }
    swiftedit::Session session{};
    session.open(source);
    const swiftedit::DocumentStamp stamp = session.stamp();
    std::vector<double> steps{}, publication{}, cancellation{}, preparation{}, dispatch{}, completion{};
    for (std::size_t trial = 0; trial < 3; ++trial) {
        const std::filesystem::path output = directory / (name + "-" + std::to_string(trial) + ".copy");
        Clock::time_point started = Clock::now();
        std::unique_ptr<swiftedit::SessionTextCopy> task =
            std::make_unique<swiftedit::SessionTextCopy>(session, output);
        const double prepare_ms = milliseconds(started);
        preparation.push_back(prepare_ms);
        raw << name << ',' << trial << ",prepare,0," << prepare_ms << ",\n";
        std::size_t step = 0;
        while ((*task).state() == swiftedit::TextCopyState::copying) {
            const double cpu_start = process_cpu_ms();
            started = Clock::now();
            const bool ready = (*task).step(session);
            const double wall_ms = milliseconds(started);
            const double cpu_ms = process_cpu_ms() - cpu_start;
            steps.push_back(wall_ms);
            raw << name << ',' << trial << ",step," << step++ << ',' << wall_ms << ',' << cpu_ms << '\n';
            if (ready)
                break;
        }
        const double cpu_start = process_cpu_ms();
        started = Clock::now();
        (*task).begin_publication(session);
        const double dispatch_ms = milliseconds(started);
        dispatch.push_back(dispatch_ms);
        raw << name << ',' << trial << ",dispatch,0," << dispatch_ms << ",\n";
        while (!(*task).publication_ready(std::chrono::milliseconds(8))) {}
        const Clock::time_point finishing = Clock::now();
        (*task).finish_publication();
        const double finish_ms = milliseconds(finishing);
        completion.push_back(finish_ms);
        raw << name << ',' << trial << ",completion,0," << finish_ms << ",\n";
        const double publish_ms = milliseconds(started);
        const double cpu_ms = process_cpu_ms() - cpu_start;
        publication.push_back(publish_ms);
        raw << name << ',' << trial << ",publish,0," << publish_ms << ',' << cpu_ms << '\n';
        task.reset();
        swiftedit::PagedFile result(output);
        if (result.size() != expected.size())
            throw std::runtime_error("Copy benchmark output size mismatch.");
        for (std::uint64_t offset = 0; offset < result.size();) {
            const swiftedit::Page page = result.page(offset, swiftedit::maximum_page);
            if (page.bytes != std::string_view(expected).substr(static_cast<std::size_t>(offset), page.bytes.size()))
                throw std::runtime_error("Copy benchmark output differs from whole-source conversion.");
            offset = page.next;
        }
        const std::filesystem::path abandoned = directory / (name + "-" + std::to_string(trial) + ".cancelled");
        task = std::make_unique<swiftedit::SessionTextCopy>(session, abandoned);
        for (std::size_t index = 0; index < 32; ++index)
            static_cast<void>((*task).step(session));
        started = Clock::now();
        task.reset();
        const double cancel_ms = milliseconds(started);
        cancellation.push_back(cancel_ms);
        raw << name << ',' << trial << ",cancel,0," << cancel_ms << ",\n";
        if (std::filesystem::exists(abandoned) || session.dirty() ||
            session.identity() != stamp.identity || session.revision() != stamp.revision)
            throw std::runtime_error("Copy benchmark changed source or published cancelled output.");
    }
    report(name, "step-wall-ms", steps);
    report(name, "prepare-wall-ms", preparation);
    report(name, "publish-wall-ms", publication);
    report(name, "dispatch-wall-ms", dispatch);
    report(name, "completion-wall-ms", completion);
    report(name, "cancel-wall-ms", cancellation);
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Supply a new owned benchmark directory.");
        const std::filesystem::path directory = std::filesystem::absolute(argv[1]);
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Benchmark directory must not already exist.");
        std::ofstream raw(directory / "samples.csv");
        raw << std::fixed << std::setprecision(6);
        std::cout << std::fixed << std::setprecision(6);
        raw << "fixture,trial,operation,index,wall_ms,cpu_ms\n";
        measure(directory, "ascii", "plain text\r\n", raw);
        measure(directory, "unicode", "a\xe7\x95\x8c\xf0\x9f\x98\x80\xcc\x81\r\n", raw);
        measure(directory, "invalid", std::string("\xff\xf0\x9f\0x", 5), raw);
        if (!raw)
            throw std::runtime_error("Cannot save copy benchmark samples.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
