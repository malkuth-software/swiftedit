#include "date_time.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace notepad {
std::string local_date_time(std::time_t instant) {
    std::tm local{};
#ifdef _WIN32
    const bool failed = localtime_s(&local, &instant) != 0;
#else
    const bool failed = localtime_r(&instant, &local) == nullptr;
#endif
    if (failed)
        throw std::runtime_error("Cannot determine local date and time.");
    std::ostringstream output{};
    output.imbue(std::locale::classic());
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    if (!output)
        throw std::runtime_error("Cannot format local date and time.");
    const std::string result = output.str();
    return result;
}
std::string current_date_time() {
    const std::time_t instant = std::time(nullptr);
    if (instant == static_cast<std::time_t>(-1))
        throw std::runtime_error("Cannot read the system clock.");
    const std::string result = local_date_time(instant);
    return result;
}
} // namespace notepad
