#include "editor.hpp"
#include "main_window_form.hpp"  // generated from forms/main_window.form
#include "new_window.hpp"
#include "swiftedit_theme.hpp"     // generated from themes/swiftedit
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
        // The application first: its theme (the GUI.Forms base, then
        // SwiftEdit's package) is compiled once, as it is made.
        gui_forms::ApplicationConfig config{};
        config.theme.base = gui_forms::ThemeBase::gui_forms;
        config.theme.packages.push_back(swiftedit::swiftedit_theme());
        gui_forms::ApplicationCreateResult created = gui_forms::Application::create(std::move(config));
        if (!created) {
            for (const gui_forms::ThemeCompileDiagnostic &finding : created.findings)
                std::cerr << "SwiftEdit theme: " << finding.code << ' ' << finding.file << ':'
                          << finding.line << ':' << finding.column << ' ' << finding.detail << '
';
            throw std::runtime_error("Cannot create the application.");
        }
        int exit_code = 1;
        {
            gui_forms::Application &application = *created.application;
            // The main window is the form's; the editor it holds builds the
            // rest and owns the dialogs.
            std::unique_ptr<swiftedit::MainWindow> window = std::make_unique<swiftedit::MainWindow>(application);
            const std::shared_ptr<notepad::Editor> editor = (*window).notepad_editor;
            (*editor).set_new_window(notepad::NewWindow{notepad::executable_path()});
            std::vector<gui_forms::ApplicationWindow> windows =
                (*editor).application_windows(std::move(window), initial);
            const gui_forms::ApplicationResult result = application.run(std::move(windows));
            if (result.callback_exception)
                std::rethrow_exception(result.callback_exception);
            exit_code = result.accepted() ? 0 : 1;
        }
        // Once, after the last run: ends the rendering library for the process.
        const gui_forms::ApplicationResult ended = gui_forms::Application::shutdown();
        static_cast<void>(ended);
        return exit_code;
    } catch (const std::exception &e) {
        std::cerr << "SwiftEdit: " << e.what() << '\n';
        return 1;
    }
}
