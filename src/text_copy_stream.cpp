#include "text_copy_stream.hpp"
#include "session.hpp"
#include <stdexcept>
namespace swiftedit {
TextCopyChunk TextCopyStream::append(const std::string_view bytes, const bool finish) {
    if (finished_)
        throw std::runtime_error("Text copy stream is already finished.");
    if (bytes.size() > maximum_page)
        throw std::runtime_error("Text copy chunks must not exceed 64 KiB.");
    std::string source = pending_;
    source.append(bytes);
    TextCopyChunk result{};
    result.bytes.reserve(source.size());
    std::size_t offset = 0;
    while (offset < source.size()) {
        const std::size_t length = utf8_sequence_length(source, offset);
        if (length) {
            result.bytes.append(source, offset, length);
            offset += length;
            continue;
        }
        const unsigned char first = static_cast<unsigned char>(source[offset]);
        const std::size_t expected = first >= 0xc2 && first <= 0xdf ? 2
                                   : first >= 0xe0 && first <= 0xef ? 3
                                   : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!finish && expected > source.size() - offset)
            break;
        result.bytes += ' ';
        ++result.invalid_bytes;
        ++offset;
    }
    std::string remainder = source.substr(offset);
    pending_.swap(remainder);
    finished_ = finish;
    return result;
}
} // namespace swiftedit
