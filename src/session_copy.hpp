#pragma once
#include "session.hpp"

namespace swiftedit {
enum class CopyState { running, complete, cancelled, failed, taken };
// A source range uses 64-bit file offsets, including on 32-bit consumers.
// Construction reserves the selection's contiguous clipboard capacity without
// initializing it. Each step borrows its session, reads at most maximum_page
// bytes and appends without growth. take() transfers ownership without copying.
// No partial clipboard is exposed on cancellation, stale input or read failure.
class SessionCopy final {
public:
    SessionCopy(const Session &, const std::uint64_t offset, const std::uint64_t length);
    SessionCopy(const SessionCopy &) = delete;
    SessionCopy &operator=(const SessionCopy &) = delete;
    void step(const Session &, const std::size_t budget = 4096);
    void cancel() noexcept;
    [[nodiscard]] SourceClipboard take();
    [[nodiscard]] CopyState state() const { return state_; }
    [[nodiscard]] std::uint64_t copied() const { return copied_; }
    [[nodiscard]] std::uint64_t length() const { return length_; }

private:
    DocumentStamp stamp_{};
    std::uint64_t start_{}, length_{}, size_{}, copied_{};
    SourceClipboard pending_{};
    CopyState state_{CopyState::running};
};
} // namespace swiftedit
