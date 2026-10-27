# Prepared-window shaping boundary review

Read-only follow-up while the coordinator verifies aggregate admission. This
is a proposal correction, not an approved API, shared source edit, or rendering
result. SwiftEdit continues to consume frozen aaca5d0.

## Observed source constraints

Reviewed File Manager's private `gui_forms/src/render/text/harfbuzz/`
`harfbuzz_font_engine.hpp/.cpp`, `prepared_service.cpp`, and renderer-neutral
`core/text/shaping/shaped_text_geometry.hpp` on 2026-10-03.

Reviewed shaper fingerprints (SHA-256): implementation
`ecb78ab3ba1d9226acd47cb294f5770e491ffaa93e8c65abd29ab91aaf01571d`;
header `ffbf444eb4089c3122015490c7bf2bfd5d2026c8c3ac0086e96ee4625d311308`.

`ShapeStorageLimits` defaults to 16,384 runs and 65,536 glyphs. `shape_bounded`
charges and allocates arrays at those capacities before shaping, even for an
empty paragraph. It reports controlled output bytes for capacity, not active
counts. Passing only a smaller remaining output-byte limit does not shrink
those arrays: accounting rejects the request before allocation if they do not
fit. A sequential row loop using the existing defaults therefore cannot be
treated as a useful implementation of a 512-row aggregate.

The engine can retain registered encoded font owners and a bounded face table.
However, each shape call separately creates grapheme boundary, scalar, font
segment, direction-run and visual-intersection storage. Sequential calls cap
simultaneous scratch use but do not reuse that allocated storage. The earlier
batch proposal's reuse wording must not be reported as implemented by merely
retaining one HarfBuzzFontEngine.

An empty call allocates output first and then sets height from font size. A
batch should retain an explicit empty logical row and established line metrics
without pretending a consumed separator needs a shaped glyph buffer.

## Consequences for the next assignment

Keep aggregate admission/retirement acceptance separate. Before adding a row
loop, the coordinator needs to select a bounded shaping-storage contract that
supports useful small paragraphs under one aggregate cap. The current output
limits express caller-chosen capacities, not exact glyph requirements. Do not
silently assume a fixed glyph-to-Unicode-scalar expansion ratio, lower the input
profile to ASCII, or concatenate separate bidi paragraphs to avoid this issue.

One candidate is a private caller-owned shaping workspace with reusable bounded
arrays, paired with output allocation based on actual native run/glyph counts
under a remaining-byte ceiling. It would require a narrow coordinated change
to the shaper, not only the originally proposed new batch files. That change
must preserve the existing A2 operation and its regression evidence, bound
temporary native result lifetime, and check allocation arithmetic before each
retained output allocation. A count-then-fill design must document whether it
shapes twice or retains native output between passes; neither cost is free.

Alternatively, a measured bounded-capacity retry design could avoid an initial
shaper refactor, but must report repeated shaping/allocation, stop at the
remaining aggregate ceiling, and never publish partial rows. This is an option
for evaluation, not a selected implementation or a claim of workspace reuse.

Required evidence for either design: mixed-script complete paragraphs; empty
rows; a 512-short-row window; run/glyph capacity boundaries; late-row failure
preserving publication state; source/display mapping and bidi paragraph
independence; actual allocation counts and peak bytes; cancellation between
native calls; and unchanged A2 correctness. Native calls remain individually
noninterruptible. Neither a source review nor storage-only tests establish
rendering, GUI editing, IME, or input latency.
