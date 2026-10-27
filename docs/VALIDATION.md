# SwiftEdit 0.2 checkpoint — 2026-09-30

Measured locally with GCC 16.2.0, Release C++20, Ninja, Windows x64.
Command: `tools/Build-Windows.ps1 -BuildDirectory C:/Users/Shadow/notepad/.build/swiftedit-dbe3766 -StageDirectory C:/Users/Shadow/notepad/dist/SwiftEdit -NativeTests`.

All six suites passed in 2.57 seconds:

| Suite | Seconds | Evidence |
|---|---:|---|
| document | 0.07 | Unicode/encoding round trips, safe publication, conflicts, read-only and hard-link refusal |
| session | 0.17 | Exact-context ambiguity, stale commits, undo/save boundary, opened restore, malformed bytes, sanitized copy, transport, naming, 16 MiB threshold and bounded pages |
| csv | 0.04 | Quoting, embedded newlines, local serialization, rectangular clear, exact decimals, explicit rounding, strict errors and reference metadata |
| cli | 0.59 | Real stdin/stdout process, explicit preview/commit, saved bytes, dirty-open refusal, exact CSV results and no autosave on quit |
| editor | 0.12 | Native-control headless input, save boundary, original restore, newline-conversion undo, large-paste cancellation, existing menu behavior |
| native | 1.57 | Self-closing owned windows, actual save, Find/Replace/undo, picker cancellation/reopening, clean shutdown |

Frozen SDK pair: `C:/Users/Shadow/file_manager/.build/sdk-checkpoints/dbe3766/windows-x64/{gui-forms-sdk,picker-sdk}`.
Combined SDK fingerprint: `CD8F39DA910556E56B401F783727BB4370BB363EBAE2F8E60B7C850908CEB654`.
Application DLL: `45CB7BB9267686F5E93333952F691B55F2C6DB0AA80D256EF9DF3DF2AB8C12EA`.
SwiftEdit.exe: `07FD6819BCDCE3D5F89F5C3B367C0CECC5200AA8BD218C37455BA6B18D90AE69`.
swiftedit-cli.exe: `FAECC56C7D59590E1852035AE940F148757756C180F6F89B857EA0031F512370`.

The GUI.Forms owner supplied and tested the additive clear_undo_history API;
the parent published a separate matching provider/picker checkpoint. No provider
source was modified by this consumer task. The original Notepad and Notepad-dpi
executable hashes remain respectively `9E4F5E85845E05AB74D0B7EE8100BAB18D78001EB30D658A1A2952AF59787C7B`
and `624A7A259C8C09CB86A991A9A746A04DF854909B6AA1B833E7A38DADE1B5AEC5`.

These are development tests, not full UI acceptance or cross-platform evidence.
The CLI/core large-file and CSV features are not yet present in the native GUI.
No 500 MB stress benchmark, full-screen terminal, visual CSV grid, Markdown
renderer, print pipeline, complete metadata editor, multi-selection or Unicode
control-glyph view is claimed. See SWIFTEDIT_OBJECTIVES.md for the remaining work.

---

## Historical Notepad evidence
# Windows development validation

## DPI follow-up checkpoint

The DPI-fixed public GUI.Forms SDK and matching clean rebuilt picker package
were consumed in a fresh build directory, preserving the original package:

```powershell
./tools/Build-Windows.ps1 -BuildDirectory C:/Users/Shadow/notepad/.build/windows-dpi -StageDirectory C:/Users/Shadow/notepad/dist/Notepad-dpi -NativeTests
```

**MEASURED:** all 3/3 suites pass in 2.44 seconds (document 0.08 s, editor
0.38 s, native 1.96 s). The consumer test additionally checks that scale
transitions 1.0 → 1.5 → 2.0 → 1.0 preserve document text and selection.

The provider independently reproduced the stale glyph-metric cache bug and
added device scale to the multiline cache key. Its focused regression covers
remeasurement, wrapping, row height, hit testing, caret scrolling and warm-cache
reuse; the provider reports 65/65 toolkit tests passing. The consumer test is
an integration state-preservation check, not a substitute for that metric oracle.

Updated launch path: `C:/Users/Shadow/notepad/dist/Notepad-dpi/notepad.exe`.

- EXE SHA-256: `624A7A259C8C09CB86A991A9A746A04DF854909B6AA1B833E7A38DADE1B5AEC5`
- GUI.Forms DLL SHA-256: `3C66E1C188A183EFF2C93833C7C9A3781DE57936A14721F00FA9DDBC865C664F`

The original `dist/Notepad` executable and DLL were not replaced. Build/staging
paths are configurable, and the build script refuses directories currently used
by a Notepad process. The source feature set and provisional text/save decisions
are unchanged.

## Original checkpoint

Date: 2026-09-29, Shadow Windows host, MinGW GCC 16.2.0, Release C++20.

Final clean command: `tools/Build-Windows.ps1 -NativeTests`.

**MEASURED:** 3/3 Notepad CTest suites passed in 2.03 seconds:

| Suite | Result | Coverage |
|---|---|---|
| document | Pass, 0.10 s | Strict UTF-8/UTF-16 round trips, BOM and mixed newline preservation, supplementary/combining characters, malformed/binary input refusal, literal search/replacement, create/replace, exact external conflict, read-only/hard-link refusal, deleted source, competing writer, failed-open preservation, UTF-16 save and successful temporary cleanup |
| editor | Pass, 0.15 s | Public multiline control input, configured CRLF Enter, menu undo/redo/cut/copy/paste/select-all/save/wrap, clean/dirty save boundary, mixed endings, oversized-line rejection, unsaved Cancel/Discard and failed Save cancels New |
| native | Pass, 1.77 s | Public Windows Application lifecycle, actual file save, owned Find/Replace and Font windows, literal Find and Replace All with single undo, shared Open/Save As cancellation/reopening, owner command restoration, all test windows close |

All file mutation fixtures are uniquely named disposable files. The native test
uses only its own application windows and public lifecycle/control calls. No
global keyboard, pointer, or desktop automation was used. Native tests are
opt-in because they briefly show self-closing application windows.

An initial harness assumed `request_close()` was synchronous. Native observation
showed a timer tick could arrive before the posted close event. The corrected
test waits for each actual closing callback before checking restored state.
This correction required no provider patch.

## Dependency checkpoint

Only installed public packages are consumed:

- GUI.Forms: `C:/Users/Shadow/file_manager/gui_forms/.build/shadow-sdk`
- File Manager: `C:/Users/Shadow/file_manager/.build/native-windows-x64/frontend-sdk`
- Toolchain: `C:/Users/Shadow/plan-paint/build-deps/msys64/mingw64/bin`

Provider-reported prerequisite evidence: GUI.Forms 65/65 tests, clean rebuilt
File Manager frontend 11/11 tests, independently installed picker consumer 1/1.
Those are provider reports; the three Notepad suites above were run here.

The multiline TextBox extension changes C++ object layout. Both picker and
Notepad were clean rebuilt against the new SDK. `Build-Windows.ps1` hashes all
installed headers, libraries and GUI runtime DLLs; a new checkpoint forces a
clean consumer build, and changes during tests/staging fail packaging. The exact
verified combined fingerprint is staged in `dist/Notepad/sdk-fingerprint.txt`.

The staging script recursively inspects executable/DLL imports and copies the
non-system runtime dependency closure. Fonts and their bundled attributions/
licenses accompany the application. The source is its own local repository;
there is no remote publication or installer/association side effect.

## Scope of the evidence

These tests establish the exercised development behavior, not an exhaustive
native acceptance review. Manual keyboard-only use, all DPI/font/script cases,
screen readers, complete IME/bidi caret behavior, large/slow filesystem latency,
power-loss recovery, broad metadata/ACL behavior and other operating systems
remain unverified. See KNOWN_GAPS.md and DECISIONS.md for precise restrictions
and the future product interview entrypoint.

## 2026-10-01 resumed implementation

Repository ownership resumed on explicit owner direction. Prior checkpoint
`e9d2715` was clean and matched origin/master on falseywinchnet/swiftedit.

Implemented Document > Word Count with document and optional selection counts,
plus the shared CLI word-count operation. Counting uses Unicode whitespace runs;
punctuation remains within a run. It is observational and does not change text,
selection or save state. Invalid UTF-8 is refused. Paged whole-document counts and
language-specific segmentation remain pending.

Clean Release build: .build/swiftedit-resumed; stage: dist/SwiftEdit-resumed.
Consumed the matching house-style-final GUI and picker SDK pair. Five headless
suites passed: document 0.49 s, session 0.59 s, CSV 0.07 s, CLI 1.15 s, editor
1.35 s; total 3.72 s. Native smoke executable compiled but was not run because
desktop launches require coordination. Thus the prior final-SDK build/headless
integration gap is closed; native desktop validation against it remains pending.
C++ spelling audit: zero findings in 17 files. git diff --check: clean.

Tests cover empty/whitespace text, CR/LF, apostrophes/hyphens, combining accents,
emoji, Unicode whitespace, zero-width space, unsegmented CJK, malformed input,
GUI document/selection reporting and state preservation, and the CLI response.

## 2026-10-01 full-feature continuation checkpoint

Native CSV/formula/source-mapping checkpoint a4fa27d was followed by Markdown
parser/native preview work. The first paint test found a zero-extent scrollbar
range error; fixed in both Markdown and single-row/column CSV handling.
Final full build passed all eight headless suites against house-style-final:
document 0.30 s, session 0.19 s, CSV 0.02 s, display 0.02 s, Markdown 0.02 s,
CLI 1.22 s, editor 0.26 s, views 0.13 s; total 2.18 s.
Native desktop QA, full semantic style review of this new scope, and the final
measured lag scan remain pending. See RESTART_HANDOFF.md. No updated release ZIP
or stage was published. Frozen SDKs and prior distributed packages are unchanged.

## 2026-10-01 owner-revoked freeze; active goal

The owner corrected the historical shutdown instruction and explicitly authorized
a sustained full-feature goal. Parent acknowledged the correction. Work resumed.

Fixed Markdown height-only resize scroll extents, artificial one-pixel scrolling
when content fits, and persistent error text after a successful layout retry.
Fault-injection and resize tests verify these behaviors, including no remeasurement
on height-only expansion. CSV reuses cached visible results when source and viewport
are unchanged; source replacement invalidates before scrollbar callbacks. Viewport
changes hide obsolete hover tooltips.

Session now exposes nonzero process-wide document identities and identity/revision
stamps for deferred edits. Open/reset changes identity; editing/undo/save preserves
it; failed open preserves the complete stamp. Tests reject a foreign document stamp
even with equal revisions. Allocation refuses identity exhaustion rather than wrapping.
Provider request-token ownership remains a separate adapter obligation.

Source/display boundary lookup now uses binary search over ordered mappings.
Regression tests round-trip every unit of a 65536-byte page with alternating
ordinary and expanded illegal-byte labels, and reject scalar/CRLF interiors.

Release build and all eight headless suites passed: document 0.10 s, session 0.22 s,
CSV 0.05 s, display 0.07 s, Markdown 0.05 s, CLI 0.81 s, editor 0.12 s, views 0.14 s;
total 1.58 s. Spelling audit: zero findings in 30 first-party C++ files. These are
test timings, not the final responsiveness certification. Native desktop validation,
remaining feature implementation and the final lag/bug scan are still pending.

## 2026-10-01 wildcard query UI

Added a native query field retaining literal grapheme text separately from wildcard
flags. A flagged character draws a dot; hover outlines its slot; right-click and
normalized Ctrl+Shift+slash toggle the current slot. The public toolkit TextBox
supplies editing/clipboard behavior through a private child; QueryField owns visible
painting, focus routing and hit geometry. Edited characters start literal; unchanged
prefix/suffix positions retain flags. Find Next/wrap and single/all replacement use
actual matched source ranges, including multibyte and combining graphemes.

Focused tests cover literal punctuation, keyboard/right-click flag toggles, unchanged
literal source, dot rendering, Unicode input and flag preservation across text edits.
Editor tests exercise literal search, Unicode wildcard matches and wrap after rejecting
a two-character gap. Replacement core tests verify variable byte lengths and output
capacity refusal. Full Release build and eight suites passed in 1.16 s; C++ spelling
audit zero findings in 32 first-party files.

Native shortcut support is not yet proven: inspected active Windows provider source
returns unknown for VK_OEM_2. Provider received that finding and owns normalization
changes for a future matched SDK. Native keyboard/focus/IME and semantic accessibility
validation remain pending. Search scanning still runs synchronously through its
slices; interruptible UI orchestration and final lag measurements remain required.

## 2026-10-01 cooperative Find Next

PatternScan owns its source/query snapshot and resumes inside a candidate or literal
UTF-8 grapheme. Its work budget charges byte comparisons, so a long shared prefix
cannot evade the slice bound. Find Next processes 4096 operations per slice (up to
two slices when wrapping), then schedules a frame callback. No growing callback
queue is created. A source edit/open/reset, query flag/text change, case change,
caret movement, Find close or editor disposal cancels work before publishing a
selection. Single Replace now tests only the selected candidate, avoiding a search
through the rest of a mismatching selection.

Tests cover yielding mid-candidate and mid-UTF-8 sequence, adversarial repeated
prefixes, restart state, and cancellation via the actual Window frame scheduler
after query/caret changes. Full eight-suite run passed in 1.72 s; after the last
cancellation refinements, affected display/editor suites passed in 0.17 s. Spelling
audit zero findings in 32 authored files. Snapshot preparation and final publication
still require measured latency evidence; Replace All orchestration remains a
separate unfinished synchronous path.

## 2026-10-01 incremental Replace All

ReplacementScan owns the source/query snapshot, reserves bounded output once,
and charges both search comparisons and output-copy bytes to each work slice.
It keeps partial output private and disallows consumption before completion.
Matches do not overlap; empty replacements and unmatched tails follow the same
state machine. GUI Replace All schedules continuation through the frame scheduler,
cancels on changes to source/query/case/selection/replacement, validates the finished
result, then applies one undoable edit. No document edits occur during preparation.

Core tests compare one-operation slices against Unicode wildcard results, reject
premature result access, and verify nonoverlapping deletion and tail retention.
Editor tests verify pending work leaves content unchanged, replacement edits cancel,
scheduled completion produces the exact result, one undo restores the source,
and an oversized logical line preserves source and selection. All eight headless
suites passed in 1.84 s; C++ spelling scan has zero findings in 32 authored files.
Snapshot construction and final validation/publication still run synchronously
and remain part of the final measured lag scan; native performance is not certified.

## 2026-10-01 Unicode catalog and character inspection

Added pinned Unicode 17.0.0 UnicodeData and NameAliases inputs with SHA-256 checks,
upstream license/provenance and a deterministic offline generator. The immutable
catalog uses binary search across 40555 assigned records/ranges. Hangul and unified
ideograph range names are generated from code point values; private-use entries
identify font/agreement-dependent meaning. Ordinary/control categories are separate;
surrogates, unassigned values and noncharacters are excluded from insertion encoding.

Document > Inspect Characters reports code points, names/categories and exact UTF-8
bytes for a selection or the caret's full grapheme. Output is bounded to 32 scalar
values/illegal bytes and emits no source controls. Specific explanations exist for
common controls; other entries currently have category explanations. Separate picker
dialogs and their insertion workflows are not yet implemented.

Core tests cover ASCII, C0/C1 aliases, bidi controls, Hangul range endpoints, CJK,
supplementary encoding, NUL preservation, invalid scalar input, category filtering,
safe illegal-byte inspection and bounded output. GUI test verifies combining-grapheme
inspection and unchanged source/selection. Nine suites passed in 2.09 s; final affected
character/editor tests passed in 0.34 s. Generator rerun succeeded against pinned hashes.
Packaging/install definitions now include Unicode and MD4C licenses; no new binary
stage or native desktop validation is claimed at this checkpoint.

## 2026-10-01 separate character pickers and native lifecycle

Added owned Unicode and control picker dialogs. Unicode browsing retains at most
256 code points per page with direct code-point entry. Controls use a bounded
catalog and explanatory labels; source controls never enter the dialog preview.
Insert preflights document/line capacity and uses normal undoable source replacement.
Copy uses the shared host clipboard. NUL copy is explicitly refused before clipboard
mutation because the Windows plain-text format terminates at NUL; Insert supports it.
The file picker hides both character dialogs and rejects insertion while it is active.

Headless tests cover supplementary insertion, control insertion, exact undo,
ordinary/control separation, copied supplementary text, NUL clipboard refusal and
exact NUL insertion/undo. Nine-suite run passed in 2.34 s; subsequent affected
character/editor checks passed, including the final editor run in 0.12 s. Spelling
scan zero findings in 37 authored C++ files.

Parent approved an exclusive short native desktop slot. The self-closing native
test created the main window plus six owned dialogs, exercised save/Find/Replace/
undo, file-picker cancel/reopen, then showed both character dialogs, inserted a
supplementary scalar and U+200B respectively, undid each exactly, closed both and
exited cleanly. Exit code 0. It used public control/window callbacks, no global
keyboard/mouse automation. Only the owned process was launched; a 20-second
process-handle timeout guard was unused. Slot released afterward.

Executable: .build/swiftedit-resumed/notepad-native-tests.exe
SHA-256: DDF8797A7F6692C6D1412CAE70350EC6E70C069E12C0BF94E4CCA596262855E4
GUI runtime: .build/swiftedit-resumed/libgui_forms_application.dll
SHA-256: C9E79914273562042AE38BFFDFDD296B6CA7C2BFA0F28BF4D4B2EFDA9DBB9336
Consumed installed SDK pair: house-style-final, Windows x64.
Local native log: .build/swiftedit-resumed/native-20261001-014002.stdout.txt

This is native lifecycle/operation evidence, not screenshot-based visual QA,
font coverage certification, physical Ctrl+? verification, or a final lag scan.
Document-wide inert control glyphs still require the new provider document view.

## 2026-10-01 mixed-ending save choice

Added an owned Save As-Is / Convert to Document Default / Cancel dialog. A pending
save retains the destination snapshot, document revision and continuation; editor
commands and character insertion are suppressed while that choice is active.
Cancellation restores editor authority without modifying source/history/disk.
Save As-Is preserves exact mixed bytes. Conversion prepares/validates a replacement
without mutating the editor, publishes against the original snapshot, then displays
the saved content and clears undo. A post-publication display failure explicitly
reports that disk was saved and retains the saved snapshot; it is not described as
a failed disk write. The future shared-session GUI will simplify this boundary.

Tests verify pending choices do not write, cancellation preserves undo, as-is exact
bytes, normalized content matching disk, and destination changes during the dialog
refusing publication while preserving source/undo. Full nine-suite run passed in
1.72 s. Final affected editor rerun passed in 0.25 s. Spelling scan zero findings
in 37 authored files.

A newly coordinated desktop slot exercised the eight-window native build. The
mixed-ending dialog showed, cancelled/re-enabled the editor, reopened and saved
mixed bytes as-is, then reopened and converted to the document's CRLF default.
The test verified both disk bytes and displayed text. Previous save/find/replace,
character-picker and file-picker lifecycle routes also ran; clean exit code 0.
The exact owned process had a 20-second timeout guard (unused), no global input,
and the desktop slot was released after completion.

Native smoke SHA-256:
102A7F09DA6841A7563AC9E53126893E24F07BA1EB4A87B55F8F42CAD7CFABDE
Log: .build/swiftedit-resumed/native-20261001-014814.stdout.txt
SDK pair: unchanged house-style-final Windows x64; GUI runtime identity matches
the preceding native receipt. This verifies lifecycle/operations, not visual
layout or a final latency certification.
Full external-conflict two-stage filename/metadata/copy workflow remains unfinished.


## Conflict save review - 2026-10-01

Added `save-review` suite for two-stage authority, stale document identity,
invalid encoding revocation, destination races, versioned-copy collision refusal,
encoding/ending publication and one-use consent. Editor tests exercise actual
owned filename/format controls, changed fields requiring another review,
UTF-16 BE/CRLF new-copy publication, original preservation and undo boundary.
All ten headless suites passed in 1.49 s. C++ spelling audit: zero findings across
41 authored files. Semantic review checked weak callback ownership, private
preparation, consent revocation, expected snapshot publication and failure
preservation separately.

Native nine-window lifecycle smoke exited 0 using only its own public callbacks
and disposable fixture. It exercised the new active conflict dialog, reviewed
Save Over and restoration of the owner alongside prior dialog/save tests.
Binary SHA-256: `F302F7274B9492ACECB7DF21CBF08B88791E0F6E7BEC37B9D383D6B216353B7C`.
Log: `.build/swiftedit-resumed/native-20261001-020127.stdout.txt`.
No physical keyboard, screen-reader, visual-layout or final latency claim follows
from this callback-driven smoke. Current SDK remains unchanged.

## Bounded blank-line context metadata - 2026-10-01

Eleven headless suites passed in 2.19 s after adding ContextCursor and CLI
context/context-next. Unit tests traverse mixed endings with every budget from
one byte through the fixture length, including CRLF splits, whitespace-only
lines, repeated EOF, reserved marker mutation refusal, stale revision and foreign
identity. A 16 MiB read-only fixture verifies bounded context and literal
marker-like source preservation. Process integration verifies open/context rows,
continuation and stale-read errors. C++ spelling audit has zero findings across
44 authored files; cursor lifetime, bounded allocation, progress publication and
source/metadata separation were reviewed separately. No native desktop run was
needed for this command/core change; no latency conclusion is drawn from suite
runtime.

## Terminal editing model - 2026-10-01

Added TerminalBuffer on the existing Session, with grapheme movement/selection,
CRLF-atomic traversal, preferred-column vertical/page movement, Home/End,
selection replacement, deletion, whole-line cut, undo/redo and shared save rules.
Navigation metadata is lazy and stamped by document identity/revision. Invalid
bytes from opened files have private one-byte control placeholders only in the
navigation copy; actual source and clipboard extraction remain byte-faithful.
New malformed UTF-8 insertions remain refused by Session. Pasting an internally
cut malformed sequence is therefore still an unresolved terminal behavior.

The terminal regression found an undo caret inside a restored multibyte scalar;
index-based grapheme snapping fixes it without passing an invalid scalar offset
to the provider. Twelve headless suites passed in 2.72 s. Spelling audit: zero
findings across 47 authored files; semantic review covered borrowed text,
revision cache, selection publication after successful edits, and dirty reset.

This is the editing model, not a completed terminal executable. Console screen,
input loop, safe Unicode cell layout, large-file terminal pagination, shortcuts,
search/wildcard controls and interactive validation remain. Full metadata rebuild
on a changed editable buffer is synchronous and needs measurement/optimization
before a responsiveness claim.

## Terminal cell layout - 2026-10-01

Pinned Unicode 17 EastAsianWidth.txt and emoji-data.txt alongside the existing
UnicodeData.txt and Unicode license. tools/Generate-TerminalUnicode.py verifies
all input hashes and generates 520 classification ranges. Regeneration is
identical (terminal_unicode.inc SHA-256
D36984E4D2128ECAC3DA045BFE67FB4EE6D7ECC06052E53B27535BBB5668354F).
Sources: https://www.unicode.org/Public/17.0.0/ucd/EastAsianWidth.txt and
https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt .

TerminalGlyph emits inert ASCII labels for control/malformed source, retains
printable graphemes, uses four-cell tab stops and a declared cell-width policy.
TerminalRow maps logical source lines, selection and caret to horizontal cell
viewports without slicing printable graphemes. Partial wide cells are blanked;
label clipping never changes the atomic source range. Tests cover CJK, combining
marks, joined emoji, tabs, escape/bidi controls, malformed bytes, selection/caret
mapping and horizontal clipping. Twelve suites passed in 2.96 s. House-style
spelling: zero findings in 51 authored files, plus semantic boundedness and
source/display review.

No claim of console-host compatibility or completed terminal UI: native console
render/input integration is next. Width policy is a declared Unicode-based
approximation requiring host validation, especially complex-script clusters and
emoji/text variation. Current row construction scans the whole logical line;
long-line latency and caching/yielding remain part of the unfinished lag work.

## Native console host - 2026-10-01

Added swiftedit-terminal.exe: private screen-buffer lifetime, UTF-16 keyboard
assembly, source-mapped row rendering, selection/navigation, file prompts, save,
private cut/copy/paste, undo/redo and explicit dirty-exit choice. Failures in row
layout retain the input loop and save commands. Prompt errors remain visible.
Key-release events do not redraw. Build-Windows stages the terminal executable
and traverses its DLL dependencies; no old stage was replaced in this turn.

Thirteen headless suites passed in 1.95 s; zero spelling findings across 52
sources. Reviewed handle ownership/partial acquisition cleanup, console mode
restoration, UTF-16 errors, source/output separation, prompt publication and
native test ownership against the full house style.

Initial smoke fixtures with key-down-only injection timed out and were killed
at the external 20-second limit. Tracing found the final exit event absent from
the consumed sequence. Adding matching key releases made the fixture reliable;
no pass is claimed for the timed-out runs. Final private-console smoke exited 0,
verifying supplemental Unicode input, saved bytes, dirty-exit cancel, undo and
input-mode restoration. It always detaches and allocates its own hidden console;
no global input injection or existing user console input is used.

Final smoke SHA-256:
`58DF7955E085799F6888B4E5BCFE246285C30550657D3F164115E36E9306E7D0`.
Terminal executable SHA-256:
`6BB6A0BABA98BAC90F91315A120A507B93D8109F48CFA64B24F7DDD9B3F48E9B`.
Log: `.build/swiftedit-resumed/console-20261001-022718.stdout.txt`.

This is not terminal feature completion. TERMINAL.md enumerates remaining
large-file pages, wrap/search/wildcard/clipboard/save-review work. No native
visual, keyboard-layout, accessibility or latency claim is made by this smoke.

## Bounded terminal large-file pages - 2026-10-01

TerminalPager replaces the host's large-file placeholder. It reads at most
64 KiB, defers the trailing unproven grapheme and partial scalar, caches by
source identity/revision/dimensions, and retains at most 1024 prior cursors.
Source-aware label continuation and full-row/newline state prevent skipped
bytes and spurious empty rows across pages. Context-exceeding or window-wider
printable graphemes report unavailable rather than being split. Source is kept.

Thirteen suites passed in 4.14 s on the final run; zero spelling findings across
54 authored files, plus semantic review of cursor publication, bounded storage,
UTF-8 edge handling, label/source separation and stale cache rejection. Tests
include a real 16 MiB file with a combining grapheme split by the 64 KiB read,
full-row CRLF continuation, label continuation, previous page, stale revision,
and an oversized unprovable cluster.

Owned private-console smoke exited 0, including large-file Next/Previous/First,
save refusal, unchanged file size/mtime and restored console mode. Binary hash:
`A4CA7688CD96DFE631A889D0A777F1EE6D937DECC2746137C6A850BA1B61DBF3`.
Log: `.build/swiftedit-resumed/console-20261001-023352.stdout.txt`.
No running test process remains. No visual geometry, physical keyboard,
accessibility or latency guarantee follows from these correctness tests.

## Terminal Find and flagged query - 2026-10-01

Ctrl+W query editing/F3 Find Next now use PatternScan with bounded 4096-work
steps and console input polling between steps. TerminalSearch validates source
identity/revision and original selection before publication. TerminalQuery edits
graphemes, preserves unchanged-slot flags, and toggles wildcard slots without
changing literal query text. Query has a 4096-byte bound. Headless coverage:
one-unit scan stepping, wrap, source/selection cancellation, explicit cancellation,
literal punctuation flags, prefix/suffix retention and deletion.

Thirteen suites passed in 2.02 s; spelling findings zero across 58 authored files.
Semantic review covered prepared query publication, borrowed source lifetime,
scan cancellation, immutable snapshots and stamped selection endpoints.

Native private-console smoke exited 0 with an added Ctrl+W/query/Ctrl+Shift+OEM2/
Enter sequence and explicit assertions for wildcard state and one published
match. Smoke binary SHA-256:
`810E0E88CAAA8D1CAFCE905E4EE7ED2C3CC528C246840F2A0C6DF6428D503DBA`.
Log: `.build/swiftedit-resumed/console-20261001-024032.stdout.txt`.
This verifies owned console virtual-key dispatch, not physical keyboard layouts,
visual quality, accessibility or latency. Large-file/malformed-byte search and
terminal replacement controls remain. Snapshot construction is synchronous and
must be included in the final measured responsiveness audit.

## Terminal Replace All - 2026-10-01

Ctrl+H opens the shared flagged query and replacement prompts. TerminalReplace
uses ReplacementScan for private bounded preparation, validates document stamp
and original selection before publication, and makes one Session edit. Partial
work/cancellation/source edits cannot publish. Deferred search/replacement errors
are caught at the input-loop boundary, retaining the session instead of exiting.

Thirteen headless suites passed in 2.63 s; zero spelling findings across 58 files.
Semantic review covered private result lifetime, pre-publication revocation,
count/no-match behavior and atomic undo boundaries. Added tests for incremental
private preparation, cancellation, replacement count, one-step undo and source
edit invalidation. Native owned-console smoke asserts three wildcard grapheme
replacements and undo, then prior dirty-exit/largefile/mode-restoration cases.
Exit 0; smoke SHA-256:
`6AC55FAF3F38CA910FFBD46D0931538F926A5323D1AE06498B5639F5FA8AD4B4`.
Log: `.build/swiftedit-resumed/console-20261001-024441.stdout.txt`.
No latency, physical-keyboard or visual assertion follows from this smoke.
Final whole-result publication is synchronous. Existing CRCR source can cause
Session payload rejection of a whole replacement result; source remains intact,
and this terminal/model parity limitation is not marked complete.

## Source-preserving Session replacement authority - 2026-10-01

Resolved the terminal CRCR limitation above. Session now issues an opaque
SessionReplacement with private immutable source identity/revision and engine
state. Only inserted replacement bytes pass through metadata/UTF-8 validation;
retained source chunks remain source. Callers may advance/discard work but cannot
forge the prepared text, source stamp or readiness. Session commit checks issuing
identity/revision and completion, consumes authority once, then moves prepared
text into one undoable change. This also removes the extra whole-result copy
previously made by range replacement; no measured speedup is claimed.

Full thirteen-suite regression passed in 2.84 s. Additional direct stale-plan
and no-op/reuse tests passed the affected terminal suite in 0.35 s. Tests cover
existing `cat CR CR cat` becoming `dog CR CR dog`, one undo restoring exact source,
inserted marker refusal, incomplete/foreign/stale-plan refusal, and one-use
consent even when identical replacement leaves revision unchanged. Spelling
scan: zero findings in 60 authored files. Semantic review covered private factory
authority, immutable snapshot ownership, commit failure and one-use consumption.
No new desktop run was needed for this core ownership change; previous native
smoke is historical evidence, not an assertion about the newly linked binaries.
Snapshot setup and undo baseline allocation still require latency measurement.

2026-10-01 source clipboard checkpoint: Session now issues owned SourceClipboard
snapshots from checked source ranges. Terminal Copy/Cut/Paste uses these snapshots
to preserve malformed bytes and existing CRCR verbatim, including after the source
document is replaced. Cut captures before deletion; paste checks destination
identity/revision, range and the editable size limit before one undoable edit.
External payload validation remains unchanged. Build and all thirteen headless
suites passed (3.08 s total); regressions cover byte roundtrips, cross-document
lifetime, stale capture/destination, range/capacity refusal and undo preservation.
House-style spelling audit: zero findings across sixty authored files; semantic
review checked ownership, source-only construction and publication ordering.
No new native console run for this slice, and suite runtime is not UI latency.
Full goal remains open: wrapping, large-file interaction, external clipboard/save
parity, public-provider GUI integration and final measured lag/bug validation.

2026-10-01 terminal row latency checkpoint: bounded sparse per-line cell/source
indexes now serve the actual console host for viewport draw and caret reveal.
Document identity/revision invalidates cached entries; 320 lines maximum, one
checkpoint per 256 graphemes. Full-traversal equivalence tests and all fourteen
headless suites passed; coordinated own-console smoke passed and slot released.
Measured 1 MiB ASCII warm-row p50 127.2604 -> 0.0279 ms; insert+row 171.5926 ->
47.2718 ms. Raw samples, percentile distributions, workload and build details:
`docs/performance/2026-10-01-terminal-rows/README.md`. These are component timings,
not end-to-end snappiness evidence. Edit metadata still rebuilds synchronously;
wrapping, provider integration and final full lag/bug scan remain unfinished.

2026-10-01 paged file portability checkpoint: PagedFile/Page moved to independent
paged_file.hpp. Windows retained-handle semantics are in paged_windows.cpp; the
actual Windows Session links that adapter. paged_posix.cpp uses bounded pread,
regular-file/NOFOLLOW checks and before/after identity/size/mtime/ctime checks.
POSIX cannot prevent uncooperative writes; changed pages are refused, not frozen
by a mandatory lock. Common tests cover exact raw bytes, EOF, offsets, budget and
32 MiB bounded tail reads; POSIX adds mutation, symlink and nonblocking FIFO tests.
Windows full 15-suite run passed (2.43s); standalone core/readers 2/2 passed.
CI now compiles both adapters on their actual runners. Mac/Linux result pending
for this commit; native application save adapter/SDK/package still unfinished.

Verified cross-platform follow-up: commit 27ffbfc passed run 36848596407 on
Windows-2022, macOS-26 and Ubuntu-24.04. Both CSV/formula and paged-reader tests
compiled and executed on each runner. POSIX-specific changed-file, symlink and
FIFO checks passed on macOS/Linux. Evidence:
https://github.com/falseywinchnet/swiftedit/actions/runs/36848596407
This proves the tested file-reader component, not a native app or downloadable
Mac bundle. Next: POSIX save/conflict semantics and installed macOS SDK packaging.

2026-10-01 POSIX save checkpoint: 3e655d4 passed real Windows/macOS/Linux CI run
36849322565. POSIX adapter creates sibling files privately, preserves existing
permissions/attributes, checks snapshots and metadata before publication, uses
atomic no-overwrite link publication for new files and atomic name exchange for
replacement. Cooperating writers are excluded by flock. A detected race during exchange verification
leaves displaced bytes at the reported sibling recovery path and reports
failure; it does not claim the visible target was unchanged after an uncertain
publication. POSIX uncooperative writers are not prevented by mandatory locks.
Deterministic test-only publication hooks prove new-target collision refusal and
displaced-content retention. Ordinary saves delete temporary files; no persistent
history is retained on success. Unsupported atomic exchange/metadata copy fails
without a destructive fallback. New files start private (0600).
Main CMake now selects platform adapters and creates a macOS app bundle; Windows
console target stays Windows-only. Fixture process IDs, permissions and cooperating
writer ownership are portable. Windows native build + 15 suites passed (2.10s),
69-file spelling audit clean. Full Mac GUI/SDK/bundle validation remains pending.
The modification-time follow-up is being rechecked on macOS/Linux CI.

Verified follow-up f12d33c: cross-platform run 36849715223 passed Windows, macOS
and Linux, including fresh modification time after metadata-preserving save.
https://github.com/falseywinchnet/swiftedit/actions/runs/36849715223
The POSIX publication hook is compiled only into the standalone adapter test
target; production builds have no injected callback. No native GUI Mac build or
bundle has been validated. Provider-installed SDK archives remain outstanding.

2026-10-01 native newline correction: new documents and files without existing
endings now use LF on macOS/Linux and CRLF on Windows. Opened files keep their
first observed ending, including mixed-ending files. GUI New/initial construction
and terminal reset/initial construction use the same native_newline policy.
Windows full build and 16 suites passed (3.39s); standalone 4-suite gate passed.
Cross-platform CI checks the actual compiled host convention and preservation.

2026-10-01 native three-platform and Mac release gate passed at 5d7d1c7.
Run 36853053157: Windows 17/17, macOS ARM64 16/16, Linux 16/16 including owned
native save/dialog/shutdown. Packaged Mac startup, loader paths, ad-hoc signature,
archive/executable hashes and published asset digest verified. Full evidence and
scope limits: DOGFOOD_2026-10-01.md. Suite times do not establish UI latency.
# Independent release download verification

After all nine release assets are downloaded into one directory, run:

```text
python tools/Verify-Release-Downloads.py --downloads <directory> --revision <full-source-SHA> --provider <full-SDK-provider-SHA>
```

The tool reads archives without extracting or executing them. It requires all
three platform archives and their sidecars/manifests, exact source and provider
identity, and the GUI, terminal and CLI executables. It verifies every
manifest-listed Windows/Linux file and the three executable hashes recorded by
the macOS manifest. Unlisted bundle files are not independently hash-certified
by this check. A successful all-platform check writes independent-verification.json;
failure does not replace an existing receipt. Always use the process exit status
and confirm the receipt revision, rather than treating an old receipt as success.

The tool was checked against downloaded v0.3.5 archives (44 Windows files,
98 Linux files and 3 macOS executables). A deliberately wrong provider revision
was rejected and left the successful receipt unchanged. It does not replace
native startup, visual, signing, or physical-device tests.
