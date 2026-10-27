# Private bounded shaper storage decision checkpoint

Status: coordinator-requested read-only proposal, 2026-10-03. Recommendation A
below is not approved implementation. No shared source or compiler invocation
was made for this checkpoint. Input/batch admission remains frozen in PR19.
Apply the complete File Manager `planning/PROGRAMMING_HOUSE_STYLE.md` to the
future scope: explicit initialized types, named kernels, typed owners and
extents, checked arithmetic, executor-confined mutation, and staged publication.

## Recommendation and alternatives

Recommend **A: reusable staging workspace followed by exact active-output
allocation/copy**, subject to source review and measured regression gates.
Keep the current A2 entry point and behavior intact. First add a private
workspace entry point and prove geometry equivalence; only then connect it to
the window batch. This is bounded storage work, not native widget integration.

| Property | A: reusable staging | B: bounded-capacity retry |
| --- | --- | --- |
| Glyph sizing | Actual native count copied into fixed staging capacity; final allocation uses active count | Start with small declared capacities; rerun complete paragraph after capacity refusal |
| Expansion assumption | None; retains existing explicit run/glyph ceilings | None only if retry is based on typed capacity exhaustion, not estimated glyph expansion |
| Native shaping | Once per run for a successful paragraph | Repeats earlier runs and often the same native run on each retry |
| Controlled scratch | Allocated before paragraph loop and reused | Current API reallocates segmentation and scratch on every attempt |
| Final payload | Only active runs/glyphs retained | Capacity, not active count, remains charged unless another compacting copy is added |
| Failure | No partial batch publication; old completed owner survives | Same publication rule; failed attempts must retire before retry |
| Main cost/risk | New private workspace/fill boundary and one staging-to-output copy | Repeated work, allocation churn, ambiguous existing length_error classification, and poor many-row utilization |

B is a comparison candidate, not the recommended production path. A retry must
not catch every length_error: the current exception also represents input,
workspace, output-byte and arithmetic limits. A typed run/glyph-capacity result
or a distinct internal exception would itself require shaper changes. Retries
must increase checked capacities, remain within the existing maximum counts,
stop at remaining payload/workspace limits, and check cancellation between
attempts. Never retry missing-font/native/invalid-input failure. Log native
shape-call counts and all failed attempts rather than timing successful work
alone. Doubling is an allocation policy, not a glyph-to-scalar correctness law.

## A: owners, extents and accounting

Use one caller-owned, noncopyable private `BoundedShapeWorkspace` per serial
shaping executor. It owns contiguous arrays and active counts; no shared mutable
workspace, process-global cache, borrowed native buffer or UI object. A named
preparation operation allocates replacement storage before adoption, preserving
an existing workspace if preparation fails. Capacity is fixed during a batch.

Workspace includes reusable grapheme scalar/boundary storage; decoded scalar,
font segment, direction and visual-intersection arrays; and staging arrays for
the existing declared maximum **16,384 runs / 65,536 glyphs**. These are existing
profile ceilings, not a promised expansion ratio. A result beyond either
ceiling is the same kind of declared resource refusal as current bounded A2.
Scalar/boundary/visual capacity uses the largest admitted paragraph's validated
requirements (or conservative UTF-8-byte bounds with checked scalar+1 and
2*scalar arithmetic), not 512 copies of per-paragraph capacity.

All simultaneously live controlled workspace bytes count toward **16 MiB**:
workspace owner/arrays, persistent bounded engine registration records, fixed
call records and any temporary allocation during workspace replacement. Do not
report max(previous,new) when both owners coexist during replacement. Existing
registered encoded font bytes retain their independent font-bank charge.
Allocator overhead and opaque HarfBuzz/FreeType/SheenBidi allocations remain
outside the controlled quota and must be reported as exclusions.

Final retained payload counts toward **one 8 MiB aggregate**: admitted batch and
input storage, row metrics/descriptors, any shape-result owner and its arrays,
all finalized run/glyph arrays, and any other controlled retained metadata.
Before a final row allocation, check:

`row_bytes = sizeof(BoundedShapedText) + active_runs*sizeof(BoundedFontRun) + active_glyphs*sizeof(ShapedGlyph)`

Check each addition/multiplication against remaining bytes by subtraction and
division first. Counts come from the staged successful native result. There is
no guessed expansion multiplier. Allocate only those active counts, copy them,
then increment aggregate live bytes. Staging and final output coexist, charged
to their distinct workspace/payload quotas. A late allocation or row failure
publishes no partial batch; caller-visible old completed output is unchanged.

An empty row retains a logical row and the established empty-line metrics, with
zero run/glyph arrays. Preserve existing empty-shape metric semantics initially
(currently height equals device font size, other fields zero), rather than
inventing baseline/ascent values. Any later empty-line metric correction needs
separate evidence. Separators remain source/display metadata, not shaped rows.

## Borrow and native lifetime contract

Each operation borrows one complete paragraph as string_view into the immutable
admitted input. Its byte range is row.display_begin/end minus the input display
base, checked before constructing the view. No text copy, separator inclusion,
or concatenation of separate bidi paragraphs. Cluster/run offsets remain
paragraph-relative until an explicit checked translation to window coordinates.

Native `append_run` already exposes the exact glyph-info and position count
after hb_shape_full and before writing output. Copy those values immediately
into admitted staging extents. Native info/position borrows expire on buffer
reset/reuse/destruction and never escape the call. A readonly staging result
view expires on the next workspace mutation; copy active output before then.
No mutable alias enters a retained batch or recorded command.

Reuse one engine and registered immutable font bank across the batch. The bank
must outlive the engine and all native face borrows. First retain current
per-paragraph ShapeCall font/buffer lifetime: native HB owners are destroyed at
call end. This preserves existing font-size/reset behavior and does not claim
zero native allocations. Native-owner caching across paragraphs is not needed
for A and should not be bundled into this source change.

Check full desired authority and cancellation before and after every paragraph
call and before final publication. No authority mutex is held during native
shaping. A native call is noninterruptible; cancellation observed afterward
rejects its output while its owners survive through return. Revoke/close must
not free workspace, engine or input while that call executes. Final publication
rechecks authority under its protocol; completed but stale geometry is never
renderable. Worker/wake integration is a later assignment, not a consequence of
the storage helper returning successfully.

The current batch-current helper also checks the admitting executor. A future
worker must not call it and interpret wrong-executor as cancellation. That
integration needs a separately named read-only authority check, under the same
mutex/key/closing rules but without granting desire/admission rights to the
worker. Keep owner-thread restrictions on mutation. Include that narrow batch
helper change explicitly in the later worker assignment.

## Source boundaries requiring approval

The following is the proposed exact review scope, not current edit permission:

- Change private `src/render/text/harfbuzz/harfbuzz_font_engine.hpp/.cpp` for a
  separately named workspace path and shared named traversal/output kernels.
  Keep existing `shape` and `shape_bounded` signatures and failure behavior.
- New `src/render/text/harfbuzz/bounded_shape_workspace.hpp/.cpp` for typed
  owner/capacity/lifetime implementation, if separation avoids crowding the
  engine. No public header or native-library type export.
- Narrow change to `src/core/text/unicode/unicode_grapheme.hpp/.cpp`: existing
  `GraphemeBoundaryBuffer` always allocates replacement scalar/boundary arrays.
  Add a private reusable workspace/fill path sharing the existing decode and
  should_break kernel. Preserve current output-on-failure guarantees and
  Unicode rules; do not duplicate segmentation in the renderer.
- New `tests/bounded_shape_workspace_tests.cpp` and
  `tests/prepared_window_shape_bench.cpp`; extend existing bounded-grapheme
  tests only for the new workspace/fill contract. Root owns CMake registration.
- Only after this stage passes: new `src/render/text/prepared_window_shape.hpp/.cpp`
  and tests to adopt exact row geometry under the admitted aggregate. Any
  change to current batch storage must be explicitly included in that assignment.

No A2 service/session, raster/display command, host, public SDK or consumer GUI
changes in the workspace stage. Sharing implementation kernels is acceptable
only if existing semantics, allocation accounting, diagnostics and failure
controls remain verifiable. Preserve failed experiments and raw output.

## Concrete fixtures, controls and commands

New benchmark target/arguments below are **proposed**, not existing commands.
Run from File Manager root after coordinator-owned registration. Use the same
Release toolchain and pinned font bytes as current bounded-shape tests:
Carlito-Regular, NotoSansArabic-Regular, NotoSansHebrew-Regular, NotoEmoji-Regular.
Record source SHA, font hashes, compiler, OS/CPU and allocation counters.

`preview-seven` contains exactly seven independent short lines: Latin ligature
text (`office AV 123`), Latin combining marks, Arabic with digits, Hebrew with
Latin digits, an empty row, an emoji/text row, and a literal `[U+0000]` label.
Use the actual selected-file preview's width/font/scale captured by the harness;
first prove each line fits without wrapping. This is the admitted short-line
File Manager preview subcase, not full wrapped-preview support.

`window-512` contains exactly 512 rows by cycling the same short mixed-script
lines with short decimal row identifiers (empty rows remain empty). Use 511 LF
separators and a final unterminated row; assert aggregate display <=16,384 bytes
and source <=65,536 bytes rather than silently truncating. Include a separate
512-empty-row/EOF correctness fixture and explicit projected control-label
source mappings. No per-row bidi concatenation. No missing-font substitution.

```powershell
cmake --build .build/prepared-window-input --target gui_forms_bounded_shape_tests gui_forms_bounded_grapheme_tests gui_forms_bounded_shape_workspace_tests gui_forms_prepared_window_shape_bench --parallel 1
ctest --test-dir .build/prepared-window-input -R 'gui_forms_(bounded_shape|bounded_grapheme|prepared_text_service|prepared_text_raster|prepared_display|prepared_window)' --output-on-failure --parallel 1
./.build/prepared-window-input/gui_forms_prepared_window_shape_bench.exe --fonts gui_forms/assets/fonts --scene preview-seven --variants legacy,a,retry --warmup 10 --samples 101 --order alternating --csv .build/preview-seven.csv
./.build/prepared-window-input/gui_forms_prepared_window_shape_bench.exe --fonts gui_forms/assets/fonts --scene window-512 --variants legacy,a,retry --warmup 10 --samples 101 --order alternating --csv .build/window-512.csv
```

Legacy control shapes each paragraph with the existing bounded API and releases
its temporary output before the next row. Report its repeated oversized
allocations honestly; it does not claim all rows fit simultaneously in an 8 MiB
aggregate. A and retry must retain all completed rows under that aggregate.
Compare exact glyph IDs, positions, clusters, run ranges, coverage and metrics
against the legacy result before measuring. Time font registration separately,
then shaping/allocation/copy, retirement, and complete-window readiness. Record
per-paragraph and whole-window p50/p95/p99/max, shape-call count, allocation count,
controlled workspace peak and aggregate live/capacity bytes; preserve failures.
Use one compiler/measurement interval with other local benchmark activity paused.

Run exact-capacity and one-less storage tests using observed active counts, not
guesses. Include mixed font/scale replacement, late-row failure, every new
allocation failure, reused workspace after failure/cancellation, and repeated
requests whose old output stays retained. An unchanged retained-output read
must perform zero shaping; zero-shape native repaint is a later renderer test.
Do not label these component measurements first-paint or physical input latency.
