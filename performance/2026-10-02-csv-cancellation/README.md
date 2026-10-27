# Formula cancellation checkpoint

The evaluator accepts an optional C++ stop token. It checks before evaluation,
at cell references, every 256 bytes of numeric parsing, during SUM/AVERAGE and
MIN/MAX aggregation, and before returning a completed result. Cancellation throws
the distinct CalculationCancelled exception. Reference error decoration preserves
that type instead of converting cancellation into a cell error. Csv is borrowed
read-only and must remain alive and immutable until execution finishes.

This is an evaluator capability, not connected GUI background execution. The
public host wake callback is valid only during window lifetime, with workers
stopped before closed. Consumer close/exception paths, bounded work/result queues,
stale-result rejection and worker ownership still need implementation and native
validation. Existing GUI callers use the default token and remain synchronous
within each formula. Allocation and destruction are not hard-latency bounded.

Local Windows Release build with public aaca5d0 SDK: all 27 tests passed in
18.76 seconds, and the style audit found zero spelling findings in 142 C++ files.
Tests verify pre-cancelled literal/formula/expression refusal, source preservation,
and an independent exact evaluation after cancellation. Existing arithmetic,
reference metadata, cycle and depth tests also pass.

`swiftedit-csv-cancel-bench samples.csv` constructs an immutable 8 MiB numeric
operand made of leading zeroes and a final 1, with a second cell =A1+A1. A worker
signals entry before evaluation. The measuring thread yields for at least 100
microseconds after observing entry, then requests stop and measures until join.
The benchmark checks the typed outcome, unchanged source and subsequent result 2.
Entry notification does not prove which evaluator instruction had executed at
the stop request. Scheduling is uncontrolled; this is not an interruption bound.

`interrupted.csv` records eight cancellations, with request-through-join times
0.0567–0.1284 ms. This includes exception unwinding and worker exit, but excludes
table construction and the initial delay. It does not measure worst-case cache
destruction, GUI close, or native presentation. `samples.csv` preserves the first
probe using sleep_for(1 ms): all eight evaluations finished before cancellation
was observed, so those measurements do not demonstrate cancellation.

`distinct.csv` and `distinct.txt` measure the existing distinct-formula viewport
fixture with default, non-cancellable GUI calls after this change. Median was
18.0489 ms and worst 19.5296 ms, compared with the preceding 18.7139 ms median.
Sequential uncontrolled runs show no observed regression here; they do not prove
zero overhead. See ../2026-10-02-csv-view/README.md for fixture and timing scope.

This work follows the immutable v0.3.4 release and is not included in its assets.
