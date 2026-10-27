#include "paged_file.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path directory{};
    Fixture() {
        const std::chrono::system_clock::duration elapsed =
            std::chrono::system_clock::now().time_since_epoch();
        const std::string stamp = std::to_string(elapsed.count());
        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                ("swiftedit-page-" + stamp + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                directory = candidate;
                return;
            }
        }
        throw std::runtime_error("Cannot reserve unique fixture directory.");
    }
    ~Fixture() {
        std::error_code error{};
        std::filesystem::remove_all(directory, error);
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
};
} // namespace
int main() {
    try {
        const Fixture fixture{};
        const std::filesystem::path path = fixture.directory / "bytes.txt";
        const std::string bytes("a\0\xff\r\nb", 6);
        {
            std::ofstream output(path, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            check(static_cast<bool>(output), "Write byte fixture");
        }
        {
            const swiftedit::PagedFile file(path);
            const swiftedit::Page full = file.page(0, 64);
            check(full.bytes == bytes && full.next == bytes.size(), "Exact byte read");
            const swiftedit::Page middle = file.page(1, 3);
            check(middle.bytes == bytes.substr(1, 3) && middle.next == 4, "Bounded offset read");
            const swiftedit::Page end = file.page(bytes.size(), 1);
            check(end.bytes.empty() && end.next == bytes.size(), "EOF read");
            bool range_refused = false;
            try {
                static_cast<void>(file.page(bytes.size() + 1, 1));
            } catch (const std::exception &) {
                range_refused = true;
            }
            check(range_refused, "Out of range refused");
            bool budget_refused = false;
            try {
                static_cast<void>(file.page(0, swiftedit::maximum_page + 1));
            } catch (const std::exception &) {
                budget_refused = true;
            }
            check(budget_refused, "Oversized read refused");
#ifndef _WIN32
            {
                std::ofstream output(path, std::ios::binary | std::ios::app);
                output << "changed";
            }
            bool change_refused = false;
            try {
                static_cast<void>(file.page(0, 1));
            } catch (const std::exception &) {
                change_refused = true;
            }
            check(change_refused, "POSIX source mutation invalidates retained pages");
#endif
        }
        bool directory_refused = false;
        try {
            const swiftedit::PagedFile invalid(fixture.directory);
        } catch (const std::exception &) {
            directory_refused = true;
        }
        check(directory_refused, "Directory refused");
#ifndef _WIN32
        const std::filesystem::path link = fixture.directory / "link.txt";
        std::filesystem::create_symlink(path, link);
        bool link_refused = false;
        try {
            const swiftedit::PagedFile invalid(link);
        } catch (const std::exception &) {
            link_refused = true;
        }
        check(link_refused, "Final symlink refused");
        const std::filesystem::path fifo = fixture.directory / "pipe";
        const int pipe_created = mkfifo(fifo.c_str(), 0600);
        check(pipe_created == 0, "Create FIFO fixture");
        bool fifo_refused = false;
        try {
            const swiftedit::PagedFile invalid(fifo);
        } catch (const std::exception &) {
            fifo_refused = true;
        }
        check(fifo_refused, "FIFO refused without waiting for a writer");
        {
            const swiftedit::PagedFile same_size(path);
            const std::filesystem::file_time_type original_time =
                std::filesystem::last_write_time(path);
            {
                std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
                output << 'Q';
                check(static_cast<bool>(output), "Rewrite one byte");
            }
            std::filesystem::last_write_time(path, original_time + std::chrono::seconds(2));
            bool same_size_refused = false;
            try {
                static_cast<void>(same_size.page(0, 1));
            } catch (const std::exception &) {
                same_size_refused = true;
            }
            check(same_size_refused, "Same-size timestamp change invalidates retained pages");
        }
#endif
        const std::filesystem::path large_path = fixture.directory / "large.txt";
        {
            std::ofstream output(large_path, std::ios::binary);
            output.seekp(32 * 1024 * 1024);
            output << 'Z';
            check(static_cast<bool>(output), "Large fixture written");
        }
        const swiftedit::PagedFile large(large_path);
        const swiftedit::Page tail = large.page(large.size() - 1, 1024);
        check(tail.bytes == "Z", "Large source stays bounded at EOF");
        std::cout << "Paged file tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
