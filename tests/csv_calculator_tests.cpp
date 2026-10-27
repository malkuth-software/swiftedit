#include "csv_calculator.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
swiftedit::CsvCalculated finish(swiftedit::CsvCalculator &calculator) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        std::optional<swiftedit::CsvCompletion> result = calculator.take();
        if (result) {
            swiftedit::CsvCalculated value = *(*result).result;
            return value;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("CSV worker timed out.");
        std::this_thread::yield();
    }
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
}
int main() {
    try {
        swiftedit::CsvCalculator calculator{};
        std::shared_ptr<const swiftedit::Csv> source =
            std::make_shared<const swiftedit::Csv>("2,=A1*3");
        calculator.request(source, {0, 1});
        source.reset();
        const swiftedit::CsvCalculated first = finish(calculator);
        require(!first.failed && first.value.result == "6",
                "Worker owns immutable source independently of caller lifetime");
        require(!calculator.take(), "A completed result is consumed exactly once");
        const std::shared_ptr<const swiftedit::Csv> slow =
            std::make_shared<const swiftedit::Csv>(std::string(1024 * 1024, '0') + "1,=A1+A1");
        const std::shared_ptr<const swiftedit::Csv> replacement =
            std::make_shared<const swiftedit::Csv>("7,=A1*2");
        const std::vector<swiftedit::CellAddress> batch(160, swiftedit::CellAddress{0, 1});
        calculator.request(replacement, batch);
        for (std::size_t index = 0; index < batch.size(); ++index)
            require(finish(calculator).value.result == "14", "Bounded result queue drains the full batch");
        calculator.request(replacement, batch);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        calculator.cancel();
        calculator.request(replacement, {0, 1});
        require(finish(calculator).value.result == "14" && !calculator.take(),
                "Cancelling a potentially full queue wakes its producer and rejects queued stale results");
        std::string reference_source = "2,\"=SUM(";
        for (std::size_t range = 0; range < 8; ++range) {
            if (range)
                reference_source += ',';
            reference_source += "A1:A10000";
        }
        reference_source += ")\"";
        for (std::size_t row = 1; row < 10000; ++row)
            reference_source += "\n2";
        const std::shared_ptr<const swiftedit::Csv> reference_table =
            std::make_shared<const swiftedit::Csv>(reference_source);
        const std::vector<swiftedit::CellAddress> reference_batch(20, swiftedit::CellAddress{0, 1});
        calculator.request(reference_table, reference_batch);
        for (std::size_t index = 0; index < reference_batch.size(); ++index) {
            const swiftedit::CsvCalculated value = finish(calculator);
            require(!value.failed && value.value.result == "160000" &&
                    value.value.references.size() == 80000,
                    "Reference-heavy results drain beyond the queued reference budget");
        }
        calculator.request(reference_table, reference_batch);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        calculator.cancel();
        calculator.request(replacement, {0, 1});
        require(finish(calculator).value.result == "14" && !calculator.take(),
                "Reference-budget cancellation clears accounting and permits a fresh request");
        for (std::size_t index = 0; index < 32; ++index) {
            calculator.request(slow, {0, 1});
            calculator.cancel();
            calculator.request(replacement, {0, 1});
            const swiftedit::CsvCalculated current = finish(calculator);
            require(!current.failed && current.value.result == "14",
                    "Cancellation and replacement never publish an obsolete result");
        }
        calculator.request(std::make_shared<const swiftedit::Csv>("=A1"), {0, 0});
        const swiftedit::CsvCalculated failed = finish(calculator);
        require(failed.failed && failed.error.find("Circular") != failed.error.npos,
                "Formula errors cross the worker boundary as owned diagnostics");
        calculator.request(replacement, {0, 1});
        require(finish(calculator).value.result == "14", "Worker recovers after a formula error");
        calculator.request(slow, {0, 1});
        calculator.cancel();
        require(!calculator.take(), "Cancelled work cannot publish a ready result");
        {
            swiftedit::CsvCalculator closing{};
            closing.request(slow, {0, 1});
        }
        std::cout << "CSV worker ownership, cancellation, stale refusal and close passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
