#include "terminal_app.hpp"
#include <algorithm>
#include <iostream>
#ifdef SWIFTEDIT_TERMINAL_SMOKE
#include <fstream>
#endif
#include <stdexcept>
#include <windows.h>

namespace {
std::wstring utf16(std::string_view source) {
    if (source.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                                           static_cast<int>(source.size()), nullptr, 0);
    if (!length)
        throw std::runtime_error("Terminal output is not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                             static_cast<int>(source.size()), result.data(), length))
        throw std::runtime_error("Cannot encode terminal output.");
    return result;
}
class Console final : public swiftedit::TerminalConsole {
public:
    Console() = default;
    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;
    ~Console() { close(); }
    void start() override {
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        original_ = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD output_mode = 0;
        if (!GetConsoleMode(input_, &input_mode_) || !GetConsoleMode(original_, &output_mode))
            throw std::runtime_error("Run swiftedit-terminal in an interactive Windows console.");
        screen_ = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                            CONSOLE_TEXTMODE_BUFFER, nullptr);
        if (screen_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot create terminal screen buffer.");
        try {
            if (!SetConsoleMode(screen_,
                                ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ||
                !SetConsoleMode(input_, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT))
                throw std::runtime_error("Cannot configure interactive terminal mode.");
            input_changed_ = true;
            if (!SetConsoleActiveScreenBuffer(screen_))
                throw std::runtime_error("Cannot activate terminal screen.");
            active_ = true;
        } catch (...) {
            close();
            throw;
        }
    }
    void close() noexcept {
        if (active_)
            SetConsoleActiveScreenBuffer(original_);
        if (input_changed_)
            SetConsoleMode(input_, input_mode_);
        if (screen_ != INVALID_HANDLE_VALUE)
            CloseHandle(screen_);
        screen_ = INVALID_HANDLE_VALUE;
        input_changed_ = false;
        active_ = false;
    }
    swiftedit::TerminalSize size() const override {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(screen_, &info))
            throw std::runtime_error("Cannot read console dimensions.");
        const swiftedit::TerminalSize result{
            static_cast<std::size_t>(std::max(1, info.srWindow.Right - info.srWindow.Left + 1)),
            static_cast<std::size_t>(std::max(1, info.srWindow.Bottom - info.srWindow.Top + 1))};
        return result;
    }
    void write(std::string_view text) const override {
        const std::wstring encoded = utf16(text);
        std::size_t offset = 0;
        while (offset < encoded.size()) {
            const DWORD count =
                static_cast<DWORD>(std::min<std::size_t>(encoded.size() - offset, 16384));
            DWORD written = 0;
            if (!WriteConsoleW(screen_, encoded.data() + offset, count, &written, nullptr) ||
                !written)
                throw std::runtime_error("Cannot draw terminal screen.");
            offset += written;
        }
    }
    bool input_ready() const override {
        const DWORD result = WaitForSingleObject(input_, 0);
        if (result == WAIT_FAILED)
            throw std::runtime_error("Cannot poll console input.");
        const bool ready = result == WAIT_OBJECT_0;
        return ready;
    }
    bool cancel_requested() override {
        if (!wrap_cancel_enabled_)
            return false;
        // Consume only ignored key releases and an Escape at the queue head.
        // Preserve all other input ordering, including resize and typed text.
        for (std::size_t index = 0; index < 32; ++index) {
            INPUT_RECORD event{};
            DWORD count = 0;
            if (!PeekConsoleInputW(input_, &event, 1, &count))
                throw std::runtime_error("Cannot inspect terminal cancellation input.");
            if (!count || event.EventType != KEY_EVENT)
                return false;
            if (event.Event.KeyEvent.bKeyDown) {
                if (event.Event.KeyEvent.wVirtualKeyCode != VK_ESCAPE)
                    return false;
                static_cast<void>(read());
                return true;
            }
            static_cast<void>(read());
        }
        return false;
    }
    void allow_wrap_cancel(bool enabled) override { wrap_cancel_enabled_ = enabled; }
    swiftedit::TerminalInput read() const override {
        INPUT_RECORD input{};
        DWORD count = 0;
        if (!ReadConsoleInputW(input_, &input, 1, &count) || count != 1)
            throw std::runtime_error("Cannot read terminal input.");
        swiftedit::TerminalInput result{};
        result.resized = input.EventType == WINDOW_BUFFER_SIZE_EVENT;
        if (input.EventType == KEY_EVENT) {
            const KEY_EVENT_RECORD &key = input.Event.KeyEvent;
            result.pressed = key.bKeyDown != FALSE;
            result.repeats = key.wRepeatCount;
            result.text_unit = static_cast<char16_t>(key.uChar.UnicodeChar);
            result.control = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            result.alt = (key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
            result.shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
            switch (key.wVirtualKeyCode) {
            case VK_ESCAPE: result.key = swiftedit::terminal_key::escape; break;
            case VK_RETURN: result.key = swiftedit::terminal_key::enter; break;
            case VK_TAB: result.key = swiftedit::terminal_key::tab; break;
            case VK_BACK: result.key = swiftedit::terminal_key::backspace; break;
            case VK_DELETE: result.key = swiftedit::terminal_key::erase; break;
            case VK_LEFT: result.key = swiftedit::terminal_key::left; break;
            case VK_RIGHT: result.key = swiftedit::terminal_key::right; break;
            case VK_UP: result.key = swiftedit::terminal_key::up; break;
            case VK_DOWN: result.key = swiftedit::terminal_key::down; break;
            case VK_HOME: result.key = swiftedit::terminal_key::home; break;
            case VK_END: result.key = swiftedit::terminal_key::end; break;
            case VK_PRIOR: result.key = swiftedit::terminal_key::page_up; break;
            case VK_NEXT: result.key = swiftedit::terminal_key::page_down; break;
            case VK_F1: result.key = swiftedit::terminal_key::f1; break;
            case VK_F2: result.key = swiftedit::terminal_key::f2; break;
            case VK_F3: result.key = swiftedit::terminal_key::f3; break;
            case VK_F5: result.key = swiftedit::terminal_key::f5; break;
            case VK_F6: result.key = swiftedit::terminal_key::f6; break;
            case VK_OEM_2: result.key = swiftedit::terminal_key::question; break;
            default:
                if (key.wVirtualKeyCode >= 'A' && key.wVirtualKeyCode <= 'Z')
                    result.key = key.wVirtualKeyCode;
                else if (key.wVirtualKeyCode != 0)
                    result.key = swiftedit::terminal_key::other;
                break;
            }
        }
        return result;
    }

private:
    HANDLE input_{INVALID_HANDLE_VALUE}, original_{INVALID_HANDLE_VALUE},
        screen_{INVALID_HANDLE_VALUE};
    DWORD input_mode_{};
    bool input_changed_{}, active_{}, wrap_cancel_enabled_{true};
};
} // namespace
#ifndef SWIFTEDIT_TERMINAL_SMOKE
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::wstring_view(argv[1]) == L"--help")) {
            std::cout << "SwiftEdit interactive terminal\nUsage: swiftedit-terminal [file]\n"
                         "Ctrl+S Save, Ctrl+R Open, Ctrl+X Exit, Shift+arrows Select, "
                         "Ctrl+Z/Y Undo/Redo, Ctrl+K Cut, Ctrl+U Paste, Ctrl+T Save Text Copy, "
                         "F2 Wrap to window, F5 Insert Date and Time, F6 Word Count.\n"
                         "Read-only pages: Shift+Up/Down or Page keys select rows; "
                         "Ctrl+A selects all; Ctrl+C copies; any key cancels pending copy.\n";
            return argc > 2 ? 1 : 0;
        }
        Console host{};
        swiftedit::Terminal terminal{host};
        const std::filesystem::path path =
            argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
        const int result = terminal.run(path);
        return result;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}

#else
namespace {
struct SmokeConsole {
    HANDLE input{INVALID_HANDLE_VALUE}, output{INVALID_HANDLE_VALUE};
    bool allocated{};
    ~SmokeConsole() {
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        if (output != INVALID_HANDLE_VALUE)
            CloseHandle(output);
        if (allocated)
            FreeConsole();
    }
};
void enqueue(HANDLE input, WORD key, wchar_t character = 0, DWORD modifiers = 0) {
    INPUT_RECORD record{};
    record.EventType = KEY_EVENT;
    record.Event.KeyEvent.bKeyDown = TRUE;
    record.Event.KeyEvent.wRepeatCount = 1;
    record.Event.KeyEvent.wVirtualKeyCode = key;
    record.Event.KeyEvent.uChar.UnicodeChar = character;
    record.Event.KeyEvent.dwControlKeyState = modifiers;
    DWORD written = 0;
    if (!WriteConsoleInputW(input, &record, 1, &written) || written != 1)
        throw std::runtime_error("Cannot enqueue input in owned test console.");
    record.Event.KeyEvent.bKeyDown = FALSE;
    if (!WriteConsoleInputW(input, &record, 1, &written) || written != 1)
        throw std::runtime_error("Cannot enqueue key release in owned test console.");
}
} // namespace
int wmain() {
    try {
        SmokeConsole console{};
        // Always detach before allocating: fixture input must never reach a
        // caller's console, including when this test is launched manually.
        FreeConsole();
        if (!AllocConsole())
            throw std::runtime_error("Cannot allocate private test console.");
        console.allocated = true;
        ShowWindow(GetConsoleWindow(), SW_HIDE);
        console.input =
            CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr);
        console.output =
            CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (console.input == INVALID_HANDLE_VALUE || console.output == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot open owned test console.");
        SetStdHandle(STD_INPUT_HANDLE, console.input);
        SetStdHandle(STD_OUTPUT_HANDLE, console.output);
        DWORD mode = 0;
        if (!GetConsoleMode(console.input, &mode))
            throw std::runtime_error("Cannot read initial input mode.");
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("swiftedit-console-" + std::to_string(GetCurrentProcessId()));
        if (!std::filesystem::create_directory(dir))
            throw std::runtime_error("Test fixture already exists.");
        struct Cleanup {
            std::filesystem::path path{};
            ~Cleanup() {
                std::error_code error{};
                std::filesystem::remove_all(path, error);
            }
        } cleanup{dir};
        const std::filesystem::path path = dir / "edit.txt";
        {
            std::ofstream file(path, std::ios::binary);
        }
        if (!FlushConsoleInputBuffer(console.input))
            throw std::runtime_error("Cannot reset test input.");
        enqueue(console.input, 'A', L'a');
        enqueue(console.input, VK_RETURN, L'\r');
        enqueue(console.input, 0, 0xd83d);
        enqueue(console.input, 0, 0xde00);
        enqueue(console.input, 'S', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_HOME, 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'W', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'A', L'a');
        enqueue(console.input, VK_OEM_2, 0, LEFT_CTRL_PRESSED | SHIFT_PRESSED);
        enqueue(console.input, VK_RETURN, L'\r');
        enqueue(console.input, 'H', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_RETURN, L'\r');
        enqueue(console.input, 'B', L'b');
        enqueue(console.input, VK_RETURN, L'\r');
        enqueue(console.input, 'Z', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_END, 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'Q', L'q');
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, 'Z', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_F6);
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        {
            Console host{};
            swiftedit::Terminal terminal{host};
            if (terminal.run(path) != 0)
                throw std::runtime_error("Terminal run failed.");
            if (terminal.replacement_count() != 3)
                throw std::runtime_error(
                    "Terminal Replace All did not replace all three graphemes.");
            if (terminal.search_hits() != 1 || !terminal.wildcard_enabled())
                throw std::runtime_error(
                    "Terminal wildcard Find did not publish the expected match.");
            if (terminal.completed_counts() != 1 || terminal.last_word_count() != 2)
                throw std::runtime_error("F6 did not count the saved document.");
        }
        DWORD restored = 0;
        if (!GetConsoleMode(console.input, &restored) || restored != mode)
            throw std::runtime_error("Terminal failed to restore console input mode.");
        const std::string expected = "a" + notepad::preferred_newline("") + "\xf0\x9f\x98\x80";
        if (notepad::read_file(path).bytes != expected)
            throw std::runtime_error(
                "Console input/save/undo did not preserve expected UTF-8 bytes.");
        const std::filesystem::path wrap_path = dir / "wrap.txt";
        const std::string wrap_source(4096, 'a');
        {
            std::ofstream file(wrap_path, std::ios::binary);
            file << wrap_source;
        }
        FlushConsoleInputBuffer(console.input);
        enqueue(console.input, VK_F2);
        enqueue(console.input, VK_DOWN);
        enqueue(console.input, VK_HOME);
        enqueue(console.input, 'X', L'X');
        enqueue(console.input, 'S', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_F2);
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        std::size_t wrap_width = 0;
        {
            Console host{};
            swiftedit::Terminal terminal{host};
            if (terminal.run(wrap_path) != 0 || terminal.wraps())
                throw std::runtime_error("Terminal wrap toggle did not return to no-wrap.");
            wrap_width = terminal.viewport_width();
        }
        std::string wrap_expected = wrap_source;
        wrap_expected.insert(wrap_width, "X");
        if (notepad::read_file(wrap_path).bytes != wrap_expected)
            throw std::runtime_error("Wrapped Down/Home/edit saved the wrong source position.");
        if (!GetConsoleMode(console.input, &restored) || restored != mode)
            throw std::runtime_error("Wrapped terminal did not restore input mode.");
        FlushConsoleInputBuffer(console.input);
        enqueue(console.input, VK_F2);
        enqueue(console.input, VK_END, 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, VK_HOME, 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_DOWN);
        enqueue(console.input, VK_HOME);
        enqueue(console.input, 'Y', L'Y');
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, 'S', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        {
            Console host{};
            swiftedit::Terminal terminal{host};
            if (terminal.run(wrap_path) != 0 || terminal.wrap_cancellations() != 1)
                throw std::runtime_error(
                    "Wrap cancellation/recovery did not execute exactly once.");
        }
        wrap_expected.insert(wrap_width, "Y");
        if (notepad::read_file(wrap_path).bytes != wrap_expected)
            throw std::runtime_error("Wrap cancellation/recovery or exit cancel changed source.");
        if (!GetConsoleMode(console.input, &restored) || restored != mode)
            throw std::runtime_error("Cancelled wrapped terminal did not restore input mode.");
        const std::filesystem::path illegal_path = dir / "illegal.txt";
        const std::filesystem::path copy_path = dir / "illegal.1.txt";
        const std::string illegal_source("A\xff\xfe\r\rZ", 6);
        {
            std::ofstream file(illegal_path, std::ios::binary);
            file.write(illegal_source.data(), static_cast<std::streamsize>(illegal_source.size()));
        }
        FlushConsoleInputBuffer(console.input);
        enqueue(console.input, 'T', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        {
            Console host{};
            swiftedit::Terminal terminal{host};
            if (terminal.run(illegal_path) != 0 || std::filesystem::exists(copy_path))
                throw std::runtime_error("Cancelled Text Copy wrote a destination.");
        }
        FlushConsoleInputBuffer(console.input);
        enqueue(console.input, 'Q', L'Q');
        enqueue(console.input, 'T', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_RETURN, L'\r');
        // A second attempt must refuse this now-existing destination.
        enqueue(console.input, 'T', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_RETURN, L'\r');
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'N', L'n');
        {
            Console host{};
            swiftedit::Terminal terminal{host};
            if (terminal.run(illegal_path) != 0 || terminal.text_copy_saves() != 1 ||
                !terminal.document_dirty())
                throw std::runtime_error("Text Copy terminal run failed.");
        }
        if (notepad::read_file(copy_path).bytes != "QA  \r\rZ" ||
            notepad::read_file(illegal_path).bytes != illegal_source)
            throw std::runtime_error(
                "Text Copy changed source or failed byte-for-space conversion.");
        if (!GetConsoleMode(console.input, &restored) || restored != mode)
            throw std::runtime_error("Text Copy terminal did not restore input mode.");
        const std::filesystem::path large_path = dir / "large.txt";
        {
            std::ofstream file(large_path, std::ios::binary);
            file << "read-only\r\n\xf0\x9f\x98\x80";
            file.seekp(swiftedit::editable_limit - 1);
            file.put('z');
        }
        const std::filesystem::file_time_type original_time =
            std::filesystem::last_write_time(large_path);
        FlushConsoleInputBuffer(console.input);
        enqueue(console.input, VK_NEXT);
        enqueue(console.input, VK_PRIOR);
        enqueue(console.input, VK_HOME, 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_DOWN, 0, SHIFT_PRESSED);
        enqueue(console.input, 'C', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'A', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'C', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, VK_F6);
        enqueue(console.input, VK_ESCAPE);
        enqueue(console.input, 'S', 0, LEFT_CTRL_PRESSED);
        enqueue(console.input, 'X', 0, LEFT_CTRL_PRESSED);
        {
            Console host{};
            swiftedit::Terminal terminal{host};
            if (terminal.run(large_path) != 0 || terminal.cancelled_counts() != 1 ||
                terminal.completed_counts() != 0 || terminal.completed_copies() != 1 ||
                terminal.cancelled_copies() != 1 || terminal.clipboard_bytes() != "read-only\r\n")
                throw std::runtime_error("Read-only terminal run failed.");
        }
        if (std::filesystem::last_write_time(large_path) != original_time ||
            std::filesystem::file_size(large_path) != swiftedit::editable_limit)
            throw std::runtime_error("Read-only terminal navigation/save altered the file.");
        if (!GetConsoleMode(console.input, &restored) || restored != mode)
            throw std::runtime_error("Read-only terminal did not restore input mode.");
        std::cout << "Owned console smoke passed: input, supplementary Unicode, save, dirty-exit "
                     "cancel, undo, wrapped editing, large-file pages and mode restoration.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
#endif
