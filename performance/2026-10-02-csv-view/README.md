# CSV viewport component audit

Measured 2026-10-02 on the local Windows host, MinGW build using the installed
public aaca5d0 paired SDK. Production source is 31acdfb; the benchmark change in
this checkpoint adds separate setup and paint-submission timing. No native
window is shown. Raw CSV samples and console receipts are retained alongside
this file; no outliers have been removed.

The fixture is an 800 by 600 production CsvView. Each scroll must display all
85 exact formula results with no error. The first five of 36 alternating wheel
events are warm-up; 31 are recorded. Default uses 512 rows, repeated uses 8192,
and distinct uses 4096 with a different zero-valued suffix on each formula.
Each formula sums the numeric first column. Source construction and window
construction are outside setup timing; set_source, layout and initial formula
completion are recorded separately in the text receipt. Setup is a single
observation, not a latency distribution.

| Fixture | Source bytes | Scroll p50 ms | Scroll p95 ms | Worst scroll ms |
|---|---:|---:|---:|---:|
| Small repeated | 51,199 | 0.1829 | 0.2457 | 0.2469 |
| Large repeated | 876,543 | 1.4234 | 1.7554 | 1.9971 |
| Distinct | 657,927 | 46.3954 | 111.962 | 150.761 |
| Distinct repeat | 657,927 | 42.8655 | 45.0082 | 45.2582 |

The first distinct run's worst synchronous slice was 32.90 ms; the repeat's
was 2.78 ms. Initial source preparation was 3.74–4.18 ms for distinct formulas
and 7.01 ms for the large repeated fixture. Initial complete distinct results
took about 52 ms. These observations do not establish the outlier's cause.
Scheduling and system load were uncontrolled. The slow observation remains
part of the evidence and prevents claiming a hard responsiveness bound.

`milliseconds` measures wheel handling through calculation completion with
on_frame driven immediately by the benchmark. It excludes real event-loop waits,
OS input delivery, native text shaping/rasterization and screen presentation.
`initial_milliseconds` is the wheel handler alone. `worst_slice_milliseconds`
includes that handler and each subsequent on_frame call. Paint timing measures
the production on_paint traversal submitting to a counting painter, not native
drawing; its maximum was below 0.04 ms here. Calculations yield between formulas
after a two-millisecond target or eight evaluations, not within a formula.

This component audit improves coverage of the requested lag scan. Native input
to presentation measurements and interruption within expensive individual
formulas remain outstanding; these results do not close the full GUI audit.

## Short-literal allocation reduction

The subsequent implementation avoids allocating dependency-cache map nodes for
numeric literals of at most 32 bytes. Longer literal strings remain cached so
repeated references cannot repeatedly scan large runs of leading zeroes. Formula
subtrees remain cached, and the reference-work and dependency-depth limits are
unchanged. Tests retain the 64/65-cell boundary checks and additionally exercise
repeated references to short and long numeric strings with exact reference lists.

`literal-*` records the initial prototype, which bypassed caching for all numeric
literals. Review rejected that unrestricted path because of long-string repeated
reference cost. `short-literal-*` records the final bounded path after all 27
local tests passed in 19.43 seconds; it is the implementation to compare here.

| Final fixture | Scroll p50 ms | Scroll p95 ms | Worst scroll ms |
|---|---:|---:|---:|
| Distinct | 18.7139 | 19.6017 | 19.8455 |
| Large repeated | 0.5610 | 0.7724 | 0.8930 |

The final distinct run's worst slice was 2.2577 ms, with initial complete results
at 22.58 ms. Compared with the pre-change repeat's 42.87 ms median, the measured
median decreased about 56%. Runs were sequential on an uncontrolled host; this
is component evidence, not a promised latency or proof that the earlier outlier
cannot recur. Individual formulas still do not yield internally.
