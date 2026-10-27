#include "csv.hpp"
#include "context.hpp"
#include "session.hpp"
#include "session_word_count.hpp"
#include "session_copy.hpp"
#include "session_search.hpp"
#include "session_text_copy.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

namespace {
std::uint64_t number(const std::string &s) {
    std::uint64_t v{};
    const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size())
        throw std::runtime_error("Expected unsigned integer.");
    return v;
}
void require_field_count(const std::vector<std::string> &fields, std::size_t expected) {
    if (fields.size() != expected)
        throw std::runtime_error("Incorrect field count; see command protocol.");
}
void field(std::string_view s) {
    const std::string escaped = swiftedit::escape_field(s);
    std::cout << '\t' << escaped;
}
void word_count_progress(const swiftedit::SessionWordCount &count) {
    if (count.state() == swiftedit::WordCountState::complete)
        std::cout << "word-count\t" << count.result() << '\n';
    else
        std::cout << "word-count-progress\t" << count.offset() << '\t' << count.size() << '\n';
}
void copy_progress(swiftedit::SessionCopy &copy, swiftedit::SourceClipboard &clipboard) {
    if (copy.state() == swiftedit::CopyState::complete) {
        clipboard = copy.take();
        std::cout << "copied\t" << clipboard.bytes().size() << '\n';
    } else
        std::cout << "copy-progress\t" << copy.copied() << '\t' << copy.length() << '\n';
}
void page(const swiftedit::Page &p) {
    std::cout << "page\t" << p.offset << '\t' << p.next << '\t' << p.size;
    field(p.bytes);
    std::cout << '\n';
}
void context_page(const swiftedit::ContextPage &context) {
    std::cout << "context\t" << context.stamp.identity.value << '\t' << context.stamp.revision.value
              << '\n';
    page(context.content);
    for (const swiftedit::BlankAnchor &anchor : context.blanks) {
        std::cout << "blank\t" << anchor.offset << '\t' << anchor.line;
        const std::string marker = swiftedit::blank_marker(anchor);
        field(marker);
        std::cout << '\n';
    }
}
std::filesystem::path path(const std::string &s) {
    const std::u8string utf8(reinterpret_cast<const char8_t *>(s.data()), s.size());
    const std::filesystem::path result(utf8);
    return result;
}
void previews(const std::vector<swiftedit::Preview> &list) {
    for (const swiftedit::Preview &p : list) {
        std::cout << "preview\t" << p.token.value << '\t' << p.revision.value << '\t' << p.offset
                  << '\t' << p.length << '\t' << p.inserted.size();
        field(p.before);
        field(p.removed.substr(0, 240));
        field(p.inserted.substr(0, 240));
        field(p.after);
        std::cout << '\n';
    }
}
std::vector<std::string> split(std::string_view s) {
    std::vector<std::string> f{};
    f.reserve(5);
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = s.find('\t', start);
        if (f.size() == 5)
            throw std::runtime_error("Too many request fields.");
        const std::size_t length = end == s.npos ? s.size() - start : end - start;
        const std::string_view encoded = s.substr(start, length);
        std::string decoded = swiftedit::unescape_field(encoded);
        f.push_back(std::move(decoded));
        if (end == s.npos)
            break;
        start = end + 1;
    }
    return f;
}
} // namespace
int main(int argc, char **argv) {
    if (argc > 1) {
        std::cout << "SwiftEdit command session v1\nRun without arguments and send tab-separated "
                     "escaped fields on stdin.\nSee docs/COMMAND_PROTOCOL.md. This process never "
                     "interprets shell commands.\n";
        return 0;
    }
    swiftedit::Session session{};
    swiftedit::ContextCursor context{};
    std::unique_ptr<swiftedit::SessionWordCount> counting{};
    std::unique_ptr<swiftedit::SessionCopy> copying{};
    std::unique_ptr<swiftedit::SessionSearch> searching{};
    std::unique_ptr<swiftedit::SessionTextCopy> text_copy{};
    swiftedit::SourceClipboard clipboard{};
    std::string line{};
    std::cout << "ready\tSwiftEdit\t1\n" << std::flush;
    while (true) {
        // Bound transport allocation before reading arbitrary input.
        line.clear();
        bool overflow = false;
        char c{};
        while (true) {
            std::cin.get(c);
            if (!std::cin || c == '\n')
                break;
            if (line.size() < 2 * swiftedit::editable_limit)
                line += c;
            else
                overflow = true;
        }
        if (!std::cin && line.empty() && !overflow)
            break;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        try {
            if (overflow)
                throw std::runtime_error("Request exceeds transport limit.");
            const std::vector<std::string> f = split(line);
            const std::string &cmd = f.front();
            if (cmd == "quit") {
                require_field_count(f, 1);
                std::cout << "ok\tquit\n" << std::flush;
                break;
            } else if (cmd == "info") {
                require_field_count(f, 1);
                std::cout << "info\t" << session.revision().value << '\t' << session.size() << '\t'
                          << session.dirty() << '\t' << session.read_only();
                const std::u8string p = session.path().u8string();
                field(std::string(reinterpret_cast<const char *>(p.data()), p.size()));
                std::cout << '\n';
            } else if (cmd == "search-start") {
                require_field_count(f, 5);
                swiftedit::SearchPattern pattern(f[1]);
                const std::uint64_t start = number(f[2]);
                const std::uint64_t match_case = number(f[3]);
                if (match_case > 1)
                    throw std::runtime_error("Search match-case must be 0 or 1.");
                const std::string &flags = f[4];
                if (!flags.empty() && flags.size() != pattern.slots().size())
                    throw std::runtime_error("Search flags must be empty or one 0/1 per query grapheme.");
                for (std::size_t index = 0; index < flags.size(); ++index) {
                    if (flags[index] == '1')
                        pattern.toggle(index);
                    else if (flags[index] != '0')
                        throw std::runtime_error("Search flags must contain only 0 or 1.");
                }
                searching = std::make_unique<swiftedit::SessionSearch>(session, std::move(pattern),
                                                                     start, match_case != 0);
                std::cout << "search-progress\t0\t" << session.size() << '\n';
            } else if (cmd == "search-next") {
                require_field_count(f, 2);
                const std::uint64_t budget = number(f[1]);
                if (!budget || budget > 65536)
                    throw std::runtime_error("Search work budget must be 1..65536 operations.");
                if (!searching)
                    throw std::runtime_error("Start a search first.");
                if ((*searching).step(session, static_cast<std::size_t>(budget))) {
                    const std::optional<swiftedit::PagedSearchMatch> match = (*searching).result(session);
                    if (match)
                        std::cout << "search-match\t" << (*match).offset << '\t' << (*match).length << '\n';
                    else
                        std::cout << "search-not-found\n";
                } else {
                    std::cout << "search-progress\t" << (*searching).offset() << '\t'
                              << (*searching).size() << '\n';
                }
            } else if (cmd == "search-cancel") {
                require_field_count(f, 1);
                searching.reset();
            } else if (cmd == "copy-start") {
                require_field_count(f, 3);
                const std::uint64_t offset = number(f[1]);
                const std::uint64_t length = number(f[2]);
                copying = std::make_unique<swiftedit::SessionCopy>(session, offset, length);
                copy_progress(*copying, clipboard);
            } else if (cmd == "copy-next") {
                require_field_count(f, 2);
                const std::uint64_t budget = number(f[1]);
                if (budget == 0 || budget > swiftedit::maximum_page)
                    throw std::runtime_error("Copy step budget must be 1..65536 bytes.");
                if (!copying)
                    throw std::runtime_error("Start a copy first.");
                (*copying).step(session, static_cast<std::size_t>(budget));
                copy_progress(*copying, clipboard);
            } else if (cmd == "copy-cancel") {
                require_field_count(f, 1);
                if (copying)
                    (*copying).cancel();
                copying.reset();
            } else if (cmd == "clipboard-page") {
                require_field_count(f, 3);
                const std::uint64_t offset = number(f[1]);
                const std::uint64_t budget = number(f[2]);
                const std::string_view bytes = clipboard.bytes();
                if (offset > bytes.size() || budget == 0 || budget > swiftedit::maximum_page)
                    throw std::runtime_error("Invalid clipboard page range or budget.");
                const std::string_view selected = bytes.substr(static_cast<std::size_t>(offset),
                                                              static_cast<std::size_t>(budget));
                std::cout << "clipboard\t" << offset << '\t' << offset + selected.size()
                          << '\t' << bytes.size();
                field(selected);
                std::cout << '\n';
            } else if (cmd == "word-count") {
                require_field_count(f, 1);
                if (session.read_only())
                    throw std::runtime_error(
                        "Use word-count-start and word-count-next for paged files.");
                const std::size_t words = notepad::word_count(session.text());
                std::cout << "word-count\t" << words << '\n';
            } else if (cmd == "word-count-start") {
                require_field_count(f, 1);
                counting = std::make_unique<swiftedit::SessionWordCount>(session);
                word_count_progress(*counting);
            } else if (cmd == "word-count-next") {
                require_field_count(f, 2);
                const std::uint64_t budget = number(f[1]);
                if (budget == 0 || budget > swiftedit::maximum_page)
                    throw std::runtime_error("Word count step budget must be 1..65536 bytes.");
                if (!counting)
                    throw std::runtime_error("Start a word count first.");
                (*counting).step(session, static_cast<std::size_t>(budget));
                word_count_progress(*counting);
            } else if (cmd == "word-count-cancel") {
                require_field_count(f, 1);
                if (counting)
                    (*counting).cancel();
                counting.reset();
            } else if (cmd == "open") {
                require_field_count(f, 2);
                if (session.dirty())
                    throw std::runtime_error("Unsaved edits: save or explicitly discard first.");
                const std::filesystem::path source = path(f[1]);
                session.open(source);
                context.reset(session);
                const swiftedit::ContextPage first_page = context.next(session);
                context_page(first_page);
            } else if (cmd == "discard") {
                require_field_count(f, 1);
                session.reset();
            } else if (cmd == "page") {
                require_field_count(f, 3);
                const std::uint64_t n = number(f[2]);
                if (n > swiftedit::maximum_page)
                    throw std::runtime_error("Page budget exceeds 65536 bytes.");
                const std::uint64_t offset = number(f[1]);
                const swiftedit::Page requested_page =
                    session.page(offset, static_cast<std::size_t>(n));
                page(requested_page);
            } else if (cmd == "context" || cmd == "context-next") {
                require_field_count(f, 2);
                const std::uint64_t budget = number(f[1]);
                if (budget == 0 || budget > swiftedit::maximum_page)
                    throw std::runtime_error(
                        "Context page budget must be between 1 and 65536 bytes.");
                if (cmd == "context")
                    context.reset(session);
                const swiftedit::ContextPage requested =
                    context.next(session, static_cast<std::size_t>(budget));
                context_page(requested);
            } else if (cmd == "find") {
                require_field_count(f, 2);
                const std::vector<std::size_t> matches = session.find(f[1]);
                for (const std::size_t at : matches) {
                    std::cout << "match\t" << at << '\n';
                    const std::size_t context_start = at > 80 ? at - 80 : 0;
                    const swiftedit::Page context = session.page(context_start, 240);
                    page(context);
                }
            } else if (cmd == "preview") {
                require_field_count(f, 5);
                const std::vector<swiftedit::Preview> candidates =
                    session.preview(f[1], f[2], f[3], f[4]);
                previews(candidates);
            } else if (cmd.starts_with("csv-")) {
                std::wstring ext = session.path().extension().wstring();
                for (wchar_t &c : ext)
                    if (c >= L'A' && c <= L'Z')
                        c += 32;
                if (ext != L".csv" || session.read_only())
                    throw std::runtime_error("CSV commands require an editable .csv file.");
                swiftedit::Csv csv(session.text());
                if (cmd == "csv-get") {
                    require_field_count(f, 2);
                    std::cout << "cell";
                    field(f[1]);
                    const swiftedit::CellAddress address = swiftedit::cell_address(f[1]);
                    const swiftedit::Cell &cell = csv.cell(address);
                    field(cell.value);
                    std::cout << '\n';
                } else if (cmd == "csv-value") {
                    require_field_count(f, 2);
                    const swiftedit::CellAddress address = swiftedit::cell_address(f[1]);
                    const swiftedit::Calculation result = swiftedit::calculate_cell(csv, address);
                    std::cout << "value";
                    field(result.result);
                    for (const swiftedit::CellAddress reference : result.references) {
                        const std::string name = swiftedit::cell_name(reference);
                        field(name);
                    }
                    std::cout << '\n';
                } else if (cmd == "csv-calculate") {
                    require_field_count(f, 2);
                    const swiftedit::Calculation result = swiftedit::calculate(csv, f[1]);
                    std::cout << "calculation";
                    field(result.result);
                    for (const swiftedit::CellAddress cell : result.references)
                        field(swiftedit::cell_name(cell));
                    std::cout << '\n';
                } else {
                    std::string next{};
                    if (cmd == "csv-set") {
                        require_field_count(f, 3);
                        const swiftedit::CellAddress address = swiftedit::cell_address(f[1]);
                        next = csv.set(address, f[2]);
                    } else if (cmd == "csv-convert-to-value") {
                        require_field_count(f, 2);
                        const swiftedit::CellAddress address = swiftedit::cell_address(f[1]);
                        next = swiftedit::convert_to_value(csv, address);
                    } else if (cmd == "csv-clear") {
                        require_field_count(f, 3);
                        const swiftedit::CellAddress first = swiftedit::cell_address(f[1]);
                        const swiftedit::CellAddress last = swiftedit::cell_address(f[2]);
                        next = csv.clear(first, last);
                    } else
                        throw std::runtime_error("Unknown CSV command.");
                    const std::vector<swiftedit::Preview> candidates =
                        session.preview("", session.text(), "", next);
                    previews(candidates);
                }
            } else if (cmd == "commit") {
                require_field_count(f, 3);
                const swiftedit::EditToken token{number(f[1])};
                const swiftedit::DocumentRevision revision{number(f[2])};
                session.commit(token, revision);
            } else if (cmd == "undo") {
                require_field_count(f, 1);
                const bool undone = session.undo();
                if (!undone)
                    throw std::runtime_error("At last-save undo boundary.");
            } else if (cmd == "redo") {
                require_field_count(f, 1);
                const bool redone = session.redo();
                if (!redone)
                    throw std::runtime_error("Nothing to redo.");
            } else if (cmd == "restore-opened") {
                require_field_count(f, 1);
                session.restore_opened();
            } else if (cmd == "save") {
                require_field_count(f, 1);
                session.save();
            } else if (cmd == "save-as") {
                require_field_count(f, 2);
                session.save_as(path(f[1]));
            } else if (cmd == "text-copy-start") {
                require_field_count(f, 2);
                text_copy = std::make_unique<swiftedit::SessionTextCopy>(session, path(f[1]));
                std::cout << "text-copy-progress\t0\t" << (*text_copy).size() << "\t0\n";
            } else if (cmd == "text-copy-next") {
                require_field_count(f, 2);
                const std::uint64_t budget = number(f[1]);
                if (!budget || budget > swiftedit::maximum_page)
                    throw std::runtime_error("Text copy read budget must be 1..65536 bytes.");
                if (!text_copy)
                    throw std::runtime_error("Start a text copy first.");
                const bool ready = (*text_copy).step(session, static_cast<std::size_t>(budget));
                std::cout << (ready ? "text-copy-ready" : "text-copy-progress") << '\t'
                          << (*text_copy).offset() << '\t' << (*text_copy).size() << '\t'
                          << (*text_copy).invalid_bytes() << '\n';
            } else if (cmd == "text-copy-publish") {
                require_field_count(f, 1);
                if (!text_copy)
                    throw std::runtime_error("Start a text copy first.");
                (*text_copy).publish(session);
                std::cout << "text-copy-saved\t" << (*text_copy).size() << '\t'
                          << (*text_copy).invalid_bytes() << '\n';
                text_copy.reset();
            } else if (cmd == "text-copy-cancel") {
                require_field_count(f, 1);
                text_copy.reset();
            } else if (cmd == "save-text-copy") {
                require_field_count(f, 2);
                session.save_text_copy(path(f[1]));
            } else
                throw std::runtime_error("Unknown command.");
            std::cout << "ok\t" << cmd << '\t' << session.revision().value << '\n';
        } catch (const std::exception &e) {
            std::cout << "error";
            field(e.what());
            std::cout << '\n';
        }
        std::cout << std::flush;
    }
}
