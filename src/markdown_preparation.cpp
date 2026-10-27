#include "markdown_preparation.hpp"
#include "display.hpp"
#include <stdexcept>
#include <utility>

namespace swiftedit {
namespace {
// Retain the existing scalar-aligned display chunks. Only their rendered text
// is used; source-range mappings never cross these artificial boundaries.
std::string inert_text(std::string_view source, std::stop_token cancellation,
                       std::size_t &retained_bytes) {
    constexpr std::size_t text_limit = 32 * 1024 * 1024;
    std::string result{};
    std::size_t begin = 0;
    while (begin < source.size()) {
        if (cancellation.stop_requested())
            throw MarkdownCancelled{};
        std::size_t end = begin;
        while (end < source.size() && end - begin < 60000) {
            const std::size_t length = utf8_sequence_length(source, end);
            end += length ? length : 1;
        }
        const DisplayPage page(source.substr(begin, end - begin));
        if (cancellation.stop_requested())
            throw MarkdownCancelled{};
        if (page.text().size() > text_limit - retained_bytes)
            throw std::runtime_error("Markdown preparation exceeds display text budget.");
        result += page.text();
        retained_bytes += page.text().size();
        begin = end;
    }
    return result;
}
}

MarkdownPreparation::MarkdownPreparation() : worker_(run, this) {}
MarkdownPreparation::~MarkdownPreparation() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        cancellation_.request_stop();
        pending_.reset();
    }
    changed_.notify_one();
    if (worker_.joinable())
        worker_.join();
}
void MarkdownPreparation::request(std::string source) {
    std::stop_source next{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.request_stop();
        cancellation_ = std::move(next);
        pending_ = Request{std::move(source), cancellation_.get_token()};
        completed_.reset();
        failure_ = {};
    }
    changed_.notify_one();
}
void MarkdownPreparation::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.request_stop();
        pending_.reset();
        completed_.reset();
        failure_ = {};
    }
    changed_.notify_one();
}
std::optional<PreparedMarkdown> MarkdownPreparation::take() {
    std::optional<PreparedMarkdown> result{};
    std::exception_ptr failure{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = std::move(completed_);
        completed_.reset();
        failure = std::exchange(failure_, {});
    }
    if (failure)
        std::rethrow_exception(failure);
    return result;
}
bool MarkdownPreparation::Ready::operator()() const {
    const bool ready = (*owner).stopping_ || (*owner).pending_.has_value();
    return ready;
}
void MarkdownPreparation::run(MarkdownPreparation *owner) noexcept {
    MarkdownPreparation &work = *owner;
    for (;;) {
        Request request{};
        {
            std::unique_lock<std::mutex> lock(work.mutex_);
            work.changed_.wait(lock, Ready{owner});
            if (work.stopping_)
                return;
            request = std::move(*work.pending_);
            work.pending_.reset();
        }
        try {
            PreparedMarkdown prepared{parse_markdown(request.source, request.cancellation)};
            std::size_t retained_bytes = 0;
            for (MarkdownBlock &block : prepared.blocks) {
                if (request.cancellation.stop_requested())
                    throw MarkdownCancelled{};
                for (MarkdownSpan &span : block.spans)
                    span.text = inert_text(span.text, request.cancellation, retained_bytes);
            }
            std::lock_guard<std::mutex> lock(work.mutex_);
            if (!work.stopping_ && !request.cancellation.stop_requested())
                work.completed_ = std::move(prepared);
        } catch (const MarkdownCancelled &) {
            // A cancelled request has no completion or error to publish.
        } catch (...) {
            std::lock_guard<std::mutex> lock(work.mutex_);
            if (!work.stopping_ && !request.cancellation.stop_requested())
                work.failure_ = std::current_exception();
        }
    }
}
}
