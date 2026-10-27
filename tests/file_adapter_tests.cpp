#include "document.hpp"
#include <chrono>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
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
                ("swiftedit-save-" + stamp + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                directory = candidate;
                return;
            }
        }
        throw std::runtime_error("Cannot reserve fixture directory.");
    }
    ~Fixture() {
        std::error_code error{};
        std::filesystem::remove_all(directory, error);
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
};
bool inject_race = false;
} // namespace
namespace notepad {
void file_test_before_publish(const std::filesystem::path &path) {
    if (!inject_race)
        return;
    inject_race = false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "late external contents";
    if (!output)
        throw std::runtime_error("Cannot inject fixture race.");
}
} // namespace notepad
int main() {
    try {
        const Fixture fixture{};
        const std::filesystem::path path = fixture.directory / u8"résumé.txt";
        const notepad::FileSnapshot missing = notepad::read_file(path);
        check(!missing.exists, "Missing file is an absent snapshot");
        const std::string bytes("a\0\xff\r\n", 5);
        const notepad::FileSnapshot first = notepad::write_file(path, bytes, missing);
        check(first.exists && first.bytes == bytes, "New file preserves exact bytes");
        const notepad::FileSnapshot second = notepad::write_file(path, "next", first);
        check(second.bytes == "next", "Existing file replaced");
        bool stale_refused = false;
        try {
            static_cast<void>(notepad::write_file(path, "stale", first));
        } catch (const std::exception &) {
            stale_refused = true;
        }
        const notepad::FileSnapshot after_stale = notepad::read_file(path);
        check(stale_refused && after_stale == second, "Stale save preserves destination");
        bool collision_refused = false;
        try {
            static_cast<void>(notepad::write_file(path, "new", {}));
        } catch (const std::exception &) {
            collision_refused = true;
        }
        check(collision_refused, "Expected absence cannot overwrite existing file");
        bool directory_refused = false;
        try {
            static_cast<void>(notepad::read_file(fixture.directory));
        } catch (const std::exception &) {
            directory_refused = true;
        }
        check(directory_refused, "Directory is not a file snapshot");
        const std::filesystem::path absent = fixture.directory / "never-created.txt";
        bool oversize_refused = false;
        try {
            static_cast<void>(
                notepad::write_file(absent, std::string(notepad::maximum_bytes + 1, 'x'), {}));
        } catch (const std::exception &) {
            oversize_refused = true;
        }
        const bool created = std::filesystem::exists(absent);
        check(oversize_refused && !created, "Oversized save does not create destination");
#ifndef _WIN32
        const int permission_changed = chmod(path.c_str(), 0604);
        check(permission_changed == 0, "Set fixture mode");
#ifdef __APPLE__
        const int attribute_set = setxattr(path.c_str(), "user.swiftedit", "metadata", 8, 0, 0);
#else
        const int attribute_set = setxattr(path.c_str(), "user.swiftedit", "metadata", 8, 0);
#endif
        check(attribute_set == 0, "Set fixture extended attribute");
        const std::filesystem::file_time_type old_modified =
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
        std::filesystem::last_write_time(path, old_modified);
        const notepad::FileSnapshot metadata_source = notepad::read_file(path);
        const notepad::FileSnapshot metadata_saved =
            notepad::write_file(path, "metadata retained", metadata_source);
        check(metadata_saved.bytes == "metadata retained", "Metadata save completed");
        const std::filesystem::file_time_type new_modified = std::filesystem::last_write_time(path);
        check(new_modified > old_modified, "Edited file receives a new modification time");
        struct stat info{};
        const int inspected = stat(path.c_str(), &info);
        check(inspected == 0 && (info.st_mode & 0777) == 0604, "Permissions preserved");
        std::array<char, 8> attribute{};
#ifdef __APPLE__
        const ssize_t attribute_size =
            getxattr(path.c_str(), "user.swiftedit", attribute.data(), attribute.size(), 0, 0);
#else
        const ssize_t attribute_size =
            getxattr(path.c_str(), "user.swiftedit", attribute.data(), attribute.size());
#endif
        check(attribute_size == 8 &&
                  std::string_view(attribute.data(), attribute.size()) == "metadata",
              "Extended attribute preserved");
        const int writer = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        check(writer >= 0, "Open cooperating writer");
        const int locked = flock(writer, LOCK_EX | LOCK_NB);
        check(locked == 0, "Acquire cooperating writer lock");
        bool lock_refused = false;
        try {
            static_cast<void>(notepad::write_file(path, "locked", metadata_saved));
        } catch (const std::exception &) {
            lock_refused = true;
        }
        close(writer);
        check(lock_refused, "Cooperating writer prevents replacement");
        const int readonly_set = chmod(path.c_str(), 0400);
        check(readonly_set == 0, "Set read-only mode");
        bool readonly_refused = false;
        try {
            static_cast<void>(notepad::write_file(path, "readonly", metadata_saved));
        } catch (const std::exception &) {
            readonly_refused = true;
        }
        const int writable_set = chmod(path.c_str(), 0600);
        check(readonly_refused && writable_set == 0, "Read-only file not replaced");
        const std::filesystem::path link = fixture.directory / "alias";
        std::filesystem::create_symlink(path, link);
        bool link_refused = false;
        try {
            static_cast<void>(notepad::write_file(link, "alias", metadata_saved));
        } catch (const std::exception &) {
            link_refused = true;
        }
        check(link_refused, "Symlink save refused");
        inject_race = true;
        bool new_race_refused = false;
        try {
            static_cast<void>(notepad::write_file(absent, "ours", {}));
        } catch (const std::exception &) {
            new_race_refused = true;
        }
        const notepad::FileSnapshot winner = notepad::read_file(absent);
        check(new_race_refused && winner.bytes == "late external contents",
              "Late new-file collision never overwrites");
        const notepad::FileSnapshot race_source = notepad::read_file(path);
        inject_race = true;
        bool replace_race_refused = false;
        try {
            static_cast<void>(notepad::write_file(path, "our replacement", race_source));
        } catch (const std::exception &) {
            replace_race_refused = true;
        }
        bool recovered = false;
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator(fixture.directory)) {
            if (entry.path().filename().string().starts_with(".swiftedit-")) {
                const notepad::FileSnapshot retained = notepad::read_file(entry.path());
                recovered = retained.bytes == "late external contents";
            }
        }
        check(replace_race_refused && recovered,
              "Late displaced contents retained after uncertain replacement");
#endif
        std::cout << "File adapter tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
