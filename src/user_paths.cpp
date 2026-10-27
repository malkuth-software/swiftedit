#include "user_paths.hpp"
#include <cstdlib>
#include <stdexcept>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif

namespace notepad {
std::filesystem::path user_home_directory() {
    std::filesystem::path candidate{};
#ifdef _WIN32
    const wchar_t *environment = _wgetenv(L"USERPROFILE");
    if (environment && *environment)
        candidate = environment;
#else
    const char *environment = std::getenv("HOME");
    if (environment && *environment)
        candidate = environment;
    else {
        // Called synchronously during UI setup; copy the account record before
        // another account lookup can invalidate its borrowed storage.
        const passwd *account = getpwuid(getuid());
        if (account && (*account).pw_dir)
            candidate = (*account).pw_dir;
    }
#endif
    if (candidate.empty() || !candidate.is_absolute())
        throw std::runtime_error("Cannot determine the user's home directory.");
    const std::filesystem::path result = std::filesystem::canonical(candidate);
    if (!std::filesystem::is_directory(result))
        throw std::runtime_error("The user's home location is not a directory.");
    return result;
}
} // namespace notepad
