#pragma once
#include <ctime>
#include <string>

namespace notepad {
// Plain local calendar time, shared by GUI and terminal insertion commands.
// Locale-independent ASCII formatting; conversion failures are reported.
[[nodiscard]] std::string local_date_time(std::time_t instant);
[[nodiscard]] std::string current_date_time();
} // namespace notepad
