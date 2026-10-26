#include "rtf_import.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        check(notepad::looks_like_rtf("{\\rtf1 x}") && !notepad::looks_like_rtf("{\\rt") &&
                  !notepad::looks_like_rtf(" {\\rtf1}"),
              "Only content beginning {\\rtf is RTF");
        check(notepad::has_rtf_extension("Notes.RTF") && notepad::has_rtf_extension("a.rtf") &&
                  !notepad::has_rtf_extension("rtf") && !notepad::has_rtf_extension("a.rtfx"),
              "The .rtf extension is matched without case");

        // Word-like RTF: an 11-point body, a heading, bold, a link and a picture.
        const std::string word =
            "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Calibri;}}{\\*\\defchp \\fs22}"
            "\\pard\\outlinelevel0\\fs32 Notes\\par\\pard\\plain\\fs22 Some {\\b bold} text, "
            "{\\field{\\*\\fldinst HYPERLINK \"https://example.com\"}{\\fldrslt here}}."
            "{\\pict\\pngblip 89504e47}\\par}";
        const notepad::RtfImport imported = notepad::import_rtf(word);
        check(imported.markdown == "# Notes\n\nSome **bold** text, [here](https://example.com).\n",
              "RTF converts to the Markdown dialect");
        check(imported.losses == "1 picture was skipped.\n", "Losses are reported in plain words");
        check(imported.body == "11 pt (the document's default character format)",
              "The body size and its rule are reported");

        // Windows Forms' RichTextBox writes an 8.5-point body: plain text.
        const notepad::RtfImport control = notepad::import_rtf(
            "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Microsoft Sans Serif;}}\\pard\\f0\\fs17 "
            "Status: {\\b OK}\\par}");
        check(control.markdown == "Status: **OK**\n" && control.losses.empty(),
              "An 8.5-point body imports as plain text with nothing lost");

        bool refused = false;
        try {
            static_cast<void>(notepad::import_rtf("{\\rtf1\\ansicpg1251 \\'e0\\par}"));
        } catch (const std::runtime_error &failure) {
            refused = std::string(failure.what()).find("Windows-1252") != std::string::npos;
        }
        check(refused, "A byte needing another code page is refused with an explanation");
        refused = false;
        try {
            static_cast<void>(notepad::import_rtf("{\\rtf1 unbalanced"));
        } catch (const std::runtime_error &failure) {
            refused = std::string(failure.what()).find("not readable RTF") != std::string::npos;
        }
        check(refused, "Malformed RTF is refused with an explanation");
        std::cout << "RTF import tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
