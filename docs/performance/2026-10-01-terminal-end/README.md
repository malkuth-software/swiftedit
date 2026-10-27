# Read-only terminal end-navigation measurements

This is partial responsiveness evidence for the shared end-navigation component,
not the final application lag audit and not a Mac GUI idle-CPU result.

The local Release run uses runtime implementation ef156ec, installed SDK
723cd7f, GCC 16.2.0 (MSYS2 Rev4), and a Windows guest reporting AMD EPYC 9354,
4 cores and 8 logical processors. Background load and filesystem cache were not
controlled. Harness SHA-256:
`5880ebb7cd22768f8cf00c5840ef6cde668dbf0c530878eb656ae9f0bdc911e1`.
Measured executable SHA-256:
`dd500b80ccb71e3286aa4df3e1102a0e46a2e3619b3c60db77f419564f393aa2`.
Original Windows raw CSV SHA-256 (CRLF):
`4f754829a43ed3cf7300b609e8d436d4a34f8104d08e0b7591c24804caa48477`.
Git stores the same CSV with LF endings, SHA-256:
`a4e8530315261243a86f22c83462cfd817762d7c604a8c39b8388c5928701e33`.

Each fixture is exactly 16 MiB and ends in ASCII Z: one long ASCII line, NUL
controls, and mixed tabs/Unicode/combining marks/CRLF. The viewport is 80×24.
Five complete scans per fixture produce 1,285 step samples per fixture. Timing
uses steady_clock and includes clock overhead. Percentiles use nearest rank;
step samples are pooled across the five scans. With only five samples, p95/p99
for publication, release, cancellation and total equal the observed worst case.
These tail numbers do not establish a confidence bound.

| Fixture | Step p50 ms | p95 ms | p99 ms | Worst ms | Total p50 ms | Total worst ms |
|---|---:|---:|---:|---:|---:|---:|
| ASCII | 3.1222 | 5.0095 | 5.3608 | 5.7132 | 856.0200 | 977.0819 |
| NUL controls | 3.2805 | 5.3466 | 5.8931 | 6.7856 | 924.8295 | 1050.3808 |
| Mixed | 1.8076 | 2.9874 | 3.1769 | 3.9301 | 513.6218 | 537.2705 |

Worst publication plus first-frame construction was 0.6903 ms. Worst pending
task release was 0.2447 ms. A key arriving during a step still waits for that
step to return; task-release timing alone is not cancellation latency. Native
event delivery, screen drawing, display latency and a real user's machine are
outside this headless benchmark. No timing pass threshold is asserted.

Every completed trial verifies EOF, the final Z, and unchanged document state.
Cancellation runs 32 steps, drops the pending task and verifies that the pager
remains at its original source offset. Raw fixture files remain only in the
owned build output directory. The CSV and summary are retained here.

Reproduce with `swiftedit-terminal-end-bench <new-output-directory>`. The output
directory must not exist. Native CI now runs the same harness after its tests,
with a 120-second process bound, and uploads raw CSV, summary and a receipt
containing source, SDK, platform, executable hash and measurement scope. Native
benchmark execution is verified below.

## Native baseline and unresolved Mac latency

Native run 36956521589 at 325ede5 passed all three platforms, including execution
of the benchmark and its correctness checks. Portable-core run 36956521574 also
passed all three. Exact source/SDK/executable receipts, raw CSV and summaries
are retained in the three `native-*-325ede5` directories.

| Platform | Fixture | Step p99 ms | Step worst ms |
|---|---|---:|---:|
| macOS ARM64 | ASCII | 32.561542 | 206.261250 |
| macOS ARM64 | NUL controls | 15.549166 | 17.549083 |
| macOS ARM64 | Mixed | 10.364208 | 89.263166 |
| Linux x64 | ASCII | 3.757564 | 4.437443 |
| Linux x64 | NUL controls | 4.250011 | 8.879565 |
| Linux x64 | Mixed | 2.437187 | 3.034692 |
| Windows x64 | ASCII | 4.124600 | 6.046500 |
| Windows x64 | NUL controls | 4.381500 | 6.148500 |
| Windows x64 | Mixed | 2.453000 | 2.703000 |

The Mac 206 ms slice is an unresolved responsiveness finding. A cooperative
input check happens only after the current step returns. Green CI establishes
execution and correctness, not acceptable latency. Hardware, background load,
cache state and runner scheduling differ; these figures are not a controlled
platform comparison and do not identify the cause of the Mac tail.

Follow-up instrumentation records process CPU time alongside each wall-clock
slice and complete trial. Windows uses GetProcessTimes; POSIX uses std::clock.
CPU clocks cover the entire process, not only the main thread. Local Windows
observations are quantized in 15.625 ms increments, so zero per-step CPU or CPU
larger than a short wall interval cannot be interpreted literally as the work
of that single step. Whole-trial CPU provides a coarser cross-check. Clock-call
overhead is included in the new wall intervals. The follow-up native results
remain pending; no attribution or performance improvement is claimed.
