#pragma once
#include "session.hpp"
#include "word_count.hpp"

namespace swiftedit {
enum class WordCountState { running, complete, cancelled, failed };
// Owns only counting state. Each step borrows the supplied session, checks its
// lifetime/revision and reads at most maximum_page bytes. Dropping or cancelling
// the task is immediate; no callback or source ownership survives a step.
class SessionWordCount final {
public:
    explicit SessionWordCount(const Session &session);
    void step(const Session &session, const std::size_t budget = 4096);
    void cancel() noexcept;
    [[nodiscard]] WordCountState state() const { return state_; }
    [[nodiscard]] std::uint64_t offset() const { return offset_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }
    [[nodiscard]] std::uint64_t result() const;

private:
    DocumentStamp stamp_{};
    std::uint64_t offset_{}, size_{}, result_{};
    notepad::WordCounter counter_{};
    WordCountState state_{WordCountState::running};
};
} // namespace swiftedit
