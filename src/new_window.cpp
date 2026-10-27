#include "new_window.hpp"
#include <climits>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#else
#include <sys/syscall.h>
#endif
#endif

namespace notepad {
std::filesystem::path executable_path() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD count = GetModuleFileNameW(nullptr, buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
        if (!count)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Cannot locate SwiftEdit executable");
        if (count < buffer.size())
            return std::filesystem::canonical(std::wstring(buffer.data(), count));
        if (buffer.size() >= 32768)
            throw std::runtime_error("SwiftEdit executable path is too long.");
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 1024;
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            throw std::runtime_error("Cannot locate SwiftEdit bundle executable.");
    }
    return std::filesystem::canonical(buffer.data());
#else
    return std::filesystem::canonical("/proc/self/exe");
#endif
}

#ifndef _WIN32
namespace {
struct Descriptor {
    int value{-1};
    explicit Descriptor(const int descriptor) : value(descriptor) {}
    ~Descriptor() { if (value >= 0) close(value); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    void reset() {
        if (value >= 0)
            close(value);
        value = -1;
    }
};
// Only async-signal-safe operations are used in the child, including failures.
// No C++ cleanup, allocation, logging or GUI operation runs after fork.
[[noreturn]] void child_failure(const int pipe, const int error) {
    const char *bytes = reinterpret_cast<const char *>(&error);
    std::size_t sent = 0;
    while (sent < sizeof(error)) {
        const ssize_t count = write(pipe, bytes + sent, sizeof(error) - sent);
        if (count > 0)
            sent += static_cast<std::size_t>(count);
        else if (count < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    _exit(127);
}
}
#endif

void launch_window(const std::filesystem::path &executable) {
    if (!executable.is_absolute() || executable.empty())
        throw std::runtime_error("New Window requires an absolute executable path.");
#ifdef _WIN32
    const std::wstring image = executable.native();
    if (image.find(L'\0') != std::wstring::npos || image.find(L'"') != std::wstring::npos)
        throw std::runtime_error("Invalid executable path.");
    std::wstring command = L"\"" + image + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Cannot open a new SwiftEdit window");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    const std::string image = executable.native();
    if (image.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid executable path.");
    char *arguments[] = {const_cast<char *>(image.c_str()), nullptr};
    const char *image_path = image.c_str();
    struct rlimit limits{};
    if (getrlimit(RLIMIT_NOFILE, &limits) != 0)
        throw std::runtime_error("Cannot establish a safe descriptor bound for New Window.");
    int descriptor_limit = 0;
#ifdef __APPLE__
    // Darwin commonly reports an infinite hard rlimit. Its kernel per-process
    // maximum supplies the finite descriptor-table bound in that case.
    std::size_t limit_size = sizeof(descriptor_limit);
    if (sysctlbyname("kern.maxfilesperproc", &descriptor_limit, &limit_size, nullptr, 0) != 0 ||
        limit_size != sizeof(descriptor_limit) || descriptor_limit <= 0)
        throw std::runtime_error("Cannot read the system descriptor bound for New Window.");
#else
    if (limits.rlim_max == RLIM_INFINITY || limits.rlim_max > static_cast<rlim_t>(INT_MAX))
        throw std::runtime_error("Cannot establish a finite descriptor bound for New Window.");
    descriptor_limit = static_cast<int>(limits.rlim_max);
#endif
    const Descriptor null_device(open("/dev/null", O_RDWR | O_CLOEXEC));
    if (null_device.value < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot prepare new window streams");
    int raw_pipe[2] = {-1, -1};
    if (pipe(raw_pipe) != 0)
        throw std::system_error(errno, std::generic_category(), "Cannot prepare new window status");
    const Descriptor read_pipe(raw_pipe[0]);
    Descriptor write_pipe(raw_pipe[1]);
    // Keep the status writer above the standard streams even if one was closed.
    Descriptor status_pipe(fcntl(write_pipe.value, F_DUPFD_CLOEXEC, 3));
    if (status_pipe.value < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot prepare new window status");
    const pid_t child = fork();
    if (child < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot start new window");
    if (child == 0) {
        for (int stream = 0; stream < 3; ++stream) {
            if (dup2(null_device.value, stream) < 0 || fcntl(stream, F_SETFD, 0) < 0)
                child_failure(status_pipe.value, errno);
        }
        if (dup2(status_pipe.value, 3) < 0 || fcntl(3, F_SETFD, FD_CLOEXEC) < 0)
            child_failure(status_pipe.value, errno);
        bool closed = false;
#ifdef __linux__
        closed = syscall(SYS_close_range, 4U, UINT_MAX, 0U) == 0;
#endif
        if (!closed) {
            for (int descriptor = 4; descriptor < descriptor_limit; ++descriptor)
                close(descriptor);
        }
        if (setsid() < 0)
            child_failure(3, errno);
        const pid_t detached = fork();
        if (detached < 0)
            child_failure(3, errno);
        if (detached > 0)
            _exit(0);
        execv(image_path, arguments);
        child_failure(3, errno);
    }
    // Close every parent writer before waiting for exec's close-on-exec EOF.
    write_pipe.reset();
    status_pipe.reset();
    int status = 0;
    pid_t waited = -1;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot reap new window launcher");
    int failure = 0;
    std::size_t received = 0;
    char *bytes = reinterpret_cast<char *>(&failure);
    while (received < sizeof(failure)) {
        const ssize_t count = read(read_pipe.value, bytes + received, sizeof(failure) - received);
        if (count > 0)
            received += static_cast<std::size_t>(count);
        else if (count == 0)
            break;
        else if (errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "Cannot read new window status");
    }
    if (received == sizeof(failure))
        throw std::system_error(failure, std::generic_category(), "Cannot open a new SwiftEdit window");
    if (received || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("New window launcher ended without a complete status.");
#endif
}
} // namespace notepad
