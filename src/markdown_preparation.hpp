#pragma once
#include "markdown.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace swiftedit {
// Presentation-only blocks: span text already contains inert display labels.
// Never use this model as document content or for source/edit mapping.
struct PreparedMarkdown {
    std::vector<MarkdownBlock> blocks{};
};
// Owns one worker, one replaceable source and one completed model. The caller
// serializes request/cancel/take. No source borrow, UI callback or native handle
// crosses the worker boundary. Destruction cancels and joins; library work
// between cancellation checks and model cleanup have no hard timing bound.
class MarkdownPreparation final {
public:
    MarkdownPreparation();
    ~MarkdownPreparation();
    MarkdownPreparation(const MarkdownPreparation &) = delete;
    MarkdownPreparation &operator=(const MarkdownPreparation &) = delete;
    void request(std::string source);
    void cancel();
    std::optional<PreparedMarkdown> take();
private:
    struct Request {
        std::string source{};
        std::stop_token cancellation{};
    };
    struct Ready {
        MarkdownPreparation *owner{};
        bool operator()() const;
    };
    static void run(MarkdownPreparation *) noexcept;
    std::mutex mutex_{};
    std::condition_variable changed_{};
    std::optional<Request> pending_{};
    std::optional<PreparedMarkdown> completed_{};
    std::exception_ptr failure_{};
    std::stop_source cancellation_{};
    bool stopping_{};
    std::thread worker_{};
};
}
