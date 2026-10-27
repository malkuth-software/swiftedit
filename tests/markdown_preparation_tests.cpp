#include "markdown_preparation.hpp"
#include "display.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<swiftedit::MarkdownBlock> finish(swiftedit::MarkdownPreparation &work) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        std::optional<swiftedit::PreparedMarkdown> result = work.take();
        if (result) {
            std::vector<swiftedit::MarkdownBlock> blocks = std::move((*result).blocks);
            return blocks;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Markdown preparation timeout.");
        std::this_thread::yield();
    }
}
void verify_current(swiftedit::MarkdownPreparation &work) {
    const std::vector<swiftedit::MarkdownBlock> blocks = finish(work);
    require(blocks.size() == 1 && blocks[0].spans.size() == 1 &&
            blocks[0].spans[0].text == "Current", "Only the current owned source can publish");
    require(!work.take(), "A Markdown model is consumed once");
}
}
int main() {
    try {
        swiftedit::MarkdownPreparation work{};
        {
            std::string source = "# Current";
            work.request(std::move(source));
        }
        verify_current(work);
        const std::string controls = "plain \xE2\x80\xAE hidden \x1b end";
        work.request(controls);
        const std::vector<swiftedit::MarkdownBlock> display = finish(work);
        const swiftedit::DisplayPage expected(controls);
        require(display.size() == 1 && display[0].spans.size() == 1 &&
                display[0].spans[0].text == expected.text(),
                "Worker prepares inert control labels before publication");
        const std::vector<swiftedit::MarkdownBlock> raw = swiftedit::parse_markdown(controls);
        require(raw[0].spans[0].text == controls, "Parsing retains raw presentation text");
        std::string chunked(59999, 'x');
        chunked += "\xF0\x9F\x98\x80";
        chunked.append(10000, 'y');
        work.request(chunked);
        const std::vector<swiftedit::MarkdownBlock> chunks = finish(work);
        require(chunks.size() == 1 && chunks[0].spans[0].text == chunked,
                "Display preparation preserves multibyte scalars across chunk limits");
        const std::string large(4 * 1024 * 1024, 'x');
        for (std::size_t iteration = 0; iteration < 32; ++iteration) {
            work.request(large);
            work.cancel();
            work.request("# Current");
            verify_current(work);
        }
        work.request("\xff");
        bool failed = false;
        try {
            static_cast<void>(finish(work));
        } catch (const std::runtime_error &failure) {
            failed = std::string_view(failure.what()).find("valid UTF-8") != std::string_view::npos;
        }
        require(failed, "Worker transfers a parse failure without a model");
        work.request("# Current");
        verify_current(work);
        work.request("");
        require(finish(work).empty(), "Empty Markdown is a completed empty model");
        work.request(large);
        work.cancel();
        require(!work.take(), "Cancellation discards pending or completed models");
        {
            swiftedit::MarkdownPreparation closing{};
            closing.request(large);
        }
        std::cout << "Markdown worker ownership, replacement, errors and close passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
