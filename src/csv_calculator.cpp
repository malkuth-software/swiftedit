#include "csv_calculator.hpp"
#include <stdexcept>
#include <utility>

namespace swiftedit {
CsvCalculator::CsvCalculator() : worker_(run, this) {}
CsvCalculator::~CsvCalculator() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        cancellation_.request_stop();
        pending_.reset();
    }
    changed_.notify_one();
    if (worker_.joinable())
        worker_.join();
}
void CsvCalculator::request(std::shared_ptr<const Csv> table, CellAddress address) {
    std::vector<CellAddress> addresses{address};
    request(std::move(table), std::move(addresses));
}
void CsvCalculator::request(std::shared_ptr<const Csv> table, std::vector<CellAddress> addresses) {
    if (!table || addresses.empty() || addresses.size() > 4096)
        throw std::runtime_error("CSV calculation requires owned source and 1..4096 cells.");
    for (const CellAddress address : addresses)
        static_cast<void>((*table).cell(address));
    std::stop_source next{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.request_stop();
        cancellation_ = std::move(next);
        pending_ = Request{std::move(table), std::move(addresses), cancellation_.get_token()};
        completed_.clear();
        queued_references_ = 0;
        failure_ = {};
    }
    changed_.notify_one();
}
void CsvCalculator::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.request_stop();
        pending_.reset();
        completed_.clear();
        queued_references_ = 0;
        failure_ = {};
    }
    changed_.notify_one();
}
std::optional<CsvCompletion> CsvCalculator::take() {
    std::optional<CsvCompletion> result{};
    std::exception_ptr failure{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failure = std::exchange(failure_, {});
        if (!completed_.empty()) {
            result = std::move(completed_.front());
            queued_references_ -= (*(*result).result).value.references.size();
            completed_.pop_front();
        }
    }
    changed_.notify_one();
    if (failure)
        std::rethrow_exception(failure);
    return result;
}
bool CsvCalculator::Ready::operator()() const {
    const bool ready = (*owner).stopping_ || (*owner).pending_.has_value();
    return ready;
}
bool CsvCalculator::Space::operator()() const {
    const bool space = (*owner).completed_.size() < 64 &&
        references <= 800000 - (*owner).queued_references_;
    const bool ready = (*owner).stopping_ || cancellation.stop_requested() || space;
    return ready;
}
void CsvCalculator::run(CsvCalculator *owner) noexcept {
    CsvCalculator &work = *owner;
    for (;;) {
        Request request{};
        {
            std::unique_lock<std::mutex> lock(work.mutex_);
            work.changed_.wait(lock, Ready{owner});
            if (work.stopping_)
                return;
            request = std::move(*work.pending_);
            work.pending_.reset();
        }
        try {
            std::string previous_formula{};
            std::shared_ptr<const CsvCalculated> previous{};
            for (const CellAddress address : request.addresses) {
                {
                    std::unique_lock<std::mutex> lock(work.mutex_);
                    work.changed_.wait(lock, Space{owner, request.cancellation});
                    if (work.stopping_ || request.cancellation.stop_requested())
                        break;
                }
                const std::string &formula = (*request.table).cell(address).value;
                std::shared_ptr<const CsvCalculated> result{};
                if (previous && formula == previous_formula) {
                    result = previous;
                } else {
                    CsvCalculated calculated{};
                    try {
                        calculated.value = calculate_cell(*request.table, address, request.cancellation);
                    } catch (const CalculationCancelled &) {
                        break;
                    } catch (const std::exception &failure) {
                        calculated.failed = true;
                        calculated.error = failure.what();
                    }
                    result = std::make_shared<const CsvCalculated>(std::move(calculated));
                    if (!(*result).failed) {
                        previous_formula = formula;
                        previous = result;
                    }
                }
                const std::size_t references = (*result).value.references.size();
                if (references > 800000)
                    throw std::runtime_error("CSV result exceeds the queued reference budget.");
                {
                    std::unique_lock<std::mutex> lock(work.mutex_);
                    work.changed_.wait(lock, Space{owner, request.cancellation, references});
                    if (work.stopping_ || request.cancellation.stop_requested())
                        break;
                    work.completed_.push_back(CsvCompletion{address, std::move(result)});
                    work.queued_references_ += references;
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(work.mutex_);
            if (!work.stopping_ && !request.cancellation.stop_requested())
                work.failure_ = std::current_exception();
        }
    }
}
} // namespace swiftedit
