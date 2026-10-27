# CSV viewport worker checkpoint

Viewport formula evaluation now runs on one lazily created worker per CsvView.
The worker receives shared immutable Csv storage and an owned batch of at most
4096 cell addresses, matching the maximum 128 by 32 visible grid. It never calls
controls, windows, host handles or wake callbacks. A replaceable request slot and
at most 64 queued completions and 800000 queued references bound the queue. The
reference budget preserves the former eight-result worst case; each queue entry
counts its references even when storage is shared. The worker waits on a condition
variable when idle or when the result queue is full. It retains one successful
formula result for identical formula reuse; errors remain local to their roots.

Source and viewport changes request cancellation and discard queued results under
the same mutex used by publication. An obsolete request's stop token is checked
again before storing a completion. New work can replace a pending request without
joining the running worker. Completion addresses are checked before UI adoption.
Disposal cancels and joins, and the destructor also joins on other lifetime paths.
No worker completion can dereference a closed GUI object. All aliases must leave
submitted Csv objects immutable; CsvView constructs const storage directly.

While calculations remain pending, the UI schedules a completion check with a
one-millisecond requested delay. The UI drains results to a two-millisecond
target, preserving placeholders, reference hover,
error display and exact stored source. No calculation checks are scheduled after
completion or cancellation. Empty checks do not explicitly invalidate the view.
The worker sleeps when idle. This avoids an after-close host-wake dependency, but
native scheduler latency and active polling overhead require measurement.

Queue bounds count results, not allocator bytes. Each result can retain up to the
existing 100000-reference evaluation limit; the active calculation, last reused
result, queued results and UI-consumed result may coexist. Allocation, reference
metadata preparation, source parsing, queue cleanup and joining are not guaranteed
to meet a wall-clock deadline. Convert to Value now reuses a current successful
cell's exact result (2525a2f), preserving the original source elsewhere. Pending
conversions now wait for the existing viewport worker instead of recalculating
on the UI thread. Escape, a changed selection, focus loss, source replacement,
viewport replacement, editing, or leaving table view revokes the pending intent.
The intent is cleared before publishing the change because an editor listener
can synchronously replace the source. Source replacement and Undo publication
still run on the UI thread; this does not establish a total command deadline.

## Measurements and rejected first attempt

Local Windows Release build using installed public aaca5d0 SDK. The existing
657927-byte distinct-formula fixture verifies 85 exact visible results on every
sample. Its harness now requests sleep_for(1 ms) between polling turns and uses
a five-second watchdog rather than a synchronous step-count limit. Actual sleep
granularity is uncontrolled and may be much coarser than one millisecond.

The first prototype handed off one cell per poll. The retained
single-result-poll.csv shows median completion of 1346.75 ms and maximum
1362.47 ms. That orchestration was rejected. The batched producer can calculate
ahead until its eight-result queue fills: batched-distinct.csv records median
185.796 ms, p95 201.105 ms and maximum 201.651 ms. The maximum initial UI handler
was 0.2016 ms and maximum measured UI slice was 0.4671 ms. The final small change
to suppress empty-check invalidation was made after these samples and passed the
full test suite; timings were not rerun for that change.

These totals include polling sleeps and cannot be compared as calculation-only
timings with earlier synchronous reports. The result delay remains materially
visible in this harness despite shorter UI work slices. Native input-to-screen
latency, cancellation/close latency and actual scheduler result delay are still
unverified; this is not a completed snappiness audit.

## Validation

All 28 local tests passed in 19.03 seconds. Worker tests cover source lifetime,
one-time consumption, a 40-result batch exceeding queue capacity, cancellation
with potentially queued results, 32 rapid cancellation/replacement cycles,
formula-error recovery and destruction with active work. View tests cover stale
source/viewport rejection, pending hover updates, duplicate formula reuse and
zero extra scheduling/dirty marks after completion. The C++ spelling audit passed
145 files. Native run 36998142520 at 3f7c96f passed all three platforms and
packaging. After restart, all 28 local tests including cached conversion passed
in 28.90 seconds; the spelling audit remains clean at 145 files.

## Native measurement extension

The next native CSV test retains the settled-idle check and Mac visual capture,
then loads the 4096-row distinct-formula fixture. Only the native frame scheduler
advances calculation; a ten-millisecond requested timer observes readiness and
routes Down/Up through the native window's model dispatcher. The test checks
selection restoration and all 85 exact result labels through a counting painter.
It records source preparation, maximum observation-timer gap, routed key callback
duration and observed completion time. These are event-loop/model measurements,
not physical key-to-screen or raster/compositor latency.

After completion, the test restarts pending work and requests window close. It
records time through Application::run return and retained test-owner release,
including worker teardown. No hard latency threshold is imposed from one CI run;
five seconds is a readiness watchdog.

Native run [36998730463](https://github.com/falseywinchnet/swiftedit/actions/runs/36998730463)
at 5801fc7 passed all three platforms and packaging. The retained native-*.txt
files are exact CSV diagnostic lines from each uploaded LastTest.log.

| Platform | Source preparation ms | Completion observed ms | Maximum timer gap ms | Maximum Down/Up callback ms | Close through owner release ms |
|---|---:|---:|---:|---:|---:|
| Windows x64 | 4.3873 | 107.715 | 17.4862 | 0.0299 | 16.2634 |
| Linux x64 | 3.13981 | 168.581 | 14.9136 | 0.005899 | 13.9812 |
| macOS arm64 | 5.42792 | 615.349 | 265.452 | 0.110209 | 33.0789 |

These are single CI observations on different hosts, not controlled platform
comparisons. The Mac delay needs investigation despite functional success. A
quick routed callback does not establish prompt input delivery or presentation.
Settled-idle frame deadlines and scheduled requests were zero on every platform.

The next test revision resets activity metrics before the stress fixture and
prints public native counters at completion and observation gaps above 50 ms.
Presentation count, cumulative/worst presentation time, paint count and frame
scheduling counters can help locate the delay. Logging a delayed observation can
itself influence subsequent timing; these remain diagnostic observations, not
performance acceptance thresholds. This extension compiles locally; its native
results remain pending.

## Presentation trace follow-up

Native run [37094617212](https://github.com/falseywinchnet/swiftedit/actions/runs/37094617212)
at 7a59318 passed all native jobs and packaging. Exact diagnostic lines are
retained in trace-*.txt. All three hosts presented 12 stress frames.

| Platform | Completion observed ms | Maximum timer gap ms | Total measured presentation ms | Worst measured presentation ms |
|---|---:|---:|---:|---:|
| Windows x64 | 84.225 | 13.7472 | 78.4894 | 6.9604 |
| Linux x64 | 224.786 | 20.1529 | 219.489463 | 20.067897 |
| macOS arm64 | 425.989 | 97.8794 | 109.557127 | 13.607041 |

The Mac's measured worst presentation is shorter than its largest timer gap;
the counters do not attribute the remainder to a specific cause. The repeated
result-publication frames and native scheduling are the next profiling targets.
These counters are the public provider's instrumentation, not OS input delivery
or end-to-end compositor timing. The changed CI host conditions and extra logging
prevent treating the lower Mac total as an optimization result.

Deferred conversion regression checks cover one-step Undo through the editor,
both menu routes, exact cached values, errors, stale source, Escape, selection,
focus and table-view cancellation. The native context-menu test now returns to
the event loop before asserting its result, retaining the same source/Undo/save
assertions. Native run 37095184375 at 0c205bc passed all three platforms and
packaging, including the deferred context conversion and Undo/save assertions.

## Queue throughput experiment

The eight-completion queue and eight-distinct-results-per-frame adoption limit
forced at least eleven calculation-adoption frames for the 85-result fixture.
The current implementation admits up to 64 results while charging each result's
reference count against 800000 queued references. A single calculation remains
limited to 100000 reference operations. The producer waits before computing when
the count limit is reached, then checks both bounds before publication. Taking,
replacement and cancellation update reference accounting under the same mutex.
The active calculation and previously reused result remain outside queue counts,
as before. These are logical limits, not a strict committed-memory budget.

UI adoption retains the two-millisecond elapsed-time target without an additional
eight-result cutoff. Individual adoption, allocation and cleanup can exceed that
target; it is not a hard deadline. Source, error, hover, cancellation and exact
result semantics are unchanged.

Sequential local Windows Release measurements, same 657927-byte fixture and
31 samples after warmup, are retained in queue-before.csv and queue-after.csv:

| Measurement | Before | After |
|---|---:|---:|
| Completion median ms | 185.787 | 46.4696 |
| Completion p95 ms | 201.309 | 46.9169 |
| Completion maximum ms | 217.335 | 46.9933 |
| Maximum UI slice ms | 0.4599 | 1.3833 |

The post-change harness recorded four slices per sample, including the initial
handler. Totals include requested one-millisecond sleeps whose actual Windows
granularity is uncontrolled. This is evidence for reduced frame handoffs in the
component harness, not a native input-to-screen claim. Native validation remains
pending.

The worker tests now drain 160 small results beyond the count cap and 20 results
with 80000 references each beyond the reference budget, then exercise cancel and
replacement. The first heavy fixture was incorrectly unquoted CSV and failed;
after quoting its comma-containing formula, the worker suite passed in 0.10 s.
The other 27 local tests passed in the preceding full run. The spelling audit
passes all 145 files; native test executables compile locally.

Native run [37095512701](https://github.com/falseywinchnet/swiftedit/actions/runs/37095512701)
at e885cc5 passed all three platforms and packaging. Exact diagnostics are retained
in queue-native-*.txt. The following are single observations on independent CI
hosts; fewer presentation frames corroborate the reduced handoffs, but these are
not controlled before/after speed ratios or physical input-to-screen measurements.

| Platform | Completion observed ms | Maximum timer gap ms | Close through owner release ms |
|---|---:|---:|---:|
| Windows x64 | 36.6861 | 13.3501 | 21.8953 |
| Linux x64 | 61.2073 | 19.8858 | 17.9626 |
| macOS arm64 | 37.6588 | 20.2878 | 14.4845 |

Mac and Linux each presented three stress frames, compared with twelve in the
preceding diagnostic run. Settled-idle scheduling checks and all exact-result,
navigation, pending-close and deferred-conversion tests passed on every platform.
