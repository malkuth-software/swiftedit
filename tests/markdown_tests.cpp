#include "markdown.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::stop_source cancelled{};
        cancelled.request_stop();
        for (const std::string sample : {std::string{}, std::string("# Preserved\r\n"), std::string("\xff")}) {
            std::vector<swiftedit::MarkdownBlock> output{};
            bool stopped = false;
            try {
                output = swiftedit::parse_markdown(sample, cancelled.get_token());
            } catch (const swiftedit::MarkdownCancelled &) {
                stopped = true;
            }
            check(stopped && output.empty(), "Cancellation is typed and publishes no presentation");
        }
        const std::string source =
            "# Heading\n\nA **bold** and *soft* [link](https://example.com).\n\n"
            "- [x] Done\n- [ ] Later\n\n> Quoted\n\n"
            "```cpp\na < b;\n```\n\n"
            "| A | B |\n|---|---|\n| 1 | 2 |\n\n"
            "<script>alert(1)</script> &amp; &#233;\n";
        const std::vector<swiftedit::MarkdownBlock> blocks = swiftedit::parse_markdown(source);
        bool heading = false, bold = false, italic = false, link = false, task = false;
        bool quoted = false, code = false, table = false, html_text = false, entity = false;
        for (const swiftedit::MarkdownBlock &block : blocks) {
            heading = heading || block.kind == swiftedit::MarkdownKind::heading;
            quoted = quoted || block.quoted;
            code = code || block.kind == swiftedit::MarkdownKind::code;
            table =
                table || (block.kind == swiftedit::MarkdownKind::table_row && block.columns == 2);
            for (const swiftedit::MarkdownSpan &span : block.spans) {
                bold = bold || span.bold;
                italic = italic || span.italic;
                link = link || span.url == "https://example.com";
                task = task || span.text == "[x] ";
                html_text = html_text || span.text.find("<script>") != std::string::npos;
                entity = entity || span.text == "\xc3\xa9";
            }
        }
        check(heading && bold && italic && link && task && quoted && code && table,
              "Common blocks, styled spans, task lists, links and tables parsed");
        check(html_text && entity, "HTML is inert text and entities decode");
        const std::vector<swiftedit::MarkdownBlock> destinations =
            swiftedit::parse_markdown(
                "[decoded](https://example.com/?a=1&amp;b=&#xE9;) [empty]()\n");
        bool decoded_destination = false;
        bool empty_destination = false;
        for (const swiftedit::MarkdownBlock &block : destinations) {
            for (const swiftedit::MarkdownSpan &span : block.spans) {
                if (span.text == "decoded")
                    decoded_destination = span.url == "https://example.com/?a=1&b=\xc3\xa9";
                if (span.text == "empty")
                    empty_destination = span.url.empty();
            }
        }
        check(decoded_destination && empty_destination,
              "Link destinations decode entities and preserve empty destinations");
        const std::vector<swiftedit::MarkdownBlock> image =
            swiftedit::parse_markdown("![alt](file:///private/image.svg)");
        check(!image.empty(), "Image is represented by inert alternate text");
        std::string repeated_links = "[";
        for (std::size_t index = 0; index < 600; ++index)
            repeated_links += "*label* ";
        repeated_links += "](https://example.com/";
        repeated_links.append(60000, 'a');
        repeated_links += ')';
        bool refused_link_storage = false;
        try {
            static_cast<void>(swiftedit::parse_markdown(repeated_links));
        } catch (const std::runtime_error &failure) {
            refused_link_storage =
                std::string_view(failure.what()) == "Markdown exceeds display storage budget.";
        }
        check(refused_link_storage, "URL copies across styled spans count against retained storage");
        std::cout << "Markdown inert presentation tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
