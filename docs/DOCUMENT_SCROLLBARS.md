# Document scrollbars

SwiftEdit opts its multiline source editor into GUI.Forms automatic scrolling.
Vertical and horizontal bars appear when text exceeds the viewport; Word Wrap
removes the horizontal bar. The provider owns text extents, bar geometry, thumb
dragging, wheel input, clipping, caret reveal and offset clamping.

This consumes GUI.Forms PR #14, merged at
`4e126d6eb3ab837f1a44c0e9c9eb7901d50f80bb`. Its control ABI changed, so GUI.Forms,
the document picker and SwiftEdit must be rebuilt together. SwiftEdit's document
model links `GUIForms::Application`, sharing the same Core as its GUI rather
than mixing static and shared Core implementations.

The consumer regression first failed against the old provider with
`Tall document exposes its vertical scrollbar`. With the update, it covers:

- Fitting text with neither bar and overflowing text with both bars.
- API thumb positioning and mouse-wheel movement using the same text viewport.
- Preservation of source text, selection and undo availability while scrolling.
- Word Wrap removing horizontal scrolling and disabling wrap restoring it.
- Viewport expansion/shrinkage and replacement with short text clamping ranges.

The native consumer smoke also observes both bars after a UI frame, scrolls the
document, verifies the offset survives painting, checks selection preservation,
toggles wrap and clears the source. macOS captures the scrolled document view.
Provider tests separately exercise real thumb dragging and clipping.

Local Windows LLVM 22 validation passed all 31 headless tests in 26.56 seconds.
The native smoke executable rebuilt, the 152-file house-style spelling audit
reported zero findings, and headless portable-package checks passed. No local
desktop application was launched.

Commit `d217ffd3b5c2e88a78cdb47e22b6070f375de154` passed native run
[37757049631](https://github.com/falseywinchnet/swiftedit/actions/runs/37757049631):
39/39 Windows, 39/39 Linux and 40/40 macOS tests. All three installer checks
passed, and the repository automatically published
[v0.3.204](https://github.com/falseywinchnet/swiftedit/releases/tag/v0.3.204).
Downloaded archives, installer hashes, source/provider identities and manifests
were independently checked; see [verification receipt](releases/v0.3.204-verification.json).
The macOS native screenshot was inspected and shows both scrollbars. This is
automated native evidence, not physical MacBook dogfooding.

This fixes navigation in the current text widget. The existing GUI text-size
limits and remaining paged-document migration are unchanged.
