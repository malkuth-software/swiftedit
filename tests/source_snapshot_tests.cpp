#include "source_snapshot.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(const bool value, const char *const message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("swiftedit-source-snapshot-" + std::to_string(test_process_id()));
    Fixture() { require(std::filesystem::create_directory(directory), "Reserve unique snapshot fixtures"); }
    ~Fixture() {
        std::error_code ignored{};
        std::filesystem::remove_all(directory, ignored);
    }
    std::filesystem::path write(const std::string &name, const std::string &bytes) const {
        const std::filesystem::path path = directory / name;
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(static_cast<bool>(output), "Write exact snapshot fixture");
        return path;
    }
};
void finish(swiftedit::SnapshotCapture &capture, const swiftedit::Session &session,
            const std::size_t budget) {
    while (capture.state() == swiftedit::SnapshotState::reading) {
        const std::uint64_t before = capture.copied();
        capture.step(session, budget);
        require(capture.copied() > before && capture.copied() - before <= budget,
                "Snapshot steps advance within the requested byte budget");
    }
}
void check_parts(const Fixture &fixture) {
    const std::string source = std::string("a\r\n\0\xff" "b\tc\n", 9);
    const std::filesystem::path path = fixture.write("parts.txt", source);
    for (const std::size_t budget : {1U, 2U, 7U, 65536U}) {
        swiftedit::Session session{};
        session.open(path);
        const swiftedit::DocumentStamp stamp = session.stamp();
        const std::vector<swiftedit::SnapshotRange> ranges = {{0, 3}, {3, 0}, {4, 4}};
        swiftedit::SnapshotCapture capture(session, ranges, {7, 3, 2});
        finish(capture, session, budget);
        std::unique_ptr<const swiftedit::SourceSnapshot> snapshot = capture.take(session);
        require(capture.state() == swiftedit::SnapshotState::taken && (*snapshot).bytes() == 7 &&
                    (*snapshot).stamp().identity == stamp.identity && (*snapshot).path() == session.path(),
                "Snapshot publishes complete source provenance with explicit part boundaries");
        session.reset();
        for (std::size_t part = 0; part < ranges.size(); ++part) {
            std::string collected{};
            const swiftedit::SnapshotRange range = ranges[part];
            for (std::uint64_t offset = 0; offset < range.length;) {
                const swiftedit::SnapshotSlice page = (*snapshot).read(part, offset, 2);
                require(page.source_offset == range.offset + offset && !page.bytes.empty(),
                        "Snapshot read keeps absolute source mapping after Session close");
                collected += page.bytes;
                offset += page.bytes.size();
            }
            require(collected == source.substr(static_cast<std::size_t>(range.offset),
                                              static_cast<std::size_t>(range.length)),
                    "Snapshot retains malformed bytes, controls and original endings without joining parts");
            require((*snapshot).read(part, range.length).bytes.empty(), "Part EOF is stable");
        }
    }
}
void check_refusals(const Fixture &fixture) {
    swiftedit::Session session{};
    session.open(fixture.write("refusals.txt", "abcdef"));
    for (const swiftedit::SnapshotLimits limits : {
             swiftedit::SnapshotLimits{5, 1, 1}, {6, 0, 1}, {6, 1, 0}}) {
        bool refused = false;
        try {
            const swiftedit::SnapshotCapture invalid(session, {{0, 6}}, limits);
        } catch (const std::runtime_error &) { refused = true; }
        require(refused, "Every source and metadata quota is checked before capture");
    }
    for (const std::vector<swiftedit::SnapshotRange> &ranges :
         std::vector<std::vector<swiftedit::SnapshotRange>>{{{0, 4}, {3, 1}}, {{7, 0}}, {{5, 2}}, {}}) {
        bool refused = false;
        try {
            const swiftedit::SnapshotCapture invalid(session, ranges, {100, 10, 10});
        } catch (const std::runtime_error &) { refused = true; }
        require(refused, "Snapshot refuses overlapping or out-of-source ranges");
    }
    swiftedit::SnapshotCapture pending(session, {{0, 6}}, {6, 1, 1});
    bool refused = false;
    try { static_cast<void>(pending.take(session)); }
    catch (const std::runtime_error &) { refused = true; }
    require(refused, "Partial snapshot cannot escape");
    for (const std::size_t budget : {0U, 65537U}) {
        refused = false;
        try { pending.step(session, budget); }
        catch (const std::runtime_error &) { refused = true; }
        require(refused && pending.copied() == 0 && pending.state() == swiftedit::SnapshotState::reading,
                "Invalid capture budget preserves the pending task");
    }
    pending.step(session, 1);
    session.replace_ranges({{0, 1}}, "z", session.stamp());
    refused = false;
    try { pending.step(session, 1); }
    catch (const std::runtime_error &) { refused = true; }
    require(refused && pending.state() == swiftedit::SnapshotState::failed,
            "Revision change discards partial capture");
    swiftedit::SnapshotCapture cancelled(session, {{0, 6}}, {6, 1, 1});
    cancelled.step(session, 1);
    cancelled.cancel();
    require(cancelled.state() == swiftedit::SnapshotState::cancelled, "Cancellation releases unpublished storage");
    swiftedit::SnapshotCapture completed(session, {{0, 6}}, {6, 1, 1});
    finish(completed, session, 2);
    session.reset();
    refused = false;
    try { static_cast<void>(completed.take(session)); }
    catch (const std::runtime_error &) { refused = true; }
    require(refused && completed.state() == swiftedit::SnapshotState::failed,
            "Stale publication releases private storage and fails the capture");
    swiftedit::SnapshotCapture empty(session, {{0, 0}}, {0, 1, 0});
    std::unique_ptr<const swiftedit::SourceSnapshot> empty_snapshot = empty.take(session);
    require((*empty_snapshot).read(0, 0).bytes.empty(), "Empty source is a complete immutable snapshot");
    for (const std::size_t budget : {0U, 65537U}) {
        refused = false;
        try { static_cast<void>((*empty_snapshot).read(0, 0, budget)); }
        catch (const std::runtime_error &) { refused = true; }
        require(refused, "Snapshot read budget is bounded");
    }
    refused = false;
    try { static_cast<void>((*empty_snapshot).read(1, 0)); }
    catch (const std::runtime_error &) { refused = true; }
    require(refused, "Snapshot cannot read beyond its explicit part list");
}
void check_paged(const Fixture &fixture) {
    const std::filesystem::path path = fixture.directory / "large.txt";
    {
        const std::string chunk(65536, 'x');
        std::ofstream output(path, std::ios::binary);
        for (std::size_t index = 0; index < 256; ++index)
            output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        output << "tail";
        output.close();
        require(static_cast<bool>(output), "Write actual paged snapshot fixture");
    }
    swiftedit::Session session{};
    session.open(path);
    require(session.read_only(), "Large snapshot uses the actual paged source adapter");
    const std::uint64_t size = session.size();
    swiftedit::SnapshotCapture capture(session, {{0, size}}, {size, 1, 257});
    {
        swiftedit::SnapshotCapture cancelled(session, {{0, size}}, {size, 1, 257});
        cancelled.step(session);
        cancelled.cancel();
        require(cancelled.state() == swiftedit::SnapshotState::cancelled && !session.dirty(),
                "Large capture cancellation preserves the source");
    }
    finish(capture, session, 8192);
    std::unique_ptr<const swiftedit::SourceSnapshot> snapshot = capture.take(session);
    session.reset();
    fixture.write("large.txt", "replaced after capture");
    require((*snapshot).read(0, 65530, 20).bytes == std::string(20, 'x') &&
                (*snapshot).read(0, size - 4, 64).bytes == "tail",
            "Completed snapshot owns cross-chunk bytes independently of later file changes");
#ifndef _WIN32
    // POSIX cannot deny an uncooperative writer. The paged adapter must detect
    // its metadata change and capture must discard all previously copied bytes.
    {
        std::ofstream output(path, std::ios::binary);
        output.seekp(swiftedit::editable_limit - 1);
        output.put('z');
    }
    session.open(path);
    swiftedit::SnapshotCapture raced(session, {{0, session.size()}}, {session.size(), 1, 256});
    raced.step(session);
    fixture.write("large.txt", "external replacement during capture");
    bool refused = false;
    try { raced.step(session); }
    catch (const std::runtime_error &) { refused = true; }
    require(refused && raced.state() == swiftedit::SnapshotState::failed,
            "Paged read failure discards partial snapshot without publication");
#endif
}
} // namespace
int main() {
    try {
        const Fixture fixture{};
        check_parts(fixture);
        check_refusals(fixture);
        check_paged(fixture);
        std::cout << "Owned source snapshots preserve parts, bytes, provenance and publication authority.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
