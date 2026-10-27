#include "session_search.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(const bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::optional<swiftedit::PagedSearchMatch> finish(swiftedit::SessionSearch &scan,
                                                const swiftedit::Session &session,
                                                const std::size_t budget = 4096) {
    std::size_t steps = 0;
    while (!scan.step(session, budget)) {
        ++steps;
        check(steps < 100000, "Streaming search did not finish within its fixture work bound");
    }
    const std::optional<swiftedit::PagedSearchMatch> result = scan.result(session);
    return result;
}
} // namespace
int main() {
    try {
        swiftedit::Session session{};
        const std::string content = std::string(8190, 'x') + "Ae\xcc\x81\r\nB" +
                                    std::string(9000, 'x') + "aab";
        session.replace_ranges({{0, 0}}, content, session.stamp());
        swiftedit::SessionSearch literal(session, swiftedit::SearchPattern("ae\xcc\x81\r\nb"));
        const std::optional<swiftedit::PagedSearchMatch> match = finish(literal, session);
        check(match && (*match).offset == 8190 && (*match).length == 7,
              "Literal match preserves Unicode and CRLF across a source read boundary");
        swiftedit::SearchPattern pattern("A?\r\nB");
        pattern.toggle(1);
        swiftedit::SessionSearch wildcard(session, std::move(pattern));
        const std::optional<swiftedit::PagedSearchMatch> wild = finish(wildcard, session, 1);
        check(wild && (*wild).offset == 8190 && (*wild).length == 7,
              "Wildcard consumes one complete combining grapheme with one-operation steps");
        swiftedit::SessionSearch overlap(session, swiftedit::SearchPattern("ab"), 8197);
        const std::optional<swiftedit::PagedSearchMatch> overlapping = finish(overlap, session);
        check(overlapping && (*overlapping).offset == content.size() - 2,
              "Overlapping candidates do not skip a later prefix");
        swiftedit::SessionSearch sensitive(session, swiftedit::SearchPattern("ae\xcc\x81\r\nb"), 0, true);
        check(!finish(sensitive, session), "Match-case mode does not fold source literals");
        swiftedit::SessionSearch after(session, swiftedit::SearchPattern("Ae\xcc\x81"), 8191);
        check(!finish(after, session), "Candidates before the requested starting byte are excluded");
        swiftedit::SessionSearch stale(session, swiftedit::SearchPattern("absent"));
        check(!stale.step(session), "First search step acquires bounded context only");
        session.replace_ranges({{0, 0}}, "changed", session.stamp());
        bool refused = false;
        try { static_cast<void>(stale.step(session)); }
        catch (const std::exception &) { refused = true; }
        check(refused, "Changed source invalidates pending search");
        swiftedit::Session long_cluster{};
        std::string cluster = "e";
        for (std::size_t index = 0; index < 6000; ++index)
            cluster += "\xcc\x81";
        long_cluster.replace_ranges({{0, 0}}, cluster + "Z", long_cluster.stamp());
        swiftedit::SearchPattern cluster_pattern("?Z");
        cluster_pattern.toggle(0);
        swiftedit::SessionSearch cluster_scan(long_cluster, std::move(cluster_pattern));
        const std::optional<swiftedit::PagedSearchMatch> cluster_match = finish(cluster_scan, long_cluster);
        check(cluster_match && (*cluster_match).length == cluster.size() + 1,
              "Adaptive context keeps a long combining grapheme atomic");
        swiftedit::Session oversized{};
        std::string too_long = "e";
        for (std::size_t index = 0; index < 33000; ++index)
            too_long += "\xcc\x81";
        oversized.replace_ranges({{0, 0}}, too_long + "Z", oversized.stamp());
        swiftedit::SessionSearch oversized_scan(oversized, swiftedit::SearchPattern("Z"));
        bool context_refused = false;
        try { static_cast<void>(finish(oversized_scan, oversized)); }
        catch (const std::exception &) { context_refused = true; }
        check(context_refused, "Excessive grapheme context is refused rather than silently skipped");
        swiftedit::SessionSearch eof_scan(session, swiftedit::SearchPattern("x"), session.size());
        check(eof_scan.step(session, 1) && !eof_scan.result(session),
              "Starting at EOF completes without rescanning the whole source");
        for (std::size_t source_bits = 0; source_bits < 64; ++source_bits) {
            std::string source{};
            for (std::size_t bit = 0; bit < 6; ++bit)
                source += source_bits & (std::size_t(1) << bit) ? 'a' : 'b';
            swiftedit::Session sample{};
            sample.replace_ranges({{0, 0}}, source, sample.stamp());
            for (std::size_t query_bits = 0; query_bits < 8; ++query_bits) {
                std::string query{};
                for (std::size_t bit = 0; bit < 3; ++bit)
                    query += query_bits & (std::size_t(1) << bit) ? 'a' : 'b';
                for (std::size_t flags = 0; flags < 8; ++flags) {
                    swiftedit::SearchPattern query_pattern(query);
                    for (std::size_t bit = 0; bit < 3; ++bit)
                        if (flags & (std::size_t(1) << bit))
                            query_pattern.toggle(bit);
                    const std::size_t start = source_bits % 7;
                    swiftedit::PatternScan reference(source, query_pattern, start, true);
                    swiftedit::SearchProgress expected{};
                    do { expected = reference.step(1); } while (!expected.complete && !expected.match);
                    swiftedit::SessionSearch actual(sample, std::move(query_pattern), start, true);
                    const std::optional<swiftedit::PagedSearchMatch> observed = finish(actual, sample, 1);
                    check(bool(observed) == bool(expected.match), "Streaming/reference wildcard presence differs");
                    if (observed)
                        check((*observed).offset == (*expected.match).offset &&
                                  (*observed).length == (*expected.match).length,
                              "Streaming/reference wildcard match range differs");
                }
            }
        }
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-session-search-" + std::to_string(test_process_id()));
        check(std::filesystem::create_directory(directory), "Owned search fixture directory already exists");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        swiftedit::Session malformed{};
        const std::filesystem::path invalid_path = directory / "invalid.txt";
        {
            std::ofstream file(invalid_path, std::ios::binary);
            file << std::string("a\xff", 2) + "b\x01";
            check(bool(file), "Cannot write invalid-byte search fixture");
        }
        malformed.open(invalid_path);
        swiftedit::SearchPattern invalid_wild("a?b");
        invalid_wild.toggle(1);
        swiftedit::SessionSearch invalid_scan(malformed, std::move(invalid_wild));
        const std::optional<swiftedit::PagedSearchMatch> invalid_match = finish(invalid_scan, malformed, 1);
        check(invalid_match && (*invalid_match).length == 3,
              "Wildcard treats an invalid source byte atomically without changing it");
        swiftedit::SessionSearch control(malformed, swiftedit::SearchPattern("\x01"));
        const std::optional<swiftedit::PagedSearchMatch> control_match = finish(control, malformed);
        check(control_match && (*control_match).offset == 3,
              "Metadata sanitization cannot create a false literal control match");
        const std::filesystem::path path = directory / "large.txt";
        {
            std::ofstream file(path, std::ios::binary);
            file.seekp(swiftedit::editable_limit - 3);
            file << "END";
            check(bool(file), "Cannot write owned search fixture");
        }
        swiftedit::Session large{};
        large.open(path);
        check(large.read_only(), "Search fixture enters paged read-only mode");
        swiftedit::SessionSearch end(large, swiftedit::SearchPattern("END"));
        const std::optional<swiftedit::PagedSearchMatch> last = finish(end, large);
        check(last && (*last).offset == swiftedit::editable_limit - 3 && (*last).length == 3 && !large.dirty(),
              "Streaming search reaches final bytes of an actual read-only file");
        std::cout << "Streaming session search passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
