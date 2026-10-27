#pragma once
#include <filesystem>

namespace notepad {
// Resolves the running image, independently of cwd and argv[0].
[[nodiscard]] std::filesystem::path executable_path();
// Launches this exact absolute executable without document arguments or a shell.
// Returns after OS launch/exec acceptance, not after the new GUI becomes ready.
// Throws on launch failure. The independent process owns its subsequent lifetime.
void launch_window(const std::filesystem::path &executable);
struct NewWindow {
    std::filesystem::path executable{};
    void operator()() const { launch_window(executable); }
};
} // namespace notepad
