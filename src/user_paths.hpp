#pragma once
#include <filesystem>

namespace notepad {
// Resolves the user's home directory, independently of document or process cwd.
[[nodiscard]] std::filesystem::path user_home_directory();
} // namespace notepad
