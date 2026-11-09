// The screenshot run (SwiftEdit --screenshots <dir>): once the main window is
// up, opens a few sample documents the run writes into <dir>, captures the
// editor's window after each has painted (swiftedit_<state>.png, through the
// window's own offscreen snapshot and stx_png), then closes the window.
#include "editor.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <stx_png/encoder.hpp>
#include <vector>

namespace notepad {

namespace {

// The sample documents, written beside the captures.
constexpr std::string_view sample_sheet =
    "/* A theme sheet, highlighted by SwiftEdit's CSS lexer and painted\n"
    "   through GUI.Forms' styled text runs. */\n"
    ":root {\n"
    "  ink-on-paper: #1b2733 on #ffffff;\n"
    "  accent: #19528b on #ffffff;\n"
    "}\n"
    "\n"
    "@media (prefers-color-scheme: dark) {\n"
    "  :root { ink-on-paper: #e5eaef on #1b2733; }\n"
    "}\n"
    "\n"
    "button, .primary::part(face) {\n"
    "  color: role(ink-on-paper);\n"
    "  border-radius: 6px;\n"
    "  opacity: 0.9 !important;\n"
    "}\n"
    "\n"
    "#settings\\.ok:hover { background-color: rgb(25 82 139 / 12%); }\n"
    "label { font-family: \"Portsmouth Rapids\"; colr: red; }\n";

constexpr std::string_view sample_form =
    "/* A form document: types, stable IDs and properties. */\n"
    "form settings.window {\n"
    "  text: \"Settings\";\n"
    "  client-size: 480 320;\n"
    "\n"
    "  check-box settings.wrap { text: \"&Word wrap\"; checked: true; bounds: 16 16 200 24; }\n"
    "  button settings.ok { text: \"OK\"; bounds: 300 270 80 28; }\n"
    "}\n";

constexpr std::string_view sample_text =
    "SwiftEdit opens plain text without decoration.\n"
    "\n"
    "Syntax colours appear for theme sheets (.css) and form documents (.form);\n"
    "this file is plain, so it is drawn in one style.\n";

void write_sample(const std::filesystem::path &file, std::string_view text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out)
        throw std::runtime_error("Cannot write the screenshot sample " + file.string());
}

} // namespace

void Editor::ScreenshotTick::operator()(gf::FrameTime) const {
    const std::shared_ptr<Editor> self = lock_alive(owner);
    if (!self)
        return;
    if (!(*self).screenshot_step()) {
        (*self).screenshot_frame_.disconnect();
        static_cast<void>((*self).handle_.request_close());
    }
}

void Editor::set_screenshot_directory(std::filesystem::path directory) {
    screenshot_directory_ = std::move(directory);
}

void Editor::start_screenshots() {
    std::filesystem::create_directories(screenshot_directory_);
    write_sample(screenshot_directory_ / "sample.theme.css", sample_sheet);
    write_sample(screenshot_directory_ / "settings.form", sample_form);
    write_sample(screenshot_directory_ / "notes.txt", sample_text);
    screenshot_next_ = 0U;
    const gf::FrameTime first = gf::FrameClock::now() + std::chrono::milliseconds(300);
    screenshot_frame_ = (*window()).schedule_ui_timer(*this, gf::FrameInterval(std::chrono::milliseconds(300)), first,
                                                      ScreenshotTick{observe()});
}

bool Editor::screenshot_step() {
    const std::size_t step = screenshot_next_;
    ++screenshot_next_;
    try {
        switch (step) {
        case 0U:
            open_file(screenshot_directory_ / "sample.theme.css");
            return true;
        case 1U:
            capture_screenshot("css");
            return true;
        case 2U:
            open_file(screenshot_directory_ / "settings.form");
            return true;
        case 3U:
            capture_screenshot("form");
            return true;
        case 4U:
            open_file(screenshot_directory_ / "notes.txt");
            return true;
        case 5U:
            capture_screenshot("plain");
            return false;
        default:
            return false;
        }
    } catch (const std::exception &failure) {
        screenshot_failures_.push_back(failure.what());
        return step < 5U;
    }
}

void Editor::capture_screenshot(const char *state) {
    gf::Window &owner = *window();
    owner.perform_layout();
    const std::optional<gf::ControlSnapshot> taken = owner.snapshot_control(shared_from_this(), owner.scale());
    if (!taken) {
        screenshot_failures_.push_back(std::string(state) + ": this window cannot draw offscreen");
        return;
    }
    const std::optional<gf::ImageResourceView> view = owner.image_resources().find((*taken).image);
    std::vector<std::byte> encoded{};
    bool written = false;
    if (view && (*view).encoding == gf::ImageResourceEncoding::bgra32_premultiplied) {
        stx_png::PngEncodeInput input{};
        input.width = (*taken).pixel_width;
        input.height = (*taken).pixel_height;
        input.row_bytes = (*view).row_bytes;
        input.pixels = (*view).encoded;
        input.order = stx_png::PngChannelOrder::bgra;
        input.alpha = stx_png::PngSourceAlpha::premultiplied;
        written = stx_png::encode_png(input, encoded);
    }
    static_cast<void>(owner.release_control_snapshot((*taken).image));
    if (!written) {
        screenshot_failures_.push_back(std::string(state) + ": the snapshot could not be encoded");
        return;
    }
    const std::filesystem::path file = screenshot_directory_ / (std::string("swiftedit_") + state + ".png");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    out.close();
    if (!out)
        screenshot_failures_.push_back(std::string(state) + ": cannot write " + file.string());
}

} // namespace notepad
