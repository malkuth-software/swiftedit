#include "editor.hpp"
#include "native_font_check.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "platform.hpp"
#ifdef __APPLE__
#include "mac_view_snapshot.hpp"
#endif
namespace gf = gui_forms;
void require(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
gf::Control::Ptr find_control(const gf::Control::Ptr &root, std::string_view id) {
    if ((*root).stable_id().value() == id)
        return root;
    for (const gf::Control::Ptr &child : (*root).children()) {
        const gf::Control::Ptr found = find_control(child, id);
        if (found)
            return found;
    }
    return {};
}
// Application::run is synchronous. Every callback borrows this stack owner only
// until run returns. The timer subscription is revoked before timer destruction;
// original window hooks retain only the production editor's weak observer.
class NativeSmoke final {
public:
    struct ReadyListener {
        NativeSmoke *owner{};
        std::size_t index{};
        void operator()(gf::Window &window, gf::ApplicationWindowHandle handle) const {
            (*owner).ready(index, window, handle);
        }
    };
    struct ClosingListener {
        NativeSmoke *owner{};
        std::size_t index{};
        void operator()(gf::HostCloseRequest &request) const { (*owner).closing(index, request); }
    };
    struct TickListener {
        NativeSmoke *owner{};
        void operator()() const { (*owner).on_tick(); }
    };
    struct WindowHooks {
        std::function<void(gf::Window &, gf::ApplicationWindowHandle)> ready{};
        std::function<void(gf::HostCloseRequest &)> closing{};
    };
    std::filesystem::path path{};
    std::shared_ptr<notepad::Editor> editor{};
    gf::Window *main{};
    gf::Window *find_window{};
    std::array<gf::ApplicationWindowHandle, 9> handles{};
    std::array<unsigned, 9> close_count{};
    std::array<WindowHooks, 9> hooks{};
    gf::ApplicationWindowHandle main_handle{};
    std::size_t ready_count{};
    bool passed{};
    bool find_open_requested{};
    std::chrono::steady_clock::time_point find_requested_at{};
    std::chrono::steady_clock::time_point csv_submitted_at{};
    std::chrono::steady_clock::time_point markdown_submitted_at{};
    int stage{};
    std::exception_ptr test_failure{};
    std::unique_ptr<gf::Timer> timer{};
    gf::SubscriptionToken tick{};
    std::string expected_title(const bool dirty, const bool untitled = false) const {
        std::string title = dirty ? "* " : "";
        title += untitled ? "Untitled" : notepad::path_utf8(path.filename());
        title += " - SwiftEdit";
        return title;
    }
    void verify_title(const bool dirty, const bool untitled = false) const {
        const std::shared_ptr<gf::Label> status =
            std::dynamic_pointer_cast<gf::Label>(find_control(editor, "notepad.status"));
        require(status && (*status).text().find("Native title update unavailable") == std::string::npos,
                "Native title request accepted");
#ifdef __APPLE__
        const std::string title = expected_title(dirty, untitled);
        require(native_window_visible(title.c_str()), "Exact document title belongs to visible native window");
#else
        static_cast<void>(dirty);
        static_cast<void>(untitled);
#endif
    }
    ~NativeSmoke() {
        // Application::run has already shut down its windows. Stop is performed
        // by the completion/failure callback before close, never after teardown.
        tick = {};
        timer.reset();
    }
    void ready(std::size_t index, gf::Window &window, gf::ApplicationWindowHandle handle) {
        require_native_fonts(window);
        hooks[index].ready(window, handle);
        handles[index] = handle;
        ++ready_count;
        if (index == 0)
            main_handle = handle;
        if (ready_count == handles.size()) {
            timer = std::make_unique<gf::Timer>(*main, std::chrono::milliseconds(60));
            tick = (*timer).tick().subscribe(TickListener{this});
            (*timer).start();
        }
    }
    void closing(std::size_t index, gf::HostCloseRequest &request) {
        if (index == 0 && test_failure)
            request.cancel = false;
        else if (hooks[index].closing)
            hooks[index].closing(request);
        ++close_count[index];
    }
    void on_tick() {
        try {
            const int current_stage = stage;
            ++stage;
            switch (current_stage) {
            case 0: {
                if (!find_open_requested) {
                    require((*(*editor).text_control()).text() == "native\r\nfixture",
                            "Native initial file");
                    verify_title(false);
                    require(!find_control(editor, "notepad.document-name"),
                            "Filename no longer consumes a client-area row");
                    (*(*editor).text_control()).select_all();
                    (*(*editor).text_control()).replace_selection("native saved\r\n");
                    verify_title(true);
                    (*editor).execute("save");
                    const notepad::FileSnapshot observed_1 = notepad::read_file(path);
                    require(observed_1.bytes == "native saved\r\n", "Native save");
                    verify_title(false);
                    // Public lifecycle only: no global input, cursor or desktop automation.
                    (*(*editor).text_control()).select(gf::Utf8Offset(0), gf::Utf8Offset(6));
                    find_requested_at = std::chrono::steady_clock::now();
                    (*editor).execute("find");
                    require(handles[3].active(), "Owned find window active");
                    const std::shared_ptr<notepad::QueryField> opened_query =
                        std::dynamic_pointer_cast<notepad::QueryField>(
                            find_control((*find_window).root(), "find.query"));
                    require(opened_query && (*opened_query).text() == "native",
                            "Find command reached the dialog and seeded selected source text");
                    const std::shared_ptr<gf::Label> status =
                        std::dynamic_pointer_cast<gf::Label>(find_control(editor, "notepad.status"));
                    require(status && !(*status).text().starts_with("Cannot show the Find window"),
                            "Find show request accepted by host");
                    find_open_requested = true;
                    // Give queued native presentation an event-loop turn.
                    // The AppKit helper separately verifies actual visibility.
                    --stage;
                    return;
                }
                const std::shared_ptr<notepad::QueryField> query =
                    std::dynamic_pointer_cast<notepad::QueryField>(
                        find_control((*find_window).root(), "find.query"));
                const std::shared_ptr<gf::TextBox> replacement =
                    std::dynamic_pointer_cast<gf::TextBox>(
                        find_control((*find_window).root(), "find.replacement"));
                require(query && replacement, "Find dialog public controls");
#ifdef __APPLE__
                const bool visible = native_window_visible("Find and Replace - SwiftEdit");
                const std::chrono::steady_clock::duration elapsed =
                    std::chrono::steady_clock::now() - find_requested_at;
                if (!visible && elapsed < std::chrono::seconds(2)) {
                    --stage;
                    return;
                }
                std::cout << "Find native visibility=" << visible << " after "
                          << std::chrono::duration<double, std::milli>(elapsed).count()
                          << " ms; presentation readiness, not an input latency benchmark.\n";
                (*query).set_text("n?tive");
                (*query).select(gf::Utf8Offset(1), gf::Utf8Offset(1));
                (*find_window).request_focus(query);
                send_native_query_wildcard("Find and Replace - SwiftEdit");
                const swiftedit::SearchPattern native_pattern = (*query).pattern();
                require((*query).text() == "n?tive" && native_pattern.slots()[1].wildcard,
                        "AppKit Control+Shift+/ toggles the selected query slot without inserting text");
                (*std::dynamic_pointer_cast<gf::Button>(
                     find_control((*find_window).root(), "find.next"))).perform_click();
                require((*(*editor).text_control()).selected_text() == "native",
                        "Native wildcard query finds the source match");
                send_native_query_wildcard("Find and Replace - SwiftEdit");
                const swiftedit::SearchPattern restored_pattern = (*query).pattern();
                require(!restored_pattern.slots()[1].wildcard,
                        "Repeating the native shortcut restores a literal query slot");
#endif
                (*query).set_text("native");
                (*replacement).set_text("replaced");
                (*std::dynamic_pointer_cast<gf::Button>(
                     find_control((*find_window).root(), "find.next")))
                    .perform_click();
                const std::string selected = (*(*editor).text_control()).selected_text();
                require(selected == "native", "Literal Find selects matching text");
                (*std::dynamic_pointer_cast<gf::Button>(
                     find_control((*find_window).root(), "find.all")))
                    .perform_click();
                require((*(*editor).text_control()).text() == "replaced saved\r\n",
                        "Replace All changes literal text");
                (*editor).execute("undo");
                require((*(*editor).text_control()).text() == "native saved\r\n",
                        "Replace All single undo");
                static_cast<void>(handles[3].request_close());
                break;
            }
            case 1:
                (*editor).execute("font");
                require(handles[4].active(), "Owned font window active");
                static_cast<void>(handles[4].request_close());
                break;
            case 2:
                (*editor).execute("open");
                require(!(*editor).enabled(), "Picker suppresses owner controls");
                static_cast<void>(handles[1].request_close());
                break;
            case 3:
                if (close_count[1] < 1) {
                    --stage;
                    return;
                }
                std::cout << "open picker closed; owner enabled=" << (*editor).enabled()
                          << std::endl;
                require((*editor).enabled(), "Picker cancellation restores owner controls");
                std::cout << "show save as" << std::endl;
                (*editor).execute("save-as");
                require(!(*editor).enabled(), "Save As suppresses owner controls");
                static_cast<void>(handles[2].request_close());
                break;
            case 4:
                if (close_count[2] < 1) {
                    --stage;
                    return;
                }
                require((*editor).enabled(), "Save As cancellation restores owner controls");
                (*editor).execute("open");
                require(!(*editor).enabled(), "Picker reusable presentation");
                static_cast<void>(handles[1].request_close());
                break;
            case 5:
                if (close_count[1] < 2) {
                    --stage;
                    return;
                }
                require((*editor).enabled(), "Reopened picker cancellation");
                (*editor).execute("characters");
                require(handles[5].active(), "Owned Unicode dialog active");
                (*(*editor).character_picker(false)).select_codepoint(0x1f600);
                (*(*editor).character_picker(false)).insert_selected();
                require((*(*editor).text_control()).text().find("\xf0\x9f\x98\x80") !=
                            std::string::npos,
                        "Native Unicode dialog inserts a supplementary scalar");
                (*editor).execute("undo");
                require((*(*editor).text_control()).text() == "native saved\r\n",
                        "Unicode insertion undo");
                static_cast<void>(handles[5].request_close());
                break;
            case 6:
                (*editor).execute("controls");
                require(handles[6].active(), "Owned control dialog active");
                (*(*editor).character_picker(true)).select_codepoint(0x200b);
                (*(*editor).character_picker(true)).insert_selected();
                require((*(*editor).text_control()).text().find("\xe2\x80\x8b") !=
                            std::string::npos,
                        "Native control dialog inserts literal source bytes");
                (*editor).execute("undo");
                require((*(*editor).text_control()).text() == "native saved\r\n",
                        "Control insertion undo");
                static_cast<void>(handles[6].request_close());
                break;
            case 7:
                if (close_count[5] < 1 || close_count[6] < 1) {
                    --stage;
                    return;
                }
                (*(*editor).text_control()).set_text("mixed\nline\r\n");
                (*editor).execute("save");
                require((*editor).save_choice_pending() && handles[7].active(),
                        "Native mixed-ending choice is active before publication");
                (*editor).execute("cancel-save");
                require(!(*editor).save_choice_pending() && (*editor).enabled(),
                        "Native mixed-ending cancellation restores owner");
                (*editor).execute("save");
                (*editor).execute("save-as-is");
                require(notepad::read_file(path).bytes == "mixed\nline\r\n",
                        "Native mixed-ending Save As-Is preserves bytes");
                break;
            case 8:
                (*editor).execute("save");
                require((*editor).save_choice_pending(), "Native normalization choice reopened");
                (*editor).execute("save-normalized");
                require(
                    notepad::read_file(path).bytes == "mixed\r\nline\r\n" &&
                        (*(*editor).text_control()).text() == "mixed\r\nline\r\n",
                    "Native conversion uses the document CRLF default and displays saved bytes");
                break;
            case 9: {
                std::ofstream external(path, std::ios::binary);
                external << "external native change";
                external.close();
                (*editor).execute("save");
                require((*editor).conflict_pending() && handles[8].active(),
                        "Native conflict choice owns an active dialog");
                (*editor).execute("conflict-over");
                (*editor).execute("conflict-review");
                break;
            }
            case 10:
                (*editor).execute("conflict-save");
                require(!(*editor).conflict_pending() && (*editor).enabled() &&
                            notepad::read_file(path).bytes == "mixed\r\nline\r\n",
                        "Native reviewed overwrite publishes and restores owner");
                break;
            case 11:
                markdown_submitted_at = std::chrono::steady_clock::now();
                (*editor).execute("markdown-view");
                break;
            case 12: {
                const std::shared_ptr<notepad::MarkdownView> markdown =
                    std::dynamic_pointer_cast<notepad::MarkdownView>(find_control(editor, "swiftedit.markdown"));
                require(static_cast<bool>(markdown), "Native Markdown view exists");
                if (!(*markdown).presentation_ready()) {
                    require(std::chrono::steady_clock::now() - markdown_submitted_at < std::chrono::seconds(5),
                            "Native Markdown preparation readiness");
                    --stage;
                    return;
                }
                const std::shared_ptr<gf::MenuStrip> menu =
                    std::dynamic_pointer_cast<gf::MenuStrip>(
                        find_control(editor, "notepad.menus"));
                require(static_cast<bool>(menu), "Native editor menu exists");
                bool opened = false;
                const std::vector<gf::MenuStripItemSpec> &items = (*menu).items();
                for (std::size_t index = 0; index < items.size(); ++index) {
                    if (items[index].stable_id == "view")
                        opened = (*menu).open(index, gf::MenuOpenMode::pointer);
                }
                require(opened, "Native View menu opens above rendered Markdown");
                break;
            }
            case 13: {
#ifdef __APPLE__
                const std::string title = expected_title(false);
                capture_native_view(title.c_str(), "editor-markdown-menu");
#endif
                const std::shared_ptr<gf::MenuStrip> menu =
                    std::dynamic_pointer_cast<gf::MenuStrip>(
                        find_control(editor, "notepad.menus"));
                (*menu).close();
                (*editor).execute("markdown-view");
                break;
            }
            case 14: {
#ifdef __APPLE__
                const std::string title = expected_title(false);
                capture_native_view(title.c_str(), "editor-source-status");
#endif
                (*(*editor).text_control()).set_text("2,3\r\n4,5");
                (*editor).execute("csv-view");
                const std::shared_ptr<notepad::CsvView> grid = (*editor).csv_control();
                require((*grid).visible(), "Native CSV editor view opened");
                (*grid).select_cell({0, 1});
                const std::shared_ptr<gf::TextBox> entry =
                    std::dynamic_pointer_cast<gf::TextBox>(find_control(grid, "csv.entry"));
                require(static_cast<bool>(entry), "Native CSV formula entry exists");
                (*entry).set_text("=A1*4");
                (*main).request_focus(entry);
                csv_submitted_at = std::chrono::steady_clock::now();
                const bool submitted = (*main).dispatch_key(
                    {gf::KeyAction::down, gf::PhysicalKey::enter});
                require(submitted && (*(*editor).text_control()).text() == "2,=A1*4\r\n4,5",
                        "Enter through the native window retains formula source");
                break;
            }
            case 15: {
                const std::shared_ptr<notepad::CsvView> grid = (*editor).csv_control();
                if ((*grid).calculations_pending()) {
                    const std::chrono::steady_clock::duration elapsed =
                        std::chrono::steady_clock::now() - csv_submitted_at;
                    require(elapsed < std::chrono::seconds(2),
                            "Native CSV formula work completes within the readiness bound");
                    --stage;
                    return;
                }
#ifdef __APPLE__
                const std::string title = expected_title(true);
                capture_native_view(title.c_str(), "editor-csv-formula");
#endif
                const std::shared_ptr<gf::MenuStrip> menu =
                    std::dynamic_pointer_cast<gf::MenuStrip>(find_control(editor, "notepad.menus"));
                require(static_cast<bool>(menu), "Native CSV menu strip exists");
                bool menu_converted = false;
                for (const gf::MenuStripItemSpec &group : (*menu).items()) {
                    if (group.stable_id != "csv")
                        continue;
                    for (const gf::MenuItemSpec &item : group.items) {
                        if (item.stable_id == "csv-convert-value" && item.command)
                            menu_converted = (*item.command).execute("native.csv-menu");
                    }
                }
                require(menu_converted && (*(*editor).text_control()).text() == "2,8\r\n4,5",
                        "Native editor CSV menu converts formula to exact result");
                (*editor).execute("undo");
                require((*(*editor).text_control()).text() == "2,=A1*4\r\n4,5",
                        "Native conversion undo restores formula");
                const gf::Rect bounds = (*grid).absolute_bounds();
                gf::PointerEvent context_press{};
                context_press.action = gf::PointerAction::down;
                context_press.button = gf::PointerButton::secondary;
                context_press.position = {bounds.x + 56 + 144 + 12, bounds.y + 68 + 12};
                (*grid).on_pointer(context_press);
                require((*grid).context_menu().is_open(), "Native cell context menu opens");
                const std::vector<gf::MenuItemSpec> &items = (*grid).context_menu().items();
                require(items.size() == 1 && items[0].text == "Convert to Value",
                        "Native cell menu exposes conversion");
                const bool converted = (*items[0].command).execute("csv.context");
                require(converted, "Native cell context command admits conversion");
                (*grid).context_menu().close();
                csv_submitted_at = std::chrono::steady_clock::now();
                break;
            }
            case 16: {
                const std::shared_ptr<notepad::CsvView> grid = (*editor).csv_control();
                if ((*grid).calculations_pending()) {
                    const std::chrono::steady_clock::duration elapsed =
                        std::chrono::steady_clock::now() - csv_submitted_at;
                    require(elapsed < std::chrono::seconds(2), "Native context conversion readiness");
                    --stage;
                    return;
                }
                require((*(*editor).text_control()).text() == "2,8\r\n4,5",
                        "Native cell context command converts formula asynchronously");
                (*editor).execute("undo");
                require((*(*editor).text_control()).text() == "2,=A1*4\r\n4,5",
                        "Native context conversion undo restores formula");
                (*editor).execute("save");
                require(notepad::read_file(path).bytes == "2,=A1*4\r\n4,5",
                        "Native CSV save preserves formula source");
                verify_title(false);
                (*editor).execute("new");
                verify_title(false, true);
                (*(*editor).text_control()).replace_selection("new draft");
                verify_title(true, true);
                (*editor).execute("undo");
                require((*(*editor).text_control()).text().empty(), "New draft undo restores blank document");
                verify_title(false, true);
                std::string long_text{};
                for (int row = 0; row < 100; ++row) {
                    long_text.append(180, 'x');
                    long_text.append("\n");
                }
                (*(*editor).text_control()).set_word_wrap(false);
                (*(*editor).text_control()).set_text(long_text);
                (*(*editor).text_control()).select(gf::Utf8Offset(0), gf::Utf8Offset(4));
                break;
            }
            case 17: {
                gf::TextBox &text = *(*editor).text_control();
                require(text.vscroll() && text.hscroll(), "Native document exposes both overflow scrollbars");
                static_cast<void>(text.scroll_to({120, 300}, gf::ScrollEventType::thumb_track, true));
                break;
            }
            case 18: {
                gf::TextBox &text = *(*editor).text_control();
                require(text.scroll_offset().x == text.scroll_position().x &&
                            text.scroll_offset().y == text.scroll_position().y &&
                            text.scroll_offset().x > 0 && text.scroll_offset().y > 0,
                        "Native painting retains the requested document viewport");
                require(text.selected_text() == "xxxx", "Native scrolling preserves selection");
#ifdef __APPLE__
                const std::string title = expected_title(true, true);
                capture_native_view(title.c_str(), "editor-document-scrollbars");
#endif
                (*editor).execute("wrap");
                break;
            }
            case 19: {
                gf::TextBox &text = *(*editor).text_control();
                require(text.vscroll() && !text.hscroll() && text.scroll_offset().x == 0,
                        "Native wrapped document only scrolls vertically");
                text.set_text("");
                break;
            }
            case 20: {
                gf::TextBox &text = *(*editor).text_control();
                require(!text.vscroll() && !text.hscroll(), "Native empty document clears overflow bars");
                passed = true;
                (*timer).stop();
                static_cast<void>(main_handle.request_close());
                break;
            }
            }

        } catch (const std::exception &failure) {
            std::cerr << "Native stage failed: " << failure.what() << std::endl;
            test_failure = std::current_exception();
            (*timer).stop();
            static_cast<void>(main_handle.request_close());
        }
    }
    void run() {
        editor = gf::make_control<notepad::Editor>(gf::StableId("native.editor"));
        std::vector<gf::ApplicationWindow> windows = (*editor).application_windows(path);
        require(windows.size() == handles.size(), "Expected main and eight owned dialogs");
        main = windows.front().model.get();
        find_window = windows[3].model.get();
        for (std::size_t i = 0; i < windows.size(); ++i) {
            hooks[i].ready = std::move(windows[i].options.ready);
            hooks[i].closing = std::move(windows[i].options.closing);
            windows[i].options.ready = ReadyListener{this, i};
            windows[i].options.closing = ClosingListener{this, i};
            windows[i].options.initially_visible = i == 0;
        }
        const gf::ApplicationResult result = gf::Application::run(std::move(windows));
        tick = {};
        timer.reset();
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (test_failure)
            std::rethrow_exception(test_failure);
        require(result.accepted() && passed, "Native lifecycle smoke completion");
    }
};
int main() {
    try {
        std::filesystem::path filename(u8"notepad-native-&-\u03b1-");
        filename += std::to_string(test_process_id());
        filename += ".csv";
        const std::filesystem::path path = std::filesystem::temp_directory_path() / filename;
        const bool observed_2 = std::filesystem::exists(path);
        require(!observed_2, "Unique native fixture");
        struct Cleanup {
            std::filesystem::path path{};
            ~Cleanup() {
                std::error_code error{};
                std::filesystem::remove(path, error);
            }
        } cleanup{path};
        {
            std::ofstream output(path, std::ios::binary);
            output << "native\r\nfixture";
        }
        NativeSmoke smoke{};
        smoke.path = path;
        smoke.run();
        std::cout << "Native smoke passed: save, Find/Replace/undo, owned dialogs, picker "
                     "cancel/reopen, CSV Enter/formula/conversion/undo/save and clean shutdown.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
