# Rendered Markdown visibility audit

The former viewport lookup started at `top - maximum_run_height`. A quote rule
or table border spanning a long paragraph made that global height enormous, so
painting and link hover traversed many off-screen text runs. Painting submitted
those runs to the native painter even though they were clipped later.

The replacement keeps stable run order and builds a balanced extent index after
layout. Each subtree records its greatest bottom coordinate. Its first run's
sorted top coordinate provides the other bound. A synchronous cursor skips a
subtree when it cannot intersect the viewport or pointer's vertical position.
Paint and hover share this traversal. Tall overlapping decorations still appear;
they do not force unrelated off-screen text to be submitted.

The existing 250000-run ceiling bounds the index to 524288 doubles (4 MiB).
Replacement may temporarily retain both old and new indexes. A cursor borrows
the immutable layout only during one synchronous call and uses a fixed 32-entry
stack; the admitted tree needs fewer than 20 pending branches. Index preparation
is linear after the existing sort and remains synchronous. This is not a fix for
initial whole-document Markdown layout latency or a completed responsiveness
audit. These index bytes are additional to the existing text/URL payload budget.

The new 10000-word quote regression failed against the preceding renderer.
With the index, it verifies fewer than 200 submitted text runs in a scrolled
140-pixel-high viewport and retention of the intersecting tall quote rule.
Additional tests verify the tail link's actual hover hit after End navigation
and a tall quoted table with both its border and quote decoration retained.
The final view suite passed in 0.12 seconds. Editor and view suites passed in
2.21 seconds before the final test extension; no application code changed after
that combined run. All 145 files passed the spelling audit. Native executables
compile locally; native validation of this change remains pending.

This evidence concerns submitted drawing work and hit correctness, not measured
wall-clock speed, raster output, physical input latency or all Markdown behavior.

Native run 37096500201 at 6b0d1a8 passed Windows, Linux and macOS tests and
packaging. That validates the extent-index checkpoint, before the metrics-cache
and native stress-measurement follow-up below.

## Initial layout measurement reuse

A follow-up regression showed that initial layout resolves the same repeated
word and space thousands of times. Layout now owns a temporary metrics cache
for its current font. It retains only size/ascent/descent, at most 1024 entries
and 65536 text bytes; tokens longer than 256 bytes are measured without caching.
Map-node overhead is separate from the text-byte budget. Any FontSpec change
clears the cache, and no cache survives the synchronous layout call. This avoids
reusing metrics across font styles, painters, provider changes or later rebuilds.

The repeated-word measurement regression failed before the change and now
requires fewer than ten measurements for the 10000-word fixture. A separate
check verifies that identical italic, bold and code words each receive their
requested font measurements. Editor/views passed in 2.11 seconds; the expanded
view suite subsequently passed in 0.11 seconds. The spelling audit is clean.

The native Markdown test retains its styled-content capture and then loads a
10000-word stress quote. It observes the first native paint through a requested
10 ms timer and reports public presentation counters. A counting painter checks
that the resulting viewport contains between 1 and 2000 stress words rather than
a layout error or the entire document. The five-second readiness limit is not a
latency threshold. The native executable compiles; measured native results remain
pending. Initial parse/layout is still synchronous and not yet interruptible.

Native run 37096824234 at decb372 passed all three platforms and packaging.
Exact first-paint diagnostics are retained in native-*.txt. Single observations:

| Platform | First paint observed ms | Presented duration ms | Visible stress words |
|---|---:|---:|---:|
| Windows x64 | 21.6673 | 21.0547 | 396 |
| Linux x64 | 87.0944 | 86.827551 | 408 |
| macOS arm64 | 70.7535 | 60.649167 | 324 |

These include layout and native presentation work under uncontrolled CI host
conditions. The observation timer adds delivery latency. Different native font
metrics/window sizes affect visible word counts. No before/after native speed
ratio is claimed. These durations leave initial-layout responsiveness unfinished.

## Cancellable parsing worker foundation

`parse_markdown` now accepts a stop token and throws `MarkdownCancelled` without
publishing a partial model. Checks bracket validation, run at each MD4C callback,
run during attribute decoding, and precede final publication. Exceptions stay
inside callback boundaries until MD4C returns. Validation, library work between
callbacks, allocations and cleanup remain noninterruptible within those calls.

`MarkdownPreparation` owns one worker, one replaceable source request and one
completed model. It owns source bytes by value, publishes only while its request
token remains current under the publication mutex, transfers failures through
an exception pointer, and sleeps when idle. Replacement/cancellation clears old
results. Shutdown requests stop and joins. It never retains a UI callback, painter
or native window handle. Caller operations are serialized on the owning executor.
The active source/model may coexist with one pending source and a completed model;
source copying and cancelled-model cleanup are not hard-latency bounded.

All 29 local suites passed in 19.97 seconds, including owned source lifetime,
32 rapid cancellation/replacement cycles, invalid UTF-8 error/recovery, empty
completion, one-time adoption and active-work destruction. Precancellation tests
cover empty, valid and invalid UTF-8 sources. These do not measure mid-library-call
cancellation latency. The spelling audit passes 148 files. The worker is not yet
connected to MarkdownView; that integration and native validation remain next.

Native run 37097170527 at 249f49a passed all three platforms and packaging for
the worker foundation.

## Visible-view worker integration

MarkdownView now submits owned source to the worker and displays a preparation
message instead of parsing on the GUI thread. A pending-only frame request checks
for completion. Source replacement revokes old work and presentation; only the
current completed model is adopted. Errors display an explicit diagnostic.
Escape and leaving rendered mode cancel pending work. Reopening the same cancelled
source submits a fresh request. Disposal cancels and joins before releasing the
worker. No polling remains after completion or cancellation.

The view still copies source, releases prior presentation allocations and performs
native text layout on the UI thread. This integration removes parsing from that
thread; it does not yet make initial layout interruptible or guarantee a deadline.
Both preparation and native layout must finish before presentation_ready is true.
Pending/failed models do not expose old link hit regions or an old rendered page.

Editor/view tests passed in 2.09 seconds. They cover pending display, latest-source
adoption, source-mode cancellation, reopening, Escape, invalid-UTF-8 diagnostics,
source preservation, and zero scheduled requests/dirty marks after completion.
Existing layout tests explicitly await model adoption before testing geometry.
Native tests now await completed presentation, verify settled-idle scheduling and
close with a replacement still pending, measuring through owner release. The
native executables compile locally; execution of this integration is pending CI.

Native run 37097550906 built all three platforms, but the Markdown probe timed
out after its stress paint. The observer changed its own timer interval after
resetting activity counters, contaminating the zero-request assertion. Its timer
callback also lacked a failure boundary that closed the window. The probe now
begins measurement on the following tick and captures exceptions before closing.
This preserves the settled-idle assertion rather than relaxing it. The corrected
native probe compiles locally; four focused headless suites passed in 2.12 seconds
and the spelling audit passed across 148 files. Native revalidation is pending.

## Worker-owned display text

The worker now projects span text into inert display labels before publishing a
distinct PreparedMarkdown model. The raw parser and source remain unchanged.
Native layout borrows prepared span text instead of constructing DisplayPage
objects on every layout or resize. The projection preserves the previous
scalar-aligned 60000-byte chunks; no edit mappings use these artificial edges.
Cancellation is checked between blocks and before/after each display chunk.

Prepared display text has an aggregate 32 MiB logical-byte limit, with an explicit
diagnostic on overflow. This excludes string capacity, block/span metadata, URLs,
the raw source, and a temporary DisplayPage. During replacement, raw span text
and its prepared replacement can coexist. It is not a total allocation ceiling
or a hard cancellation-time guarantee. Native shaping and run layout remain on
the UI thread. Tests cover control labels, unchanged raw parser output, and a
four-byte scalar crossing the chunk threshold, alongside existing cancellation,
replacement, view, and editor checks: four suites passed in 2.26 seconds locally.
The final full headless run passed all 29 tests in 20.68 seconds, and the source
spelling audit passed across 148 files. Native performance comparison is pending;
moving work off the UI thread is not itself proof of lower end-to-end latency.

## Asynchronous parsing native checkpoint

Run 37097937531 at 2c9256f passed native tests and packaging on Windows, Linux,
and macOS. This revision includes asynchronous parsing and the corrected observer,
but predates worker-owned display-text projection. Raw results are recorded in
async-native-*.txt. Observed stress first-paint times were 40.30 ms on Windows,
79.40 ms on Linux, and 67.73 ms on macOS. All three recorded zero paint passes,
presented frames, scheduled frame requests, and frame deadlines during the
settled-idle interval. Closing with replacement preparation pending, measured
through owner release, took 16.46, 21.75, and 47.44 ms respectively.

These are single CI observations, not controlled before/after performance ratios
or guaranteed deadlines. The macOS native-view capture was inspected: heading,
emphasis, quote, task list, table, code, inert link and image-alt text were visible.
It is a native view capture rather than physical-screen or keyboard dogfooding.

The native probe now records synchronous layout duration separately from first
completed paint and worst presentation. This covers native measurements, run
construction, sorting and extent-index construction; it excludes background
preparation and paint submission. It also records the largest gap between its
10 ms observation callbacks during stress preparation. The gap includes host
scheduling and presentation, so it is not an isolated input-latency metric.
Timing uses two clock reads per layout attempt, including failed attempts, and
does not schedule extra product frames. Four focused headless suites passed in
2.35 seconds; the native probe compiled and the 148-file spelling audit passed.
Native timing results for this additional instrumentation are pending.

## Prepared-display native results and URL ownership repair

Run 37098258452 at 7a3e90f passed all three native platform jobs. Raw results
are in worker-display-native-*.txt. Windows/Linux/macOS first completed paints
were observed at 37.98/98.04/116.61 ms, with worst presentations of
24.50/73.77/34.16 ms. Settled idle recorded zero paints, presentations and
preparation-frame requests/deadlines on all three. Pending-work close through
owner release took 18.91/17.24/62.24 ms. These mixed single-run results do not
establish an end-to-end speedup over the preceding checkpoint.

Source review also found that every word copied its span's URL into a run.
Runs now borrow the immutable prepared span URL. Source replacement destroys
runs before their owning blocks; layout replacement borrows the same unchanged
blocks, and member destruction releases runs before blocks. The hovered URL is
still copied into its own string. Prepared-model/parser URL budgets remain;
the run budget counts owned text rather than repeatedly charging borrowed URLs.
The 300-word/60000-byte URL regression now requires a completed rendered page
instead of accepting artificial storage exhaustion. Existing source replacement,
hover, navigation and recovery tests remain in the focused validation set.

## Native drawing follow-up

Partial results from run 37098620036 at e9ab3af distinguish layout from total
presentation: Linux synchronous layout was 4.71 ms versus 76.07 ms worst
presentation; macOS layout was 3.28 ms versus 36.02 ms worst presentation.
Maximum observer gaps were 76.10 and 46.28 ms respectively. These results redirect
the immediate investigation toward drawing/presentation rather than assuming
initial layout accounts for the whole delay. Windows was still running when
this follow-up was prepared.

Space-only runs no longer submit native text-draw commands. They still take part
in layout, hit regions, code background fills, and link/strike decorations.
The tall-document test now rejects space-only text submissions, while existing
navigation, hover and geometry tests continue to pass. Four focused suites passed
in 2.24 seconds and the 148-file spelling audit passed. Native timing and visual
verification of this change remain pending; no speedup is claimed yet.

Run 37098620036 subsequently passed Windows as well, completing all three native
jobs. Windows measured 4.16 ms synchronous layout, 25.38 ms worst presentation,
27.63 ms maximum observer gap and 38.78 ms to observed completed paint. The full
raw lines for all platforms are preserved in layout-native-*.txt. These are the
baseline before shared run URLs and space-only text-draw elision.

## Drawing checkpoint and release preparation

Run 37098988259 at d12b9ed passed native tests and packaging on all three
platforms. Raw lines are preserved in drawing-native-*.txt. Windows/Linux/macOS
first completed paints were observed at 27.79/84.60/131.36 ms; worst presentations
were 12.47/67.85/50.54 ms and synchronous layouts were 4.51/4.08/6.47 ms. All
three reported zero paints and preparation scheduling during settled idle.
The Windows sample improved while the Mac sample worsened; differing CI runs
do not establish causation or a cross-platform speedup. The native Mac capture
was inspected after the change and retains visible spacing, code backgrounds,
link decoration, table, quote and heading presentation.

Version 0.3.6 preparation passed the full 29-test local headless suite in
19.15 seconds and the 148-file spelling audit. Release-tag native validation and
independent archive verification remain separate requirements before claiming
the new release is available.

## Repeated native sampling

The native regression now repeats its 10000-word quoted document 21 times,
changing an off-screen suffix so every sample performs preparation and layout.
It retains the per-sample raw metrics and reports nearest-rank p50, p95, p99 and
maximum for observed completion, synchronous layout, worst presentation per
sample and maximum observer gap per sample. At n=21, p99 equals the maximum.
Every sample checks completed presentation and bounded visible text before
acceptance; settled idle and pending-work close still run after the final sample.

These samples share one process, font resources and host. They characterize
repeated document replacement under that run's conditions, not independent cold
starts, physical input latency or controlled before/after performance. The
15-second native-test timeout remains. The executable compiles locally and the
148-file spelling audit passes; execution is assigned to native CI to preserve
the locally coordinated desktop. No repeated-sample results are claimed yet.

A separate markdown-distinct-native case submits 10000 distinct numbered words.
Source lines contain 20 words each and remain within the current GUI line and
document limits; explicit quote continuation retains one rendered paragraph.
This case performs one observation, independently reports layout/presentation/
observer timing, and retains completion, bounded visible text, idle and close
checks. It does not claim a distribution. The original repeated-word case stays
unchanged so its 21-sample results still describe the same cache-friendly source.
Both native cases have a 15-second timeout; passing that timeout is not an
interactive latency acceptance threshold. This added executable path compiles
locally and passes the spelling audit; native execution remains pending.

## Repeated-word native results

Run 37100001524 at d46151b passed native tests and packaging on all platforms.
The repeated-native-*.txt files retain every sample, aggregate percentiles,
settled idle and close observations. These precede the distinct-word fixture.

| Platform | Completion p50 / p95 / max ms | Layout p50 / p95 / max ms | Worst presentation per sample p50 / p95 / max ms |
|---|---|---|---|
| Windows | 23.78 / 36.39 / 36.65 | 2.65 / 2.89 / 3.00 | 10.13 / 10.41 / 10.45 |
| Linux | 62.42 / 64.41 / 65.26 | 1.62 / 2.26 / 2.42 | 43.17 / 44.25 / 44.56 |
| macOS | 66.76 / 162.22 / 182.78 | 3.77 / 12.70 / 19.62 | 35.64 / 82.42 / 150.11 |

The Mac tail is materially wider than its median. Host scheduling and CI
contention are not controlled, so this is an observed result requiring further
investigation, not proof of a specific provider defect. No full responsiveness
acceptance follows from this run. Closing with work pending took 22.26 ms on
Windows, 20.60 ms on Linux and 19.11 ms on macOS.

## Distinct-word finding and metrics invalidation repair

Run 37100399925 at 4528377 passed correctness/packaging on all platforms, but
the new distinct-word case exposes unacceptable synchronous work. Windows/Linux/
macOS layout took 1527.05/759.69/1654.37 ms, with observed completion at
1595.42/825.83/1761.04 ms. The raw lines in distinct-native-*.txt preserve the
finding. This test passing its 15-second timeout is not a responsiveness pass.
Repeated-word caching is insufficient; native layout needs interruption/yielding
while the public prepared-text integration is developed.

A separate regression demonstrated stale geometry after replacing the text
metrics provider at unchanged width. Markdown now invalidates its layout on a
requested control measurement pass. The test failed before the repair and passes
after it, also checking that subsequent stable paints reuse the rebuilt geometry.
Editor and view tests passed 2/2 in 3.05 seconds; the 148-file spelling audit
passed. This repair does not address the distinct-word stall.

## Resumable UI layout checkpoint

The UI now retains private partial geometry and advances native measurement in
slices targeting 8 ms or 2048 traversal steps, whichever comes first. The clock
is checked between tokens/spans/blocks. Font metrics remain cached within one
layout; no Painter or native handle is retained between callbacks. Complete
geometry is published atomically. Source replacement, width changes and control
measurement invalidation discard partial work. Escape and hiding cancel it;
reopening cancelled source starts again. Scheduling exists only while pending.

The 8 ms target is not a hard deadline: a single native measurement, allocation,
block-column initialization/finalization, final sort/index construction, and
retirement can exceed it. The native harness now logs cumulative work and the
longest individual layout slice separately from completion and observer gaps.
Completion can take longer because frames and input run between slices. Its
readiness timeout is now 15 seconds; the distinct test timeout is 30 seconds and
the 21-sample test timeout is 60 seconds. These are test-hang guards, not latency
acceptance thresholds. Native results for this change remain pending.

Headless regressions cover yielding with deliberately slow metrics, yielding
with a fast large source, no partial text publication, Escape, idle after
cancellation, restarting the same source, metrics/width invalidation during
work, and replacing a partly laid-out source. All 29 local suites passed in
22.04 seconds before the final width-reversal and run-budget guard review;
focused verification follows those adjustments. The spelling audit found zero
findings in 148 files. No local desktop was launched.

Final focused editor/view verification passed 2/2 in 2.84 seconds; the style audit remained clean.

Run 37101481404 at 40c240e passed all three native/package jobs. Raw
sliced-native files retain both fixtures. Linux distinct layout longest slice
was 8.16 ms, but completion rose to 4450 ms over 186 paints; repeated-word
completion also rose to about 223 ms over 11 paints. Yielding fixed the long
measurement callback but exposes unnecessary presentation work between slices.
The next change uses the public UI timer and current window text-metrics service
to advance layout without repainting an unchanged placeholder.

A further regression failed when metrics invalidation erased a worker parse
failure and published an empty successful view. Keeping source readiness false
and separate from layout readiness fixes it; editor/view tests passed 2/2 in
2.45 seconds and style audit remained clean.

The follow-up uses an owned/revocable UI timer with a weak named callback.
Each layout callback borrows the current window metrics service only for the
call. Pending measure/arrange invalidation yields to a paint/layout pass first.
The token is disconnected before each callback and on completion/cancellation;
no persistent idle timer is retained. An unfinished timer slice does not mark
paint dirty. The headless regression verifies that property with slow metrics.
The step ceiling is now 32768; the 8 ms clock target still applies. This avoids
forcing a cache-friendly 10000-word paragraph through 11 presentations.
Editor/view tests passed 2/2 in 2.65 seconds after this refinement; the spelling
audit passed all 148 files. New native measurements remain pending.

Run 37101965709 at c2c8e52 passed native tests and packaging on all three
platforms. Raw timer-native files preserve its metrics. These timings have a
readiness limitation: a timer can finish geometry before native paint, while
the prior readiness predicate checked layout alone. Do not treat that run's
completion/presentation fields as proof that final text reached the screen.
The next revision requires the completed layout to pass through on_paint before
presentation_ready becomes true. A headless regression verifies this transition.

The distinct native case now also routes Escape through Window::dispatch_key
while layout is pending, checks that cancellation survives the following tick
with no scheduled work, reopens the same source, and closes during unfinished
layout. Handler duration and close-through-owner-release are logged separately.
This is routed native-window testing, not physical keyboard latency measurement.
The executable builds locally; its new native path still needs CI execution.
Focused editor/view suites passed 2/2 in 2.74 seconds; the added readiness
regression then passed the view suite in 0.29 seconds. Style audit is clean.

Run 37102333980 at 51cdad7 passed all three native/package jobs with the corrected
paint readiness and routed cancellation tests. Raw painted-native files preserve
all observations. Linux distinct-word layout took 724.70 ms cumulatively across
slices, with a longest slice of 9.47 ms and complete-paint observation at 896.88
ms. macOS took 1290.67 ms cumulatively, longest slice 33.35 ms and completion
2604.86 ms. These are separate CI observations, not controlled before/after
experiments or proof of an 8 ms hard bound. Routed Escape, cancellation surviving
the next tick without scheduled work, reopening and close during unfinished
layout passed on every platform. Full physical dogfood remains open.
