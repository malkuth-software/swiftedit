#include "new_window.hpp"
#include "platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

void require(const bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void refused(const std::filesystem::path &image) {
    bool failed = false;
    try {
        notepad::launch_window(image);
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid launch must report an error");
}
int main(const int argc, char **) {
    try {
        const std::filesystem::path self = notepad::executable_path();
        require(self.is_absolute() && std::filesystem::is_regular_file(self),
                "Resolve actual executable independently of cwd");
        // The parent copies this dedicated helper into its unique fixture.
        // A no-argument child writes its own location then exits, with no GUI.
        if (argc == 1) {
            const std::filesystem::path staging = self.parent_path() / "child.pending";
            const std::filesystem::path done = self.parent_path() / "child.done";
            std::ofstream output(staging, std::ios::binary);
            const std::u8string bytes = self.u8string();
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.close();
            require(static_cast<bool>(output), "Child completion receipt");
            std::filesystem::rename(staging, done);
            return 0;
        }
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-window-" + std::to_string(test_process_id()));
        require(std::filesystem::create_directory(directory), "Own unique launch fixture");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code ignored{};
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const std::filesystem::path image = directory / std::filesystem::path(u8"Swift Edit & é.exe");
        std::filesystem::copy_file(self, image);
        refused("relative-program");
        refused(directory / "absent-executable");
        refused(directory);
        notepad::launch_window(image);
        const std::filesystem::path receipt = directory / "child.done";
        const std::chrono::steady_clock::time_point limit =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!std::filesystem::exists(receipt) && std::chrono::steady_clock::now() < limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(std::filesystem::exists(receipt), "Independent child completed");
        std::ifstream input(receipt, std::ios::binary);
        const std::string observed((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
        const std::u8string expected = std::filesystem::canonical(image).u8string();
        require(observed == std::string(reinterpret_cast<const char *>(expected.data()),
                                        expected.size()), "Exact path survived spaces and Unicode");
        // Windows can briefly retain its executable mapping after the receipt.
        std::error_code failure{};
        do {
            failure.clear();
            std::filesystem::remove(image, failure);
            if (failure)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (failure && std::chrono::steady_clock::now() < limit);
        require(!failure, "Child released executable after completing");
        std::cout << "Independent executable launch and failure reporting passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
