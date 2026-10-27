# Reusable shaping workspace implementation checkpoint

2026-10-03. Private candidate in the coordinated File Manager checkout. This
does not activate a public SDK, window shaper, controller, renderer or SwiftEdit
GUI feature. The owner goal remains incomplete.

## Authored and reviewed scope

Under `gui_forms/`:

- `src/core/text/unicode/unicode_grapheme.hpp/.cpp`: reusable scalar/boundary
  ownership, checked preparation, allocation-free validated fill, and shared
  bounded segmentation kernel. Existing Unicode break rules remain unchanged.
- `src/render/text/harfbuzz/bounded_shape_workspace.hpp/.cpp`: scratch owner,
  old/new replacement accounting, staging arrays, exact active-output copy.
- `src/render/text/harfbuzz/harfbuzz_font_engine.hpp/.cpp`: private entry points,
  bounded-registration requirement, shared font selection and bounded traversal.
- `tests/bounded_grapheme_tests.cpp` and
  `tests/bounded_shape_workspace_tests.cpp`: equivalence, allocation failure,
  ownership, accounting, invalid input, retained output and native failure tests.

The coordinator owns CMake registration. It adds the workspace source to the
existing text engine and a separate test executable linked to that engine, with
the same pinned font-directory argument as the old bounded shape tests.

Review applied `planning/PROGRAMMING_HOUSE_STYLE.md` to the authored changes:
explicit initialized types; named methods/kernels; checked count arithmetic;
executor-confined scratch; disjoint input/array borrows; preparation before
traversal; no growable per-glyph container on the bounded path; staged publication;
and automatic retirement after failure. No known new style violation was found.
Untouched legacy helpers and the complete old engine are not blanket-certified.
The shared traversal was extracted from the existing bounded path and retains
its coverage, bidi ordering, native-call and metric operations. Face-table indices
replace transient stored face pointers; registration stays unchanged during calls.

## Ownership and limits

The workspace is noncopyable/nonmovable and owns every mutable scratch array.
Preparation accounts for the old and candidate workspace records and arrays
before allocation; adoption follows all successful allocations. Grapheme
preparation separately exposes array payload accounting. The enclosing workspace
charges its owner records. Engine registration and fixed call records are charged
by the engine entry point. These are controlled requested-storage limits, not a
measurement of total process memory, stack frames or allocator overhead.

Encoded font owners retain their separate lifetime/accounting. Opaque FreeType,
HarfBuzz and SheenBidi allocations are excluded. Native fonts and buffers remain
call-local. Native glyph-info/position borrows are consumed before reset or
destruction and never retained in a result. Each output owns only actual run and
glyph counts. A failed call leaves caller-owned old output intact; internal
partial staging is not exposed and is reset before reuse.

The caller supplies remaining output allowance after charging its other retained
objects. The 512-row test charges its owner array and result allocations; it does
not establish certified input/batch accounting, cancellation, or renderability.
Those remain later integration obligations. No native-call interruption is claimed.

## Observed Windows validation

MinGW Release, existing `.build/prepared-window-input`, one compiler job.
Initial diagnostics-OFF six-suite checkpoint passed. After enabling
`GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS=ON`, rebuilding affected targets and completing
the source review, the final six-suite run passed 6/6 in 1.25 seconds:
bounded grapheme, bounded shape, bounded shape workspace, prepared text service,
prepared text raster and prepared display. CTest duration is not a performance
benchmark. No performance run was made.

New tests cover nine inputs at three font settings; actual geometry/metrics;
eight preparation and three result allocation failures; exact old/new memory
boundaries; zero allocation on sufficient preparation and grapheme fill; malformed
UTF-8 and unprepared requests; missing primary; 512 retained mixed-script results;
six native failure hooks; a later-run failure; and correct reuse after refusals.

## Subsequent integration evidence

The coordinator reviewed and merged the workspace as File Manager PR22,
`0f77d9ff5cdd08ddd48b730c06e021fe522156eb`. Both native matrices passed
Windows x64, macOS arm64 and Linux x64 (runs 37108791306 and 37108795570).
The PR status and merge identity were rechecked on 2026-10-03. This supersedes
the earlier pending native validation, but does not activate SwiftEdit's SDK.

The separate controlled measurement record merged through PR24,
`b021c7f5e3925b162c3794c5d140c99ba811bb8d`. The coordinator's Windows
diagnostics-enabled measurements observed a 512-row full-cycle median of
250092.4 microseconds for the old path and 54920.1 microseconds for reusable
workspace shaping. That includes the benchmark's output-retirement policy;
it is not UI latency or cross-platform performance evidence. A roughly
55-millisecond operation still belongs off the UI thread.

## Private complete-window implementation, source checkpoint

On 2026-10-03 the coordinator assigned the next private implementation in the
shared provider checkout. The authored files were frozen for coordinator
compilation after source review. At this checkpoint the new tests are UNRUN.

The worker-confined shaper registers the immutable font bank once, reuses its
workspace, and prepares every paragraph within the admitted batch reservation.
It charges the geometry table, retained arrays and temporary output owner before
each result. Publication transfers deep-const run/glyph arrays only after a
final full-identity and ledger check. Failure discards partial rows while
preserving input, committed fields and the generation reservation. Closing is
checked again before initialization publishes the native engine.

Authored tests cover 512 mixed-script rows with blank lines and EOF; comparison
against independent per-paragraph geometry and metrics; failure at each output
allocation; later-row revocation and missing coverage; initialization closing;
worker observation without mutation rights; final-owner reservation retirement;
and distinct atomic-control versus literal-lookalike source mappings. Empty
paragraphs may consume zero remaining runs/glyphs on a prepared workspace;
nonempty text still refuses exhausted counts. The original bounded API and
workspace-preparation policy are unchanged.

Exact retained byte sums and the empty temporary-owner byte boundary have
authored checks. No fixture claims to reach the fixed aggregate 8 MiB, run or
glyph ceiling, and no certified input or byte count was forged to reach one.
These limits are controlled requested storage, excluding opaque native
allocations and allocator overhead. Empty-row baseline/caret policy, session
scheduling, host wake, rendering, public API adoption and full GUI migration
remain unfinished. Cancellation is between native calls, without a hard deadline.
