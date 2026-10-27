#include "source_snapshot.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace swiftedit {
SnapshotCapture::SnapshotCapture(const Session &session, const std::vector<SnapshotRange> &parts,
                                 const SnapshotLimits limits) {
    if (parts.empty() || parts.size() > 1000 || parts.size() > limits.parts)
        throw std::runtime_error("Snapshot part quota exceeded.");
    const std::uint64_t size = session.size();
    std::uint64_t bytes = 0, chunks = 0, previous_end = 0;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const SnapshotRange range = parts[index];
        if (range.offset > size || range.length > size - range.offset ||
            (index && range.offset < previous_end))
            throw std::runtime_error("Snapshot ranges must be ordered, disjoint and within source.");
        if (range.length > limits.bytes - bytes)
            throw std::runtime_error("Snapshot source-byte quota exceeded.");
        bytes += range.length;
        const std::uint64_t count = range.length / maximum_page + (range.length % maximum_page != 0);
        if (count > limits.chunks - chunks)
            throw std::runtime_error("Snapshot chunk quota exceeded.");
        chunks += count;
        previous_end = range.offset + range.length;
    }
    // Allocate metadata after checking every range and charge. Source buffers
    // are allocated one bounded chunk at a time while stepping.
    pending_ = std::unique_ptr<SourceSnapshot>(new SourceSnapshot());
    (*pending_).stamp_ = session.stamp();
    (*pending_).source_size_ = size;
    (*pending_).bytes_ = bytes;
    (*pending_).path_ = session.path();
    (*pending_).parts_ = parts;
    (*pending_).storage_.resize(parts.size());
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const std::uint64_t length = parts[index].length;
        const std::uint64_t count = length / maximum_page + (length % maximum_page != 0);
        if (count > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("Snapshot metadata exceeds addressable storage.");
        (*pending_).storage_[index].reserve(static_cast<std::size_t>(count));
    }
    advance_empty_parts();
}
void SnapshotCapture::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != (*pending_).stamp_.identity ||
        current.revision != (*pending_).stamp_.revision || session.size() != (*pending_).source_size_)
        throw std::runtime_error("Snapshot source changed during capture.");
}
void SnapshotCapture::advance_empty_parts() {
    while (part_ < (*pending_).parts_.size() && part_copied_ == (*pending_).parts_[part_].length) {
        ++part_;
        part_copied_ = 0;
    }
    if (part_ == (*pending_).parts_.size())
        state_ = SnapshotState::complete;
}
void SnapshotCapture::step(const Session &session, const std::size_t budget) {
    if (state_ != SnapshotState::reading)
        throw std::runtime_error("Snapshot capture is not reading.");
    if (!budget || budget > maximum_page)
        throw std::runtime_error("Snapshot step budget must be 1..65536 bytes.");
    try {
        validate(session);
        const SnapshotRange range = (*pending_).parts_[part_];
        std::vector<std::string> &chunks = (*pending_).storage_[part_];
        if (chunks.empty() || chunks.back().size() == maximum_page) {
            std::string chunk{};
            chunk.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(maximum_page, range.length - part_copied_)));
            chunks.push_back(std::move(chunk));
        }
        std::string &chunk = chunks.back();
        const std::size_t amount = static_cast<std::size_t>(std::min<std::uint64_t>(
            range.length - part_copied_, std::min(budget, maximum_page - chunk.size())));
        const std::uint64_t offset = range.offset + part_copied_;
        const Page page = session.page(offset, amount);
        validate(session);
        if (page.offset != offset || page.next != offset + amount || page.bytes.size() != amount ||
            page.size != (*pending_).source_size_)
            throw std::runtime_error("Snapshot source returned inconsistent coverage.");
        chunk.append(page.bytes);
        part_copied_ += amount;
        copied_ += amount;
        advance_empty_parts();
    } catch (...) {
        pending_.reset();
        state_ = SnapshotState::failed;
        throw;
    }
}
void SnapshotCapture::cancel() noexcept {
    if (state_ == SnapshotState::reading || state_ == SnapshotState::complete) {
        pending_.reset();
        state_ = SnapshotState::cancelled;
    }
}
std::unique_ptr<const SourceSnapshot> SnapshotCapture::take(const Session &session) {
    if (state_ != SnapshotState::complete)
        throw std::runtime_error("Snapshot has no complete unpublished result.");
    try {
        validate(session);
    } catch (...) {
        pending_.reset();
        state_ = SnapshotState::failed;
        throw;
    }
    std::unique_ptr<const SourceSnapshot> result = std::move(pending_);
    state_ = SnapshotState::taken;
    return result;
}
SnapshotSlice SourceSnapshot::read(const std::size_t part, const std::uint64_t relative,
                                  const std::size_t budget) const {
    if (part >= parts_.size() || relative > parts_[part].length || !budget || budget > maximum_page)
        throw std::runtime_error("Snapshot read bounds are invalid.");
    const std::size_t amount = static_cast<std::size_t>(std::min<std::uint64_t>(budget, parts_[part].length - relative));
    SnapshotSlice result{parts_[part].offset + relative, {}};
    result.bytes.reserve(amount);
    std::size_t chunk = static_cast<std::size_t>(relative / maximum_page);
    std::size_t inside = static_cast<std::size_t>(relative % maximum_page);
    while (result.bytes.size() < amount) {
        const std::string &source = storage_[part][chunk];
        const std::size_t count = std::min(amount - result.bytes.size(), source.size() - inside);
        result.bytes.append(source, inside, count);
        ++chunk;
        inside = 0;
    }
    return result;
}
} // namespace swiftedit
