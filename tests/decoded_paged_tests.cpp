#include "decoded_paged_file.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace se = swiftedit;
void check(const bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void write(const std::filesystem::path &path, const std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    check(static_cast<bool>(file), "Fixture write failed");
}
void verify_codec(const std::filesystem::path &path, const notepad::Encoding encoding) {
    std::string source(65535, 'a');
    source.append("\xf0\x9f\x98\x80");
    source.append(65532, 'b');
    source.append("e\xcc\x81\r\n");
    source.append("\0\x01", 2);
    source.append("\xef\xbb\xbf");
    source.append(65536, 'c');
    const std::string encoded = notepad::encode(source, encoding, notepad::TextControls::preserve);
    write(path, encoded);
    se::DecodedPagedFile file(path);
    bool refused = false;
    try { const se::Page premature = file.page(0, 1); static_cast<void>(premature); }
    catch (const std::runtime_error &) { refused = true; }
    check(refused, "Preparing source cannot expose partial text");
    while (file.state() == se::DecodedFileState::preparing) {
        const std::uint64_t before = file.scanned();
        file.step();
        check(file.scanned() - before <= se::maximum_page, "Preparation step stays within encoded read budget");
    }
    check(file.state() == se::DecodedFileState::ready && file.size() == source.size() &&
          file.encoded_size() == encoded.size() && file.encoding() == encoding, "Decoded size and codec match");
    for (std::size_t offset = 0; offset < source.size(); offset += 4093) {
        const se::Page page = file.page(offset, se::maximum_page);
        check(page.bytes == source.substr(offset, se::maximum_page) && page.offset == offset &&
              page.next == offset + page.bytes.size() && page.size == source.size(),
              "Random logical pages match original Unicode across encoded checkpoints");
    }
    for (std::size_t offset = 65530; offset < 65543; ++offset) {
        const se::Page page = file.page(offset, 1);
        check(page.bytes == source.substr(offset, 1), "Byte transport may split a decoded scalar without losing bytes");
    }
    const se::Page eof = file.page(source.size(), 1);
    check(eof.bytes.empty() && eof.next == source.size(), "Decoded EOF is exact");
}
void verify_failure(const std::filesystem::path &path) {
    std::string malformed(65536, 'a');
    malformed.append("\xf0\x9f", 2);
    write(path, malformed);
    se::DecodedPagedFile file(path);
    file.step();
    check(file.state() == se::DecodedFileState::preparing, "Valid prefix does not publish truncated suffix");
    bool refused = false;
    try { file.step(); }
    catch (const std::runtime_error &) { refused = true; }
    check(refused && file.state() == se::DecodedFileState::failed, "Truncated scalar fails and retires source");
    refused = false;
    try { const se::Page failed = file.page(0, 1); static_cast<void>(failed); }
    catch (const std::runtime_error &) { refused = true; }
    check(refused, "Failed source cannot publish its valid prefix");
    // Failed preparation has closed its deny-write-sharing handle on Windows.
    write(path, std::string(131072, 'x'));
    se::DecodedPagedFile cancelled(path);
    cancelled.step();
    cancelled.cancel();
    cancelled.step();
    check(cancelled.state() == se::DecodedFileState::cancelled, "Cancelled preparation cannot resume");
    write(path, "");
    se::DecodedPagedFile empty(path);
    empty.step();
    check(empty.state() == se::DecodedFileState::ready && empty.size() == 0 && empty.page(0, 1).bytes.empty(),
          "Empty decoded file needs no invented checkpoint");
}
void verify_malformed_utf16(const std::filesystem::path &path) {
    const std::array<std::string, 4> malformed{
        std::string("\xff\xfex\0\x00\xd8", 6),
        std::string("\xfe\xff\0x\xdc\0", 6),
        std::string("\xff\xfex\0z", 5),
        std::string("\xfe\xff\0x\xd8\0\0y", 8)};
    for (const std::string &bytes : malformed) {
        write(path, bytes);
        se::DecodedPagedFile file(path);
        bool refused = false;
        try { file.step(); }
        catch (const std::runtime_error &) { refused = true; }
        check(refused && file.state() == se::DecodedFileState::failed,
              "Odd UTF-16, isolated or unpaired surrogates never publish");
    }
}
int main() {
    try {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-decoded-pages-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(std::filesystem::create_directory(directory), "Unique fixture directory required");
        struct Cleanup {
            std::filesystem::path path{};
            ~Cleanup() { std::error_code error{}; std::filesystem::remove_all(path, error); }
        } cleanup{directory};
        const std::array<notepad::Encoding, 4> encodings{notepad::Encoding::utf8, notepad::Encoding::utf8_bom,
            notepad::Encoding::utf16_le, notepad::Encoding::utf16_be};
        for (const notepad::Encoding encoding : encodings)
            verify_codec(directory / "source", encoding);
        verify_failure(directory / "failure");
        verify_malformed_utf16(directory / "utf16-failure");
        std::cout << "Decoded paging tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
