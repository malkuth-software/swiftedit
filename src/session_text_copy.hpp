#pragma once
#include "session.hpp"
#include "text_copy_stream.hpp"
#include "new_file_writer.hpp"
#include <chrono>
namespace swiftedit {
class TextCopyPublication;
enum class TextCopyState { copying, ready, publishing, published, failed };
// One bounded read/conversion/write per step. Publication is separate so callers
// can accept cancellation before installing the completed file. Source/session
// must stay current until publication starts. Destruction cancels a prepared
// temporary, or joins a publication already in progress.
class SessionTextCopy final {
public:
    SessionTextCopy(const Session &, const std::filesystem::path &);
    ~SessionTextCopy();
    SessionTextCopy(const SessionTextCopy &) = delete;
    SessionTextCopy &operator=(const SessionTextCopy &) = delete;
    [[nodiscard]] bool step(const Session &, std::size_t budget = 65536);
    void publish(const Session &);
    // After begin_publication, cancellation is no longer accepted. The worker
    // owns only the prepared writer; it never accesses Session or UI state.
    void begin_publication(const Session &);
    [[nodiscard]] bool publication_ready(std::chrono::milliseconds maximum_wait);
    void finish_publication();
    [[nodiscard]] TextCopyState state() const { return state_; }
    [[nodiscard]] std::uint64_t offset() const { return offset_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }
    [[nodiscard]] std::uint64_t invalid_bytes() const { return invalid_; }
private:
    void validate(const Session &) const;
    DocumentStamp stamp_{};
    std::uint64_t size_{}, offset_{}, invalid_{};
    TextCopyStream stream_{};
    std::unique_ptr<notepad::NewFileWriter> writer_{};
    std::unique_ptr<TextCopyPublication> publication_{};
    TextCopyState state_{TextCopyState::copying};
};
} // namespace swiftedit
