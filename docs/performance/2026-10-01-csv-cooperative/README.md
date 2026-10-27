# Cooperative CSV viewport calculations

Distinct visible formulas now run in a revocable frame queue. Each slice stops
between formulas after either eight uncached evaluations or two milliseconds.
A single formula retains its existing 64-cell dependency depth, 100,000-reference
and expression bounds; the two-millisecond check is not an intra-formula deadline.
Literal cells and pending placeholders appear before the queue finishes. Existing
successful repeated-formula reuse remains scoped to this viewport generation.

Changing source, scrolling or resizing replaces pending work. Leaving table view
cancels it; returning to unchanged source rebuilds the cancelled queue. Completion
and disposal disconnect the frame request. Hover details update when the hovered
formula completes, without waiting for another mouse move. No source formulas,
results, clipboard bytes, selection or undo history are rewritten by display work.

## Local component measurements

Windows Release, installed SDK 0322371. Each run has five warmup scrolls and
31 measured scrolls. The new --distinct fixture has 4,096 rows, eight columns,
657,927 bytes, and 85 visible formulas. Each formula sums A1:A4096 and adds a
distinct integer multiplied by zero; every exact result must remain 8192.
The benchmark checks every visible result and rejects errors/placeholders after
completion. It advances callbacks directly, so waiting for native presentation
and user-input dispatch are excluded.

| Distinct-formula observation | Median ms | p95 ms | Maximum ms |
|---|---:|---:|---:|
| Total computation across callbacks | 41.8383 | 43.9533 | 44.3416 |
| Initial wheel handler and preparation | 2.2674 | 2.5648 | 2.5898 |
| Largest slice within each sample | 2.4924 | 2.6314 | 2.6716 |

Callbacks per sample: median 18, p95 19, maximum 20. This improves opportunities
for input dispatch without claiming less total mathematical work. It is not an
end-to-end latency distribution or a universal two-millisecond guarantee.
The repeated 8,192-row fixture still completes with total median 1.4441 ms,
p95 1.6339 ms and maximum 1.6956 ms. Raw samples are retained alongside this file.

Headless regressions verify yielding, placeholders, revocation on source change
and scroll, cancel/restart with unchanged source, hover updates and final results.
An opt-in native CSV fixture requires actual host frame callbacks to finish the
queue without calling on_frame directly. Native validation is recorded by CI;
local tests do not launch the desktop.

Source ed04eda passed native run 36881623545 on macOS, Windows and Linux.
The owned Windows CSV fixture completed through native scheduling in 0.11 s;
that duration is a fixture runtime, not an interaction-latency measurement.
A follow-up adds a no-op guard for late callbacks after completion and extends
native validation with a focus-cleared, settled 200 ms observation. That check
requires zero new scheduled frame requests and frame deadlines after the queue
finishes; native paint metrics are retained diagnostically. It does not measure
focused caret CPU or substitute for the separate Mac idle investigation.

The expanded fixture at source 86fb579 passed on both macOS and Windows in
run 36881985501. Each recorded zero paint passes, frame deadlines, scheduled frame
requests and scheduler wakes in the focus-cleared settled observation. Raw
metrics are preserved as settled-mac-86fb579.txt and settled-windows-86fb579.txt.
Linux validation was still installing dependencies when these observations were
recorded; it must be checked separately rather than inferred from these results.

Run 36881985501 subsequently completed successfully on Linux as well. All three
native jobs and portable run 36881985581 are green. Mac source 86fb579 is published
as v0.2.3-dogfood.20261001; see the dogfood receipt for exact package hashes.
