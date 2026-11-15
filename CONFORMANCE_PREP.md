# Conformance preparation (claude/conformance-prep)

This branch gets SwiftEdit ready for the GUI.Forms conformance rebase, so the rebase only moves pins. Nothing on it
has been compiled.

- **Base:** `claude/swiftedit-new-standard` e411177 (open PR #10).
- **GUI.Forms target:** the integrator's `claude/batch-cp33` db7ce9306 (package version still 0.6.0). Items 1 to 5
  were made against main 96d36969 (cp29); items 6 and 7 bring the branch to cp33. The batch was rebuilt since
  3b76c159a, which items 6 and 7 were first read against; the public C++ headers are the same at both.
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

6. **cp33** (7594ebd):
   - **XML forms:** `forms/main_window.form` is converted. The `.form` highlighter is a new XML lexer
     (`src/highlight/xml_language.*`) with two dialects, form documents ("form") and composite control documents
     ("control", `.control`). The CSS form-document dialect and the outline's object rules are retired. The
     Language menu, the file filter and the screenshot sample follow.
   - **Font pack stage 4:** the faces are compiled into GUI.Forms and `GUIForms_FONT_DIR` holds their licences only.
     Bundles and packages carry them in `licenses/fonts`; the native fixtures copy no faces; the launchers,
     presets and Build-Native stop setting `GUI_FORMS_FONT_DIR`.
   - **One module** (GUI.Forms 66f82629b, folded): GUI.Forms, its threading library and STXlib's state link
     statically into the executable. The build-tree copies of GUI.Forms' library (Frameworks on macOS, beside the
     executable elsewhere) are gone; `tools/Package-Mac.py` accepts a bundle with no Frameworks directory; the
     Windows and Linux packages follow the dependency closure, which now holds no first-party library.
   - **D15:** `main` sets `LC_CTYPE` from the environment on Linux, with the `std-rules: allow(locale-change)`
     marker.
   - **Static libc++ on Linux** (66556d959) comes through GUI.Forms' `cmake/llvm22.cmake`, which Build-Native
     already passes.
   - **Checked, nothing to change:** no SwiftEdit class derives from one of cp33's final classes, and no override is
     of a function cp33 made final. `WheelPhase` is appended last; SwiftEdit's own scroll views (CSV, Markdown,
     the code editor) build no `MouseEventArgs` by aggregate and treat every phase alike.
7. **QueryField on the content editing mode** (2ea5ef6). The edit part is a `ContentTextEditor`, `query.editing`,
   instead of a TextBox at an empty box:
   - it fills the field, is transparent to the pointer (hit testing, hover and the right-click toggle stay
     QueryField's), `set_multiline(false)` and `set_draws_selection(false)`, and carries the accessible name;
   - QueryField measures its labels through the window outside paint (`Window::measure_text_extent_utf8`) and
     submits a one-line layout for each revision and after each scroll: a cluster per grapheme, a caret stop at
     each boundary (a wildcard dot is narrower than its literal), the scroll applied;
   - QueryField keeps painting the text, selection and caret; the editor paints nothing without frames;
   - the edit filter (`set_edit_filter`) keeps the flags exact and enforces the 4096-byte budget, so a refused edit
     leaves no history; QueryField's own undo history stays the only one used.

8. **QueryField as a composite control** (this batch). `controls/query-field.control` declares the field: the face
   and edge parts (drawn by the theme), the `query-text` part (`paint="hook"`), the `content-text-editor` child
   `@editing` (fill, single line, no selection drawing, pointer-transparent, the accessible name), the focus
   target and `<semantics from="child(@editing)"/>`. `gui_forms_add_control` generates `notepad::QueryFieldControl`
   into a public include directory of `notepad_ui`, and `QueryField` derives from it:
   - `initialize_control_tree` makes the document's children, then finds `@editing` and subscribes to it (edit
     filter, text, selection, focus, layout requests), as before;
   - `paint_hook` draws the labels, the selection, the hover box and the caret, clipped to the part; the theme
     draws the frame, which QueryField painted itself before;
   - the focus override, `focusable = false` and the editor's property writes are gone: the document says them;
   - the layout, the flags, the history, the key preview and the pointer handling are unchanged.

## Composite focus, rechecked against cp33

The three gaps 336a43d found are closed by the content editing mode (item 7), not by composites:

1. **An edit part that holds focus without painting:** closed. The editor is a real region over the text area, and
   with no frame channel it paints nothing over QueryField (d80a6ec6d).
2. **The input-method caret:** closed. `ContentTextEditor::text_input_caret()` is the submitted layout's caret, which
   is QueryField's.
3. **Staying out of the semantic tree:** closed. The editor's text box node (text, selection, caret, text layout) is
   the field's only node; QueryField publishes no name.

**QueryField is a composite now** (item 8). GUI.Forms closed the three composite gaps this section reported
(`claude/composite-focus-gaps` 5c977d83f, folded into batch-cp33):

- **A.** `<semantics from="child(@editing)"/>` hands the composite's node to its focus target, so the editor's text
  box node is the field's one node.
- **B.** `multiline` and `draws-selection` are document properties of `content-text-editor`.
- **C.** A part declared `paint="hook"` is drawn by the typed class through `CompositeControl::paint_hook`; a class
  deriving from the generated one passes its definition through the generated constructor.

## The content editing mode plan

Done in 2ea5ef6 (item 7); the questions it raised were settled by cp33's d80a6ec6d: the edit filter exists, no-frame
paint draws nothing, measuring goes through the window outside paint, and a resubmit at the same revision is quiet.
Left for later: a description naming the flagged positions in the editor's node.

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
- cp33 (items 6 and 7):
  - `highlight_tests`: the registry (5 languages), the XML form and control lexing, multi-line state, incremental
    equality; `decoration_tests`; `editor_tests` `verify_language_menu` (Form and Control items);
  - `editor_tests` `verify_query_content_layout` (accepted layouts for a combining grapheme, a dot, typing and a
    scrolled field; the input caret at QueryField's caret; the editor's node), with
    `verify_query_occurrence_history` and `verify_query_editing_keys` unchanged;
  - `view_tests` query cases (paint measures only the baseline now);
  - item 8: `editor_tests` `verify_query_content_layout` (no node of the field's own, the document's focus target
    and editor properties, the hook part's box); the `query-field.control` generation at build time (the form
    compiler checks the document); `view_tests` query paint through the hook (`on_paint` draws the labels);
  - the app's form generation (`gui_forms_add_form`), the macOS native fixtures with faces from the pack, and the
    Linux and Windows packaging smoke runs.

## What still waits

- **The final pins:**
  - the GUI.Forms revision and the four cache digests in `ci/dependencies.json`;
  - the template_harness commit in `harness.lock`.
  - Both move at the conformance rebase. `prepare_dependencies.py` requires `harness.lock` to equal GUI.Forms' own.
- **STXlib boundary types** (the swap-in): the `std::string` and `std::vector` crossings of the GUI.Forms API move
  when the owned types land.
- **Font family:** the bundled body face selection. SwiftEdit's editor and views use the theme's faces.
- **Release tooling:** queue note 32cf3d9. The `tools/` packaging scripts still build and package the retired
  FileManagerDocumentPicker; fix that before a release run.

## How the integrator builds it

```sh
# The SDK, from a GUI.Forms checkout at batch-cp33 db7ce9306 (or the conformance release):
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
- QueryField (2ea5ef6): the order of `arrange`, the label measure and the first `layout_requested`; whether a
  pointer press on the field (the editor is pointer-transparent) places the caret as the tests expect.
- QueryField as a composite: whether `CompositeControl`'s own mouse raisers, which run after QueryField's, leave
  the press handled; whether the theme's face and edge for an `editor` family composite look like the old paper and
  border.
- The XML lexer's colours for unusual documents (a value spanning lines, a comment opened inside a tag's line).
- The word-move offsets the new test expects (`one Ytwo Xthree`) follow the engine's word starts as I read them.

## Linux package: the programs alone (owner decision, 2026-11-15)

The owner: "we dont build against any distro"; STXlib is to carry the C library; X11 and XWayland support is
sufficient for now. `tools/Package-Desktop.py` no longer copies the ldd closure into `lib/` (nor the Debian
copyright notices of copied libraries) and the launchers no longer set `LD_LIBRARY_PATH`. Each Linux program
must import only system libraries (the C library, its loader, libgcc_s, libutil for the terminal, and GUI.Forms'
X11, ATK and GLib host libraries), with every `ldd` line in a system directory; the build-tree RUNPATH is
removed. Tests: `tests/test_package_imports.py` (run in the publication contract step; passes here), and the Linux
package step with its packaged startup checks.

Windows (coordinator, option a): the DLL copy is now a refusal. Each program may import only DLLs present in
System32 and API sets; GUI.Forms, the codecs and the LLVM runtimes must be linked in, so nothing is copied
beside the programs. `tests/test_package_imports.py` covers both platforms' classification.
Risk: a SwiftEdit dependency linked shared on Linux (md4c, stx_rtf2md, stx_png) now fails packaging instead of
being copied.
