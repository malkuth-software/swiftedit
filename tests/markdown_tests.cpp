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

        // The stx_rtf2md dialect: attribute spans and block attribute lines.
        const std::vector<swiftedit::MarkdownBlock> dialect = swiftedit::parse_markdown(
            "{align=center}\nCentred [under]{u color=#c00000} and [**big**]{size=+2 bg=#00ff00}\n\n"
            "{align=right}\n## Right\n\nH[2]{sub}O, x[2]{sup}, [a [b]{size=-1} c]{u}\n\n"
            "\\[not]\\{u} and `[code]{u}` and [x]{zz}\n");
        check(dialect.size() == 4, "An attribute-only paragraph joins the block after it");
        const swiftedit::MarkdownBlock &centred = dialect[0];
        check(centred.alignment == swiftedit::MarkdownAlignment::center && centred.spans[0].text == "Centred ",
              "A leading attribute line aligns its paragraph and is removed");
        check(centred.spans[1].text == "under" && centred.spans[1].underline &&
                  centred.spans[1].colour == swiftedit::MarkdownColour{0xc0, 0, 0},
              "A span underlines and colours its text");
        check(centred.spans[3].text == "big" && centred.spans[3].bold && centred.spans[3].size_step == 2 &&
                  centred.spans[3].background == swiftedit::MarkdownColour{0, 0xff, 0},
              "Emphasis inside a span keeps both");
        check(dialect[1].kind == swiftedit::MarkdownKind::heading &&
                  dialect[1].alignment == swiftedit::MarkdownAlignment::right,
              "An attribute line before a heading aligns the heading");
        const swiftedit::MarkdownBlock &scripts = dialect[2];
        bool subscript = false, superscript = false, nested = false, outer = false;
        for (const swiftedit::MarkdownSpan &span : scripts.spans) {
            subscript = subscript || (span.text == "2" && span.subscript);
            superscript = superscript || (span.text == "2" && span.superscript);
            nested = nested || (span.text == "b" && span.underline && span.size_step == -1);
            outer = outer || (span.text == "a " && span.underline && span.size_step == 0);
        }
        check(subscript && superscript && nested && outer, "Super/subscripts and nested spans compose");
        std::string literal{};
        for (const swiftedit::MarkdownSpan &span : dialect[3].spans)
            literal += span.text;
        check(literal == "[not]{u} and [code]{u} and [x]{zz}",
              "Escaped brackets, code and unknown attributes stay literal");
        std::cout << "Markdown inert presentation tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
