#include "session_copy.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace swiftedit {
SessionCopy::SessionCopy(const Session &session, const std::uint64_t offset,
                         const std::uint64_t length)
    : stamp_(session.stamp()), start_(offset), length_(length), size_(session.size()) {
    if (start_ > size_ || length_ > size_ - start_)
        throw std::runtime_error("Copy range exceeds document.");
    if (length_ > std::numeric_limits<std::size_t>::max() ||
        length_ > std::string{}.max_size())
        throw std::runtime_error("Copy range exceeds clipboard capacity.");
    pending_.bytes_.reserve(static_cast<std::size_t>(length_));
    if (length_ == 0)
        state_ = CopyState::complete;
}

void SessionCopy::step(const Session &session, const std::size_t budget) {
    if (state_ != CopyState::running)
        throw std::runtime_error("Copy task is not running.");
    if (budget == 0 || budget > maximum_page)
        throw std::runtime_error("Copy step budget must be 1..65536 bytes.");
    try {
        const DocumentStamp current = session.stamp();
        if (current.identity != stamp_.identity || current.revision != stamp_.revision ||
            session.size() != size_)
            throw std::runtime_error("Copy document changed; start a new copy.");
        const std::size_t amount = static_cast<std::size_t>(
            std::min<std::uint64_t>(budget, length_ - copied_));
        const std::uint64_t offset = start_ + copied_;
        Page page = session.page(offset, amount);
        if (page.offset != offset || page.size != size_ ||
            page.next != offset + amount || page.bytes.size() != amount)
            throw std::runtime_error("Copy source returned an inconsistent page.");
        pending_.bytes_.append(page.bytes);
        copied_ += amount;
        if (copied_ == length_)
            state_ = CopyState::complete;
    } catch (...) {
        pending_ = {};
        state_ = CopyState::failed;
        throw;
    }
}

void SessionCopy::cancel() noexcept {
    if (state_ == CopyState::running || state_ == CopyState::complete) {
        pending_ = {};
        state_ = CopyState::cancelled;
    }
}

SourceClipboard SessionCopy::take() {
    if (state_ != CopyState::complete)
        throw std::runtime_error("Copy has no completed result.");
    SourceClipboard result = std::move(pending_);
    pending_ = {};
    state_ = CopyState::taken;
    return result;
}
} // namespace swiftedit
