#include "terminal_app.hpp"
#include "terminal_input.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t interrupted = 0;
volatile std::sig_atomic_t resized = 0;
void terminal_signal(const int signal) {
    if (signal == SIGWINCH)
        resized = 1;
    else
        interrupted = signal;
}
// Publication workers block these signals. Block them in the input thread across the flag
// check; pselect atomically restores the old mask while it waits. Destruction
// restores the caller's mask on every return and exception.
class SignalBlock final {
public:
    explicit SignalBlock(const std::array<int, 5> &signals) {
        sigset_t blocked{};
        sigemptyset(&blocked);
        for (const int signal : signals)
            sigaddset(&blocked, signal);
        if (sigprocmask(SIG_BLOCK, &blocked, &previous_) != 0)
            throw std::runtime_error("Cannot protect terminal signal wait.");
    }
    SignalBlock(const SignalBlock &) = delete;
    SignalBlock &operator=(const SignalBlock &) = delete;
    ~SignalBlock() { sigprocmask(SIG_SETMASK, &previous_, nullptr); }
    const sigset_t &previous() const { return previous_; }
private:
    sigset_t previous_{};
};
enum class InputWake { input, timeout, signal };
class PosixConsole final : public swiftedit::TerminalConsole {
public:
    PosixConsole() = default;
    PosixConsole(const PosixConsole &) = delete;
    PosixConsole &operator=(const PosixConsole &) = delete;
    ~PosixConsole() { close(); }
    void start() override {
        if (active_ || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
            throw std::runtime_error("Run swiftedit-terminal in an interactive terminal.");
        if (tcgetattr(STDIN_FILENO, &original_) != 0)
            throw std::runtime_error("Cannot read terminal mode.");
        interrupted = 0;
        resized = 0;
        try {
            struct sigaction action{};
            action.sa_handler = terminal_signal;
            sigemptyset(&action.sa_mask);
            for (const int signal : signals_) {
                if (sigaction(signal, &action, &previous_[installed_]) != 0)
                    throw std::runtime_error("Cannot install terminal signal handler.");
                ++installed_;
            }
            struct termios raw = original_;
            cfmakeraw(&raw);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
                throw std::runtime_error("Cannot enter terminal raw mode.");
            active_ = true;
            write("\x1b[?1049h\x1b[?2004h");
        } catch (...) {
            close();
            throw;
        }
    }
    void close() noexcept {
        if (active_) {
            constexpr std::string_view restore = "\x1b[0m\x1b[?25h\x1b[?2004l\x1b[?1049l";
            std::size_t offset = 0;
            while (offset < restore.size()) {
                const ssize_t written = ::write(STDOUT_FILENO, restore.data() + offset,
                                                 restore.size() - offset);
                if (written < 0 && errno == EINTR)
                    continue;
                if (written <= 0)
                    break;
                offset += static_cast<std::size_t>(written);
            }
            while (tcsetattr(STDIN_FILENO, TCSANOW, &original_) != 0 && errno == EINTR) {}
            active_ = false;
        }
        while (installed_) {
            --installed_;
            sigaction(signals_[installed_], &previous_[installed_], nullptr);
        }
    }
    swiftedit::TerminalSize size() const override {
        struct winsize dimensions{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &dimensions) != 0)
            throw std::runtime_error("Cannot read terminal dimensions.");
        const swiftedit::TerminalSize result{
            dimensions.ws_col ? dimensions.ws_col : std::size_t(80),
            dimensions.ws_row ? dimensions.ws_row : std::size_t(24)};
        return result;
    }
    void write(const std::string_view text) const override {
        std::size_t offset = 0;
        while (offset < text.size()) {
            if (interrupted)
                throw std::runtime_error("Terminal interrupted; restoring terminal state.");
            const ssize_t written = ::write(STDOUT_FILENO, text.data() + offset, text.size() - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                throw std::runtime_error("Cannot write terminal output.");
            offset += static_cast<std::size_t>(written);
        }
    }
    bool input_ready() const override {
        // Raw bytes are not necessarily a complete event. Decode a bounded
        // available batch without entering the blocking read path during work.
        for (std::size_t consumed = 0; consumed < bytes_.size(); ++consumed) {
            if (pending_ || decoder_.ready() || interrupted || resized || input_closed_)
                return true;
            if (offset_ == count_) {
                struct pollfd descriptor{STDIN_FILENO, POLLIN, 0};
                const int result = poll(&descriptor, 1, 0);
                if (result < 0) {
                    if (errno == EINTR)
                        return interrupted || resized;
                    throw std::runtime_error("Cannot inspect terminal input.");
                }
                if (!result) {
                    if (escape_deadline_ && std::chrono::steady_clock::now() >= *escape_deadline_) {
                        decoder_.expire();
                        escape_deadline_.reset();
                    }
                    return decoder_.ready();
                }
                const ssize_t received = ::read(STDIN_FILENO, bytes_.data(), bytes_.size());
                if (received < 0 && errno == EINTR)
                    return interrupted || resized;
                if (received <= 0) {
                    input_closed_ = true;
                    return true;
                }
                offset_ = 0;
                count_ = static_cast<std::size_t>(received);
            }
            feed_byte(bytes_[offset_]);
            ++offset_;
        }
        return decoder_.ready();
    }
    swiftedit::TerminalInput read() const override {
        if (pending_) {
            swiftedit::TerminalInput result = std::move(*pending_);
            pending_.reset();
            return result;
        }
        for (;;) {
            if (interrupted)
                throw std::runtime_error("Terminal interrupted; restoring terminal state.");
            if (input_closed_)
                throw std::runtime_error("Terminal input closed; restoring terminal state.");
            if (resized) {
                resized = 0;
                swiftedit::TerminalInput result{};
                result.resized = true;
                return result;
            }
            if (decoder_.ready()) {
                swiftedit::TerminalInput result = decoder_.take();
                return result;
            }
            if (offset_ < count_) {
                feed_byte(bytes_[offset_]);
                ++offset_;
                continue;
            }
            const InputWake wake = wait_input();
            if (wake == InputWake::signal)
                continue;
            if (wake == InputWake::timeout) {
                decoder_.expire();
                escape_deadline_.reset();
                continue;
            }
            const ssize_t received = ::read(STDIN_FILENO, bytes_.data(), bytes_.size());
            if (received < 0 && errno == EINTR)
                continue;
            if (received <= 0)
                throw std::runtime_error("Terminal input closed; restoring terminal state.");
            offset_ = 0;
            count_ = static_cast<std::size_t>(received);
        }
    }
    void allow_wrap_cancel(const bool enabled) override { wrap_cancel_ = enabled; }
    bool cancel_requested() override {
        if (!wrap_cancel_ || !input_ready())
            return false;
        if (!pending_)
            pending_ = read();
        if ((*pending_).pressed && (*pending_).key == swiftedit::terminal_key::escape) {
            pending_.reset();
            return true;
        }
        return false;
    }
private:
    void feed_byte(const unsigned char byte) const {
        decoder_.feed(byte);
        if (decoder_.escape_pending())
            escape_deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        else
            escape_deadline_.reset();
    }
    InputWake wait_input() const {
        const SignalBlock blocked(signals_);
        if (interrupted || resized)
            return InputWake::signal;
        fd_set input{};
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);
        struct timespec escape_timeout{};
        const struct timespec *timeout = nullptr;
        if (escape_deadline_) {
            const std::chrono::steady_clock::duration remaining =
                *escape_deadline_ - std::chrono::steady_clock::now();
            if (remaining > std::chrono::steady_clock::duration::zero()) {
                const std::chrono::nanoseconds nanoseconds =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(remaining);
                escape_timeout.tv_sec = static_cast<time_t>(nanoseconds.count() / 1000000000);
                escape_timeout.tv_nsec = static_cast<long>(nanoseconds.count() % 1000000000);
            }
            timeout = &escape_timeout;
        }
        const int ready = pselect(STDIN_FILENO + 1, &input, nullptr, nullptr,
                                  timeout, &blocked.previous());
        if (ready < 0 && errno == EINTR)
            return InputWake::signal;
        if (ready < 0)
            throw std::runtime_error("Cannot wait for terminal input.");
        const InputWake result = ready ? InputWake::input : InputWake::timeout;
        return result;
    }
    static constexpr std::array<int, 5> signals_{SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGTSTP};
    std::array<struct sigaction, 5> previous_{};
    std::size_t installed_{};
    struct termios original_{};
    bool active_{}, wrap_cancel_{true};
    mutable swiftedit::TerminalInputDecoder decoder_{};
    mutable std::optional<std::chrono::steady_clock::time_point> escape_deadline_{};
    mutable bool input_closed_{};
    mutable std::array<unsigned char, 4096> bytes_{};
    mutable std::size_t offset_{}, count_{};
    mutable std::optional<swiftedit::TerminalInput> pending_{};
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
            std::cout << "SwiftEdit interactive terminal\nUsage: swiftedit-terminal [file]\n"
                         "Ctrl+S Save, Ctrl+R Open, Ctrl+X Exit; Ctrl+Z/Y Undo/Redo; "
                         "Ctrl+K Cut, Ctrl+U Paste, Ctrl+T Save Text Copy.\n"
                         "Ctrl+W Find, Ctrl+H Replace; F2 Wrap, F5 Date/Time, F6 Word Count.\n"
                         "Shift+arrows select when supported by the terminal; clipboard is private.\n";
            const int exit_code = argc > 2 ? 1 : 0;
            return exit_code;
        }
        PosixConsole console{};
        swiftedit::Terminal terminal(console);
        const std::filesystem::path path = argc == 2 ? std::filesystem::path(argv[1]) :
                                                     std::filesystem::path{};
        const int result = terminal.run(path);
        return result;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
