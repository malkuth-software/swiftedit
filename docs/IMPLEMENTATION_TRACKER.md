# Full implementation continuation

Owner direction (2026-10-01): keep working through the feature set, then perform
a measured lag/bug scan. This file tracks work; unchecked items are not complete.

- [x] Separate Word Count GUI/CLI tool; committed bc8a331.
- [x] Final SDK clean build and headless integration.
- [x] Revision-checked atomic source-range edit core and bounded display mapping.
- [x] Flagged one-grapheme wildcard core with bounded search slices.
- [x] Native query-field wildcard flags, right-click toggling, literal punctuation,
  grapheme-aware Find Next/wrap and replacement integration (headless verified).
- [x] Stored CSV formulas, dependency evaluation, exact errors and circularity checks.
- [x] Native CSV view, cell entry, rectangular clear, draggable scrollbars.
- [x] Menu/context Convert to Value, undo, formula hover/reference highlighting.
- [ ] Source/display provider contract and coherent SDK integration.
- [ ] GUI shared session migration, invalid/control-byte editing and paged viewing.
- [ ] Discontiguous selection and flagged wildcard GUI/terminal controls.
- [x] Mixed-ending save choice and full external-conflict/copy workflow.
  - Owned mixed-ending and two-stage conflict dialogs, editable save metadata,
    versioned new copy, explicit reviewed overwrite; races refuse intact.
- [ ] Conventional terminal screen.
- [x] Separate revision-bound blank-line metadata in bounded AI context reads.
- [ ] Native Markdown rendering, inert links, source toggle and rendered ruler.
- [ ] Native print, Markdown layout preview, multiple independent windows.
- [ ] Character/control inspector and insertion tools.
  - Inspector and separate Unicode/control picker dialogs are implemented with
    Unicode 17 catalog, page/code-point navigation, explanatory inert control
    previews, explicit insertion/copy and undo. Headless and native owned-window
    smoke pass. Visible-control document rendering and broader visual/accessibility
    checks remain tied to the document-view integration.
- [ ] Final native interaction/visual checks and measured lag scan/fixes.

CSV clarification: Enter stores a formula and displays its result. Convert to
Value appears both in the menu and cell context menu. Error conversion is refused
without mutation; successful conversion is one undoable change.

Current tests: all eight headless suites pass after wildcard UI integration, 1.16 s.
The display suite covers mapping/atomic edits/wildcard slices. CSV and CLI tests
cover dependency changes/conversion/undo/circularity. Editor tests exercise the
native grid's source toggle, cell entry, menu conversion, right-click popup and
shared command invocation, clipboard cell source, undo and error preservation.
Native desktop interaction and final performance claims remain unverified.

Wildcard follow-through remains: Windows host slash/question normalization is
missing from the consumed SDK; Ctrl+Shift+slash is tested with normalized events
only, and provider has the native fix request. Native focus/IME/accessibility and
query-flag undo semantics require review. Find Next now yields through the frame
scheduler with a byte-comparison work budget and cancels stale work. Replace All
now incrementally scans and copies into a private bounded result, then publishes
one undoable edit. Source/query/selection/replacement changes cancel pending work.
Snapshot preparation and final validation/publication still need latency
measurement. Do not count this as complete
terminal wildcard support or a completed search latency scan.

Provider work is coordinated with the GUI.Forms owner and Orchestrator registry
owner through the original parent. SwiftEdit owns source bytes and save policy;
provider owns virtual text/shaping/selection/viewport and native print seams.
Frozen SDKs and Plan Paint remain untouched.


2026-10-01 conflict workflow checkpoint: owned two-stage Save Over/New Copy,
editable filename/encoding/line endings, Review Destination warning and explicit
Save implemented. New Copy suggests dot versions and refuses existing targets.
Final publication binds to reviewed file identity/bytes and original document
identity/revision; field changes require another review, races refuse intact.
Ten headless suites and the nine-window native lifecycle smoke pass. This does
not complete byte-faithful GUI migration, print, dynamic windows or final lag QA.

TerminalBuffer editing model now shares Session and provides grapheme selection,
CRLF-atomic movement, preferred vertical columns, line cut, undo/redo and saves.
The terminal screen/input loop is covered by the later owned-console checkpoint.
Synchronous metadata rebuild still needs measurement. The source clipboard
checkpoint below resolves malformed internal cut/paste.

Interactive Windows terminal host now builds and passes its owned-console input/
save/undo/mode-restoration smoke. See TERMINAL.md. The terminal checkbox remains
open for large-file pages, wrap, search/wildcard and further interactive work.

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
