#include "csv.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
struct Trial {
    const swiftedit::Csv &table;
    std::stop_token cancellation{};
    std::atomic<bool> entered{};
    bool cancelled{};
    std::exception_ptr failure{};
    static void run(Trial *owner) noexcept {
        Trial &trial = *owner;
        trial.entered.store(true, std::memory_order_release);
        try {
            const swiftedit::Calculation result =
                swiftedit::calculate_cell(trial.table, {0, 1}, trial.cancellation);
            if (result.result != "2")
                throw std::runtime_error("Uncancelled formula result changed.");
        } catch (const swiftedit::CalculationCancelled &) {
            trial.cancelled = true;
        } catch (...) {
            trial.failure = std::current_exception();
        }
    }
};
}
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-csv-cancel-bench samples.csv");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot write cancellation samples.");
        output << "trial,source_bytes,cancelled,request_to_join_ms\n"
               << std::fixed << std::setprecision(6);
        const std::string source = std::string(8 * 1024 * 1024, '0') + "1,=A1+A1";
        const swiftedit::Csv table(source);
        std::size_t cancelled = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            std::stop_source stop{};
            Trial trial{table, stop.get_token()};
            std::jthread worker(Trial::run, &trial);
            while (!trial.entered.load(std::memory_order_acquire))
                std::this_thread::yield();
            const std::chrono::steady_clock::time_point entered = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - entered < std::chrono::microseconds(100))
                std::this_thread::yield();
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            stop.request_stop();
            worker.join();
            const std::chrono::duration<double, std::milli> elapsed =
                std::chrono::steady_clock::now() - start;
            if (trial.failure)
                std::rethrow_exception(trial.failure);
            cancelled += trial.cancelled ? 1 : 0;
            output << index << ',' << source.size() << ',' << trial.cancelled << ','
                   << elapsed.count() << '\n';
        }
        // The same immutable table remains usable after worker cancellation.
        const swiftedit::Calculation result = swiftedit::calculate_cell(table, {0, 1});
        if (result.result != "2" || table.cell({0, 1}).value != "=A1+A1")
            throw std::runtime_error("Cancellation changed source or subsequent calculation.");
        std::cout << "cancelled=" << cancelled << "/8; source and subsequent exact result preserved.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
