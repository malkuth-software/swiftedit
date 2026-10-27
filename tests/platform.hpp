#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
inline std::uint64_t test_process_id() {
#ifdef _WIN32
    const std::uint64_t result = GetCurrentProcessId();
#else
    const std::uint64_t result = static_cast<std::uint64_t>(getpid());
#endif
    return result;
}
inline void set_test_read_only(const std::filesystem::path &path, bool readonly) {
#ifdef _WIN32
    const DWORD attributes = readonly ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
    const bool failed = SetFileAttributesW(path.c_str(), attributes) == 0;
#else
    const bool failed = chmod(path.c_str(), readonly ? 0400 : 0600) != 0;
#endif
    if (failed)
        throw std::runtime_error("Cannot set fixture permissions.");
}
class TestWriter {
public:
    explicit TestWriter(const std::filesystem::path &path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot open writer fixture.");
#else
        descriptor_ = open(path.c_str(), O_WRONLY | O_CLOEXEC);
        if (descriptor_ < 0)
            throw std::runtime_error("Cannot open writer fixture.");
        if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("Cannot lock writer fixture.");
        }
#endif
    }
    ~TestWriter() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
#else
        if (descriptor_ >= 0)
            close(descriptor_);
#endif
    }
    TestWriter(const TestWriter &) = delete;
    TestWriter &operator=(const TestWriter &) = delete;

private:
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int descriptor_{-1};
#endif
};
