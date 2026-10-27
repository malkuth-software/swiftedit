# Encoding-preserving Session foundation

The GUI currently owns the older Document model, which supports UTF-8, UTF-8
with BOM, and BOM-marked UTF-16 LE/BE. The byte-faithful Session used by terminal
and CLI previously had only a UTF-8 publication path. Moving the GUI to Session
without an explicit output codec would lose existing encoding behavior.

`Session::open_decoded(path)` now provides a separate, explicit open path for
future mapped GUI integration. It uses the existing BOM detection and UTF
validation, preserves the detected output encoding, and stores logical text as
UTF-8. It does not guess a legacy code page. Source ranges, clipboard bytes,
revisioned edits, projections, undo/save baselines and as-opened restoration all
refer to that logical UTF-8 text. They are not offsets into a UTF-16 file.

The original encoded file snapshot remains separate and supplies save conflict
checks. Save and Save As encode the current logical text using the retained
codec before invoking the existing safe publisher. Failed conversion or
publication does not change the path, codec, saved baseline, revision, or undo
history. Successful publication retains the existing save-B/restore-A policy.

The default `Session::open` is unchanged: it retains every input byte, including
BOMs and malformed sequences, and uses UTF-8 publication after illegal bytes
are removed. Reset or a later raw open clears the decoded output codec. Existing
terminal and CLI callers still use that path. No transport command or product
menu has been added for the new internal API.

The shared decoder/encoder now accepts an explicit `TextControls` policy.
Existing GUI callers default to their legacy control rejection. The new Session
path requests preservation of every valid scalar, including NUL and C0 controls,
so it can support the required visible, inert mapped display. No control is
replaced, hidden in the source model, or converted to a display label by the
codec itself. Do not connect this content directly to the old GUI text widget;
the mapped display must be integrated at the same time.

Save Text Copy remains an explicit UTF-8 copy, preserving valid source scalars
and replacing illegal bytes as before. It does not change the source document's
codec, path or saved baseline.

The initial implementation accepted only encoded files and decoded text below
the 16 MiB editable threshold. The incremental paged path described below now
handles the read-only threshold. Cancellable GUI opening, native mapped editing,
GUI encoding selection in the reviewed-save flow and actual GUI Session adoption
remain unfinished.

## Incremental decoded read-only source

`DecodedPagedFile` owns an ordinary PagedFile handle and prepares a sparse index
in steps of at most 65,536 encoded bytes. Checkpoints retain encoded and logical
UTF-8 offsets at complete scalar boundaries, including UTF-16 surrogate pairs.
The index is proportional to encoded file size (one pair of uint64 offsets per
block, plus vector capacity); the complete decoded text is never retained.
Allocation failure fails preparation and releases its handle and index.

UTF-8, UTF-8 BOM and both BOM-marked UTF-16 byte orders use the existing strict
decoder with control preservation. An interior BOM remains content. Incomplete
scalars at a block edge are reread on the next step; malformed EOF and complete
invalid sequences fail instead of publishing the valid prefix. UTF-32 and
malformed encoded input remain refusals in this explicit decoded API. Raw open
still preserves arbitrary bytes unchanged.

Ready sources serve arbitrary logical UTF-8 byte pages through the sparse index.
As with raw page transport, a page may split a multibyte scalar; concatenation
preserves its exact bytes. Each page decodes bounded blocks around its cursor,
with no whole-file scan. There is no decoded-block cache yet. Native I/O and
per-block validation/allocation are not hard time-bounded.

Callers can cancel/destroy a pending source before adoption. Session adoption
requires a ready source, an encoded or decoded size at least 16 MiB, and the
original observed Session identity/revision. Rejection preserves both the pending
owner and current Session. Successful adoption moves the file/index owner,
retains its codec, establishes a new document identity, and enforces read-only
editing/save behavior. Copy consumes logical UTF-8 pages. Reset releases the
handle and returns to the default raw UTF-8 codec.

`Session::open_decoded(path)` remains a synchronous convenience API: large input
or UTF-16 expansion prepares this index and adopts read-only content. Future GUI
opening must call incremental preparation and stamp-checked adoption instead of
running that convenience loop on the UI thread. Small editable decoded sessions
retain their existing save/undo/snapshot behavior. No GUI model has switched in
this change, and no raw terminal/CLI open has changed its interpretation.

Tests cover all four encodings, interior BOM/controls, scalars and surrogate
pairs crossing 64 KiB boundaries, random decoded pages, one-byte transport,
empty files, malformed UTF-16/UTF-8 endings, cancellation after a step, stale
adoption, source and decoded-size threshold transitions, read-only edit refusal,
and copied Unicode spanning one-byte page reads. Cross-platform validation is
pending for this addition.

Local validation: all 30 Windows suites passed in 32.78 seconds. After adding
explicit pending-adoption and encoded-size/decoded-smaller assertions, the
decoded-paging and Session suites passed again in 1.76 seconds. Source review
covered the new decoder header/implementation/tests, Session integration and
modified tests, plus CMake registration, against the complete house style.
Reviewed concerns include scalar-boundary arithmetic, codec forcing, staged
publication, owner transfer, cancellation/failure retirement, executor confinement,
bounded read/decode buffers and sparse-index growth. This does not certify
untouched decoder/file-adapter internals or establish a native latency bound.

Regression coverage in `tests/session_tests.cpp` includes all four encodings,
exact BOM/byte-order output, mixed endings, non-BMP Unicode, controls/NUL,
clipboard and explicit UTF-8 copy behavior, edit undo/redo, save boundaries,
restore-opened, Save As collisions, encoded-snapshot conflicts, malformed UTF-16
open refusal, raw reopening, source/decoded-size boundaries and encoded-output
overflow. Existing Document tests retain the GUI's legacy codec policy, and
CLI/terminal suites exercise unchanged byte-faithful operation.

Local validation: all 29 suites passed in 22.96 seconds. After adding the
encoded-output overflow assertion, the Session suite passed again in 1.28
seconds. The C++ spelling audit passed all 148 files. Native cross-platform
validation subsequently passed at 491dc93 (run 37103664522).

## Reviewed Session publication

SaveReview now has a Session overload. It reads the current Session source and
stamp itself, consumes readiness before attempting publication, and uses the
same two-stage choice/review and observed-destination race checks as the existing
GUI Document path. Only this review class can call the private Session operation
that accepts a freshly observed overwrite snapshot; normal Save/Save As remain
unchanged.

On success the Session adopts normalized text, requested output encoding,
destination and save baseline together, clears undo to the new save boundary,
and retains its independent as-opened text. Failure leaves Session state and
history unchanged and requires a new review before retrying. Illegal bytes and
read-only sessions cannot bypass their publication restrictions through review.
Prepared logical text and encoded output must both remain below the 16 MiB
editable threshold. An exact-threshold UTF-16 output regression failed before
the encoded-size guard and passed after it.

The initial encoding foundation at 491dc93 passed cross-platform core and native
build/test/package validation (native run 37103664522). The reviewed-publication
extension passed all 29 local suites in 23.28 seconds before the final boundary
repair and extra read-only/expansion assertions; the five relevant suites then
passed in 5.08 seconds. The 148-file spelling audit is clean. Cross-platform
core run 37104088781 and native build/test/package run 37104088785 subsequently
passed at 6872916 on Windows, Linux and macOS. No existing GUI has switched its
model, and these changes do not complete mapped editing or paged decoding.

## Decoded projection integration

The document-projection suite now exercises all four codecs through decoded
Session opening, a nonzero-offset DocumentProjection page, and the installed
public DocumentViewState. It verifies absolute logical UTF-8 mapping for an
inert NUL label after a multibyte character, rejects label-interior positions,
and checks the inverse mapping. Copy returns the real NUL; a mapped replacement
changes that source character only. Rebinding the view to the changed Session
revision revokes the old page positions. Undo followed by save reproduces the
original encoded bytes, including BOM and byte order.

Local projection integration passed in 0.24 seconds, with zero spelling audit
findings across 148 files. This checks existing model contracts, not native
selection gestures or a new edit controller. D1 mapping establishes scalar and
token boundaries, not complete grapheme legality.

SelectionSet now accepts typed display selections with a public view and page
token. It checks the token against the current Session before mapping, uses
the view's exact token/endpoint validation, and then applies its existing source
grapheme and ordered/disjoint-range checks. It owns only source ranges and the
Session stamp after construction; it retains no view or page borrow. Copy and
rewrite therefore retain the existing stale-document, equal-grapheme-count,
undo and source-preservation rules. This does not itself rebind or invalidate a
native view: the future UI controller still owns that lifecycle.

Integration regressions reject combining-grapheme splits, generated-label
interiors, and stale Session revisions even before view rebinding. Separate
Unicode selections with equal grapheme counts remain editable; unequal counts
are copy-only and rejected rewrites leave source unchanged. All 29 local suites
passed in 23.82 seconds. The subsequent selection optimization limits synchronous
segmentation to complete LF-delimited context groups enclosing the selections.
Unselected lines between separate groups are skipped. Very long lines or large
selected ranges can still span most of the document;
this is not a hard latency bound or completed native gestures. See
`performance/2026-10-03-selection-context/README.md` for measured before/after
component evidence and boundary regression coverage.
