#include "editor.hpp"
#include "new_window.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef _WIN32
namespace {
// CommandLineToArgvW owns one LocalAlloc block, including every string.
struct NativeArguments {
    int count{};
    LPWSTR *values{};
    NativeArguments() : values(CommandLineToArgvW(GetCommandLineW(), &count)) {
        if (!values)
            throw std::runtime_error("Cannot read command line.");
    }
    ~NativeArguments() {
        if (values)
            LocalFree(values);
    }
    NativeArguments(const NativeArguments &) = delete;
    NativeArguments &operator=(const NativeArguments &) = delete;
};
} // namespace
#endif

int main(int argc, char **argv) {
    try {
        std::filesystem::path initial{};
#ifdef _WIN32
        static_cast<void>(argc);
        static_cast<void>(argv);
        const NativeArguments arguments{};
        if (arguments.count > 1)
            initial = std::filesystem::absolute(arguments.values[1]);
#else
        if (argc > 1)
            initial = std::filesystem::absolute(argv[1]);
#endif
        std::shared_ptr<notepad::Editor> editor =
            gui_forms::make_control<notepad::Editor>(gui_forms::StableId("notepad.editor"),
                notepad::NewWindow{notepad::executable_path()});
        std::vector<gui_forms::ApplicationWindow> windows = (*editor).application_windows(initial);
        const gui_forms::ApplicationResult result = gui_forms::Application::run(std::move(windows));
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        const int exit_code = result.accepted() ? 0 : 1;
        return exit_code;
    } catch (const std::exception &e) {
        std::cerr << "SwiftEdit: " << e.what() << '\n';
        return 1;
    }
}
