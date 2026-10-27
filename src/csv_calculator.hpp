#pragma once
#include "csv.hpp"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace swiftedit {
struct CsvCalculated {
    Calculation value{};
    std::string error{};
    bool failed{};
};
struct CsvCompletion {
    CellAddress address{};
    std::shared_ptr<const CsvCalculated> result{};
};
// One worker, one replaceable batch, at most 64 queued results and 800000 queued
// references (the former eight-result worst case). Shared results count once per
// queue entry, conservatively. These are logical counts, not allocator byte caps.
// No UI callbacks or borrowed
// source survive submission. The caller serializes request/cancel/take; worker
// communication is protected by mutex_. Destruction requests stop and joins.
// All aliases must leave submitted Csv objects immutable until work retires.
class CsvCalculator final {
public:
    CsvCalculator();
    ~CsvCalculator();
    CsvCalculator(const CsvCalculator &) = delete;
    CsvCalculator &operator=(const CsvCalculator &) = delete;
    void request(std::shared_ptr<const Csv>, CellAddress);
    void request(std::shared_ptr<const Csv>, std::vector<CellAddress>);
    void cancel();
    [[nodiscard]] std::optional<CsvCompletion> take();
private:
    struct Request {
        std::shared_ptr<const Csv> table{};
        std::vector<CellAddress> addresses{};
        std::stop_token cancellation{};
    };
    struct Ready {
        CsvCalculator *owner{};
        bool operator()() const;
    };
    struct Space {
        CsvCalculator *owner{};
        std::stop_token cancellation{};
        std::size_t references{};
        bool operator()() const;
    };
    static void run(CsvCalculator *) noexcept;
    std::mutex mutex_{};
    std::condition_variable changed_{};
    std::optional<Request> pending_{};
    std::deque<CsvCompletion> completed_{};
    std::size_t queued_references_{};
    std::exception_ptr failure_{};
    std::stop_source cancellation_{};
    bool stopping_{};
    // Started after all observed members have been initialized.
    std::thread worker_{};
};
}
