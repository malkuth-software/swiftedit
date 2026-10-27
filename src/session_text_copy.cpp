#include "session_text_copy.hpp"
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <array>
#endif
namespace swiftedit {
#ifndef _WIN32
// The worker must not receive terminal lifecycle signals. Block them around
// creation so it inherits the mask, then restore only the calling thread.
// This preserves the terminal's flag-check/pselect signal-delivery contract.
class PublicationSignalMask final {
public:
    PublicationSignalMask() {
        sigset_t blocked{};
        sigemptyset(&blocked);
        constexpr std::array<int, 5> signals{SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGTSTP};
        for (const int signal : signals)
            sigaddset(&blocked, signal);
        const int error = pthread_sigmask(SIG_BLOCK, &blocked, &previous_);
        if (error)
            throw std::runtime_error("Cannot establish publication worker signal mask.");
    }
    ~PublicationSignalMask() { pthread_sigmask(SIG_SETMASK, &previous_, nullptr); }
    PublicationSignalMask(const PublicationSignalMask &) = delete;
    PublicationSignalMask &operator=(const PublicationSignalMask &) = delete;
private:
    sigset_t previous_{};
};
#endif
class TextCopyPublication final {
public:
    explicit TextCopyPublication(std::unique_ptr<notepad::NewFileWriter> writer)
        : writer_(std::move(writer)) {
#ifndef _WIN32
        const PublicationSignalMask blocked{};
#endif
        worker_ = std::thread(run, this);
    }
    ~TextCopyPublication() {
        if (worker_.joinable())
            worker_.join();
    }
    bool ready(const std::chrono::milliseconds maximum_wait) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!complete_ && maximum_wait.count() > 0)
            completed_.wait_for(lock, maximum_wait, Completed{this});
        return complete_;
    }
    void finish() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!complete_)
                throw std::runtime_error("Text copy publication is still running.");
        }
        if (worker_.joinable())
            worker_.join();
        if (failure_)
            std::rethrow_exception(failure_);
    }
private:
    struct Completed {
        TextCopyPublication *owner{};
        bool operator()() const { return (*owner).complete_; }
    };
    static void run(TextCopyPublication *owner) noexcept {
        TextCopyPublication &work = *owner;
        std::exception_ptr failure{};
        try { (*work.writer_).publish(); }
        catch (...) { failure = std::current_exception(); }
        work.writer_.reset();
        {
            std::lock_guard<std::mutex> lock(work.mutex_);
            work.failure_ = failure;
            work.complete_ = true;
        }
        work.completed_.notify_one();
    }
    std::unique_ptr<notepad::NewFileWriter> writer_{};
    std::mutex mutex_{};
    std::condition_variable completed_{};
    std::exception_ptr failure_{};
    bool complete_{};
    // Started in the constructor body after all observed members are initialized.
    std::thread worker_{};
};
SessionTextCopy::~SessionTextCopy() = default;

SessionTextCopy::SessionTextCopy(const Session &session, const std::filesystem::path &path)
    : stamp_(session.stamp()), size_(session.size()),
      writer_(std::make_unique<notepad::NewFileWriter>(std::filesystem::absolute(path))) {}
void SessionTextCopy::validate(const Session &session) const {
    const DocumentStamp current = session.stamp();
    if (current.identity != stamp_.identity || current.revision != stamp_.revision || session.size() != size_)
        throw std::runtime_error("Text copy source changed; copy cancelled.");
}
bool SessionTextCopy::step(const Session &session, const std::size_t budget) {
    if (!budget || budget > maximum_page)
        throw std::runtime_error("Text copy read budget must be 1..65536 bytes.");
    if (state_ != TextCopyState::copying)
        throw std::runtime_error("Text copy is not accepting more source data.");
    try {
        validate(session);
        const Page source = session.page(offset_, budget);
        const bool final = source.next == size_;
        const TextCopyChunk output = stream_.append(source.bytes, final);
        (*writer_).append(output.bytes);
        offset_ = source.next;
        invalid_ += output.invalid_bytes;
        if (final)
            state_ = TextCopyState::ready;
        return final;
    } catch (...) {
        state_ = TextCopyState::failed;
        writer_.reset();
        throw;
    }
}
void SessionTextCopy::begin_publication(const Session &session) {
    if (state_ != TextCopyState::ready)
        throw std::runtime_error("Text copy is not ready to publish.");
    try {
        validate(session);
        static_cast<void>(session.page(size_, 1));
        publication_ = std::make_unique<TextCopyPublication>(std::move(writer_));
        state_ = TextCopyState::publishing;
    } catch (...) {
        state_ = TextCopyState::failed;
        writer_.reset();
        throw;
    }
}
bool SessionTextCopy::publication_ready(const std::chrono::milliseconds maximum_wait) {
    if (state_ != TextCopyState::publishing)
        throw std::runtime_error("Text copy publication has not started.");
    if (maximum_wait.count() < 0 || maximum_wait > std::chrono::milliseconds(1000))
        throw std::runtime_error("Text copy completion wait must be 0..1000 milliseconds.");
    return (*publication_).ready(maximum_wait);
}
void SessionTextCopy::finish_publication() {
    if (state_ != TextCopyState::publishing)
        throw std::runtime_error("Text copy publication has not started.");
    if (!(*publication_).ready(std::chrono::milliseconds(0)))
        throw std::runtime_error("Text copy publication is still running.");
    try {
        (*publication_).finish();
        publication_.reset();
        state_ = TextCopyState::published;
    } catch (...) {
        publication_.reset();
        state_ = TextCopyState::failed;
        throw;
    }
}
void SessionTextCopy::publish(const Session &session) {
    begin_publication(session);
    while (!publication_ready(std::chrono::milliseconds(1000))) {}
    finish_publication();
}
} // namespace swiftedit
