#include "decoded_paged_file.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace swiftedit {
namespace {
std::size_t complete_utf8_prefix(const std::string_view bytes) {
    if (bytes.empty())
        return 0;
    std::size_t start = bytes.size() - 1;
    while (start > 0 && (static_cast<unsigned char>(bytes[start]) & 0xc0U) == 0x80U)
        --start;
    const unsigned char lead = static_cast<unsigned char>(bytes[start]);
    std::size_t length = 1;
    if (lead >= 0xc2U && lead <= 0xdfU) length = 2;
    else if (lead >= 0xe0U && lead <= 0xefU) length = 3;
    else if (lead >= 0xf0U && lead <= 0xf4U) length = 4;
    // Only an incomplete candidate scalar is deferred. Malformed complete bytes
    // stay in the prefix so the strict decoder rejects them on this step.
    if (length > bytes.size() - start)
        return start;
    return bytes.size();
}
std::uint32_t last_utf16_unit(const std::string_view bytes, const bool little) {
    const std::uint32_t first = static_cast<unsigned char>(bytes[bytes.size() - 2]);
    const std::uint32_t second = static_cast<unsigned char>(bytes.back());
    const std::uint32_t unit = little ? first | (second << 8) : (first << 8) | second;
    return unit;
}
}
DecodedPagedFile::DecodedPagedFile(const std::filesystem::path &source)
    : path_(std::filesystem::absolute(source)), file_(std::make_unique<PagedFile>(path_)) {
    encoded_size_ = (*file_).size();
    const Page header = (*file_).page(0, 4);
    const std::string_view bytes(header.bytes);
    if (bytes.starts_with(std::string_view("\xff\xfe\0\0", 4)) ||
        bytes.starts_with(std::string_view("\0\0\xfe\xff", 4)))
        throw std::runtime_error("UTF-32 is not supported. No document was changed.");
    if (bytes.starts_with("\xef\xbb\xbf")) {
        encoding_ = notepad::Encoding::utf8_bom;
        encoded_offset_ = 3;
    } else if (bytes.starts_with("\xff\xfe")) {
        encoding_ = notepad::Encoding::utf16_le;
        encoded_offset_ = 2;
    } else if (bytes.starts_with("\xfe\xff")) {
        encoding_ = notepad::Encoding::utf16_be;
        encoded_offset_ = 2;
    }
}
DecodedPagedFile::Block DecodedPagedFile::decode_block(const std::uint64_t offset) const {
    const Page raw = (*file_).page(offset, maximum_page);
    std::size_t count = raw.bytes.size();
    const bool utf16 = encoding_ == notepad::Encoding::utf16_le || encoding_ == notepad::Encoding::utf16_be;
    const bool little = encoding_ == notepad::Encoding::utf16_le;
    if (raw.next < encoded_size_) {
        if (utf16) {
            count -= count % 2;
            const std::string_view units(raw.bytes.data(), count);
            const std::uint32_t last = last_utf16_unit(units, little);
            if (last >= 0xd800U && last <= 0xdbffU)
                count -= 2;
        } else
            count = complete_utf8_prefix(raw.bytes);
    }
    if (count == 0 && offset < encoded_size_)
        throw std::runtime_error("Decoded source made no progress.");
    // Force the selected codec for every block. The UTF-16 sentinel prevents
    // an interior NUL from resembling a UTF-32 signature; it is removed below.
    std::string framed{};
    framed.reserve(count + 4);
    if (utf16) {
        framed.append(little ? "\xff\xfe" : "\xfe\xff", 2);
        framed.append(little ? std::string_view("x\0", 2) : std::string_view("\0x", 2));
    } else
        framed.append("\xef\xbb\xbf", 3);
    framed.append(raw.bytes.data(), count);
    notepad::Decoded decoded = notepad::decode(framed, notepad::TextControls::preserve);
    if (utf16)
        decoded.text.erase(0, 1);
    Block result{offset + count, std::move(decoded.text)};
    return result;
}
void DecodedPagedFile::step() {
    if (state_ != DecodedFileState::preparing)
        return;
    try {
        if (encoded_offset_ == encoded_size_) {
            state_ = DecodedFileState::ready;
            return;
        }
        Block block = decode_block(encoded_offset_);
        if (block.text.size() > std::numeric_limits<std::uint64_t>::max() - logical_size_)
            throw std::runtime_error("Decoded source size overflow.");
        checkpoints_.push_back({encoded_offset_, logical_size_});
        encoded_offset_ = block.next;
        logical_size_ += block.text.size();
        if (encoded_offset_ == encoded_size_)
            state_ = DecodedFileState::ready;
    } catch (...) {
        cancel();
        state_ = DecodedFileState::failed;
        throw;
    }
}
void DecodedPagedFile::cancel() noexcept {
    file_.reset();
    std::vector<Checkpoint> empty{};
    checkpoints_.swap(empty);
    state_ = DecodedFileState::cancelled;
}
Page DecodedPagedFile::page(const std::uint64_t offset, const std::size_t budget) const {
    if (state_ != DecodedFileState::ready || !file_)
        throw std::runtime_error("Decoded source is not ready.");
    if (budget == 0 || budget > maximum_page || offset > logical_size_)
        throw std::runtime_error("Invalid decoded page range.");
    Page result{offset, offset, logical_size_, {}};
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(budget, logical_size_ - offset));
    result.bytes.reserve(count);
    if (count == 0) {
        const Page checked = (*file_).page(encoded_size_, 1);
        static_cast<void>(checked);
        return result;
    }
    // Upper bound finds the last checkpoint at or before the logical cursor.
    std::size_t first = 0;
    std::size_t last = checkpoints_.size();
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (checkpoints_[middle].logical <= offset) first = middle + 1;
        else last = middle;
    }
    if (first == 0)
        throw std::runtime_error("Decoded source has no covering checkpoint.");
    std::uint64_t encoded = checkpoints_[first - 1].encoded;
    std::uint64_t logical = checkpoints_[first - 1].logical;
    while (result.bytes.size() < count) {
        const Block block = decode_block(encoded);
        const std::size_t skip = static_cast<std::size_t>(result.next - logical);
        if (skip > block.text.size())
            throw std::runtime_error("Decoded source checkpoint changed.");
        const std::size_t take = std::min(count - result.bytes.size(), block.text.size() - skip);
        if (take == 0)
            throw std::runtime_error("Decoded source page made no progress.");
        result.bytes.append(block.text, skip, take);
        encoded = block.next;
        logical += block.text.size();
        result.next += take;
    }
    return result;
}
}
