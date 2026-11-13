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
     - the old bodies are now public `handle_pointer(PointerEvent&)` and `handle_key(KeyEvent&)`;
     - protected raisers feed them: `on_mouse_down/up/move/wheel` through `pointer_event_of`; `on_mouse_leave` with
       a leave event at the target; `on_key_down` through `key_event_of`, after the base and only while the key is
       unhandled.
     - Each raiser copies `handled` back and calls the base raiser, so MouseDown and MouseUp subscribers now run.
   - The tests drove `on_pointer` and `on_key` directly. They now call `handle_pointer` and `handle_key`
     (`tests/view_tests.cpp`, `editor_tests.cpp`, `native_tests.cpp`, `csv_view_bench.cpp`).
   - One test wheeled a stock `gf::TextBox`, whose raisers are protected. It now dispatches through its window
     (`window.dispatch_pointer`).
4. **QueryField's private TextBox (interim).**
   - QueryField handed keys to its hidden child `gf::TextBox` with `(*edit_).on_key(event)`. That entry is gone, and
     TextBox is final with protected raisers, so no key can be handed to it now.
   - `QueryField::edit_key` applies the editing keys a one-line query needs, through the child's public calls, by
     grapheme (`store_` mirrors the child's text):
     - Left and Right (Shift extends);
     - Home and End;
     - Backspace and Delete;
     - Control or Command with A, C, X and V.
   - Typing still reaches the child through `on_text_input`, which is public, and QueryField's own undo and flag
     toggles are unchanged.
   - Lost until GUI.Forms gives an embedded editor a key entry: word moves (Alt/Control+arrow), and any other text
     key the engine knew.
   - **This needs a decision** (below).
5. **API drift:** two checks found nothing else to change.
   - Identifier check: every identifier that exists in GUI.Forms' headers at the old pin (ffbdc2a7, cp25) but not at
     96d3696 was searched for in SwiftEdit. Apart from `on_key`/`on_pointer`, the matches were SwiftEdit's own names
     (`handle_`, `encoded_size`, `caret_visible`).
   - Export check: every `gf::` name SwiftEdit uses is exported or header-only. That includes
     `Theme::FieldProvenance` and `PartProvenance`, which are marked `GUI_FORMS_API`.

## Decision needed (QueryField keys)

1. **GUI.Forms adds a public entry** that hands one key to an embedded control. Two shapes would do: a TextBox
   `process_key(KeyEventArgs&)`, or a public form of `detail::ControlInputAccess`. QueryField then forwards again and
   `edit_key` is deleted. Recommended: it is small, and every composite editor needs it.
2. QueryField moves onto cp32's `ContentTextEditor`, which fits QueryField's own grapheme layout. But the editor is
   then the focused control, and QueryField takes its flag and history shortcuts in `on_key_preview`. That is a larger
   rewrite.
3. Keep `edit_key` as the permanent key set (word moves stay missing).

## Tests that should cover it

- `view_tests`: CSV grid hover, wheel and keys; Markdown keys and links; query toggle and right-click.
- `editor_tests`: the wheel test through the window, and the CSV context press.
- `native_tests`: the CSV context press.
- `code_editor_tests`: MarkerStrip activation.
- Not covered by any test yet: QueryField's Backspace, Delete, arrows and clipboard keys through `edit_key`. Add
  one when the decision above is made.

## What still waits

- **The final pins:**
  - the GUI.Forms revision and the four cache digests in `ci/dependencies.json`;
  - the template_harness commit in `harness.lock`.
  - Both move at the conformance rebase. `prepare_dependencies.py` requires `harness.lock` to equal GUI.Forms' own.
- **The QueryField key entry** (decision above), which may arrive with cp32 or cp33.
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
