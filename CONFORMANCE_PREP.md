# Conformance preparation (claude/conformance-prep)

This branch gets SwiftEdit ready for the GUI.Forms conformance rebase, so the rebase only moves pins. Nothing on it
has been compiled.

- **Base:** `claude/swiftedit-new-standard` e411177 (open PR #10).
- **GUI.Forms target:** main 96d36969311722531a6a41828b555c2e9fa9261e (checkpoint cp29 merged; package version 0.6.0).
- **Pins:** not moved. `ci/dependencies.json` (`pin_note`) and `harness.lock` carry a PLACEHOLDER marker.

The theme editor (malkuth-tools) builds `swiftedit_highlight` and `swiftedit_code_editor` from this repository, so
its prep branch is built against this one (`-DSWIFTEDIT_SOURCE_DIR`).

## What was adapted

1. **Package version:** `find_package(GUIForms 0.6 ...)` (was 0.2, which 0.6.0 refuses).
2. **Includes:** none to move. SwiftEdit includes only the umbrella headers; `gui_forms.hpp` still reaches
   `theme.hpp`.
3. **Target-phase raisers** (GUI.Forms 56b6627ea: `Control::on_pointer` and `Control::on_key` are gone):
   - `CsvView`, `MarkdownView`, `QueryField` and `MarkerStrip`:
     - the old bodies are now public `handle_pointer(PointerEvent&)` and, for CsvView and MarkdownView,
       `handle_key(KeyEvent&)`;
     - QueryField has no key handling of its own any more (point 4);
     - protected raisers feed them: `on_mouse_down/up/move/wheel` through `pointer_event_of`; `on_mouse_leave` with
       a leave event at the target; `on_key_down` through `key_event_of`, after the base and only while the key is
       unhandled.
     - Each raiser copies `handled` back and calls the base raiser, so MouseDown and MouseUp subscribers now run.
   - The tests drove `on_pointer` and `on_key` directly. They now call `handle_pointer` and `handle_key`, or
     go through the window for QueryField (`tests/view_tests.cpp`, `editor_tests.cpp`, `native_tests.cpp`, `csv_view_bench.cpp`).
   - One test wheeled a stock `gf::TextBox`, whose raisers are protected. It now dispatches through its window
     (`window.dispatch_pointer`).
4. **QueryField is a composite** (GUI.Forms composite focus, cp25):
   - Its private `gf::TextBox` is the edit part. `focus_target()` returns it, and QueryField keeps `focusable` false.
     So `request_focus(query)` and keyboard traversal give the TextBox the focus, and the window reports QueryField as
     the active control.
   - The TextBox receives keys, text input and the clipboard itself. Nothing is forwarded.
   - The TextBox stays visible, so it can hold focus, but it is arranged to an empty box (`{0, 0, 0, 0}`).
     QueryField still paints the query and takes the pointer.
   - QueryField takes its own keys in `on_key_preview`, which reaches it before the edit part:
     - Control+Shift+/ toggles the flag at the caret;
     - Control or Command with Z or Y (Shift+Z redoes) moves through its own history.
     - The TextBox's undo history is cleared after every edit, as before.
   - The flags follow the text through the TextBox's edit filter, which reports each user edit's range before it
     applies; the following `text_changed` moves the flags.
     - A replacement by the same bytes raises no `text_changed`, so it is applied in the filter.
     - A change the filter did not see falls back to the smallest diff, starting at a grapheme boundary.
   - Removed: `edit_key`, `handle_key`, QueryField's `on_key_down`, `on_text_input` and `on_focus_changed`, and the
     direct calls into the TextBox. QueryField's focus state comes from the TextBox's `focus_observed`.
5. **API drift:** two checks found nothing else to change.
   - Identifier check: every identifier that exists in GUI.Forms' headers at the old pin (ffbdc2a7, cp25) but not at
     96d3696 was searched for in SwiftEdit. Apart from `on_key`/`on_pointer`, the matches were SwiftEdit's own names
     (`handle_`, `encoded_size`, `caret_visible`).
   - Export check: every `gf::` name SwiftEdit uses is exported or header-only. That includes
     `Theme::FieldProvenance` and `PartProvenance`, which are marked `GUI_FORMS_API`.

## What composite focus does not express (for GUI.Forms)

QueryField paints its own text (flagged graphemes as dots) instead of showing its edit part. Composite focus covers
the keys: the preview phase reaches the composite first, and TextBox's `accepts_enter`, `accepts_escape`, `committed`
and `cancelled` cover Enter and Escape. Three things are missing:

1. **An edit part that holds focus without painting.** Focus requires `eligible_for_input()`, which requires
   `effectively_visible()`. So QueryField keeps the TextBox visible and arranges it to an empty box.
   - If an empty-box control is not eligible, or the TextBox misbehaves at zero size, focus fails. Only a build shows
     which.
   - Wanted: a supported way for a composite to own the painting of its edit part's text, such as a non-painting
     edit part.
2. **The input-method caret.** The focused control's `text_input_caret()` is the TextBox's, at its empty box, so
   composition and candidate windows open at the field's left edge, not at QueryField's caret.
   - Wanted: a way for the composite to answer `text_input_caret` for its edit part. For example, SurfaceRoot could
     ask the active control (the composite) before the focused one.
3. **Accessibility.** The visible TextBox may now be published as its own text box node, beside QueryField. The
   SearchField header says its editor "is not published separately", but I found no public way for a consumer's
   composite to keep its edit part out of the semantic tree while it publishes the text and selection itself.
   - Wanted: that public way, or the rule SearchField relies on, stated for consumer composites.

## Tests that should cover it

- `view_tests`: CSV grid hover, wheel and keys; Markdown keys and links.
  - The query toggle now goes through the window's key preview, and the text input through `dispatch_text`.
  - The query right-click.
- `editor_tests`:
  - the wheel test through the window, and the CSV context press;
  - `verify_query_occurrence_history`: flags across insertion, deletion, Backspace, paste, an identical paste over a
    flag, the byte budget, and undo/redo through the field's history;
  - new `verify_query_editing_keys`, through the window with focus on the edit part:
    - the edit part is focused and QueryField is the active control;
    - Left, Right, Home, End;
    - Alt+Left and Control+Right word moves;
    - Shift+Left selection, Backspace and Delete (also on a combining grapheme);
    - Control+A, C, X and V;
    - Control+Z reaches the field's history.
- `native_tests`: the CSV context press.
- `code_editor_tests`: MarkerStrip activation.
- `query_edit_bench`: unchanged; it already drives the field through the window.

## What still waits

- **The final pins:**
  - the GUI.Forms revision and the four cache digests in `ci/dependencies.json`;
  - the template_harness commit in `harness.lock`.
  - Both move at the conformance rebase. `prepare_dependencies.py` requires `harness.lock` to equal GUI.Forms' own.
- **The three composite-focus gaps above**, if GUI.Forms takes them.
- **cp33:** the final-virtuals cleanup (more overrides may become final).
- **STXlib boundary types** (the swap-in): the `std::string` and `std::vector` crossings of the GUI.Forms API move
  when the owned types land.
- **Font family:** the bundled body face selection. SwiftEdit's editor and views use the theme's faces.
- **Release tooling:** queue note 32cf3d9. The `tools/` packaging scripts still build and package the retired
  FileManagerDocumentPicker; fix that before a release run.

## How the integrator builds it

```sh
# The SDK, from a GUI.Forms checkout at 96d36969 (or the conformance release):
python3 -B tools/build_sdk.py --target macos-arm64 --prefix <sdk> --features all
# SwiftEdit against it:
cmake -S <swiftedit> -B <swiftedit>/build -G Ninja \
  -DCMAKE_PREFIX_PATH=<sdk> -DCMAKE_CXX_COMPILER=<llvm22>/bin/clang++
cmake --build <swiftedit>/build -j5
ctest --test-dir <swiftedit>/build --output-on-failure
```

- Or use a lockstep record: `SWIFTEDIT_LOCKSTEP=<record.json>`, with `tools/Build-Native.py`.
- Do not run `prepare_dependencies.py` with this branch's pins: they are the placeholder (cp25).

## Risks not checked without a build

- Calls on temporaries to accessors that now delete their `&&` overload.
- Signature drift in calls whose names survived.
- Whether `window.dispatch_pointer` reaches the TextBox at the test's point, and not a scrollbar child.
- Whether a TextBox arranged to an empty box can take focus (gap 1).
- The word-move offsets the new test expects (`one Ytwo Xthree`) follow the engine's word starts as I read them.
