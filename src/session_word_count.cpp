#include "session_word_count.hpp"
#include <stdexcept>

namespace swiftedit {
SessionWordCount::SessionWordCount(const Session &session)
    : stamp_(session.stamp()), size_(session.size()) {
    if (size_ == 0) {
        result_ = counter_.finish();
        state_ = WordCountState::complete;
    }
}

void SessionWordCount::step(const Session &session, const std::size_t budget) {
    if (state_ != WordCountState::running)
        throw std::runtime_error("Word count task is not running.");
    if (budget == 0 || budget > maximum_page)
        throw std::runtime_error("Word count step budget must be 1..65536 bytes.");
    try {
        const DocumentStamp current = session.stamp();
        if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
            session.size() != size_)
            throw std::runtime_error("Word count document changed; start a new count.");
        const Page page = session.page(offset_, budget);
        if (page.offset != offset_ || page.next <= offset_ || page.next > size_ ||
            page.size != size_ || page.next - offset_ != page.bytes.size())
            throw std::runtime_error("Word count source returned an inconsistent page.");
        counter_.append(page.bytes);
        offset_ = page.next;
        if (offset_ == size_) {
            result_ = counter_.finish();
            state_ = WordCountState::complete;
        }
    } catch (...) {
        state_ = WordCountState::failed;
        throw;
    }
}

void SessionWordCount::cancel() noexcept {
    if (state_ == WordCountState::running)
        state_ = WordCountState::cancelled;
}

std::uint64_t SessionWordCount::result() const {
    if (state_ != WordCountState::complete)
        throw std::runtime_error("Word count has no completed result.");
    return result_;
}
} // namespace swiftedit
