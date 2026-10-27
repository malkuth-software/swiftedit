# Paged word-count responsiveness

Measured on the local Windows host using a Release build of the source at
032dc62342f60b5da4b4503e982e2c8f5202f30a plus the benchmark added with this receipt.
Installed provider SDK: 6def54ad2bf2089b57c09337c0b3f82cc1178187. This does not
measure or contain the pending Mac idle CPU fixes.

The benchmark creates and owns a 64 MiB local file. Each 64 KiB block repeats
`a`, U+00A0, `b`, ASCII space, followed by padding spaces. The expected count is
26,843,136 words. Every scan verifies that count and the unchanged dirty state.
The ordinary retained paged-file adapter and SessionWordCount task perform the
reads. Each budget has one unrecorded warm-up pass and three recorded passes.
The samples cover each step's allocation, read, validation and counting work.
Fixture construction and CSV output are outside the timed region. Full-scan
time includes task stepping and timing/sample bookkeeping, but no terminal UI.

| Step budget | Samples | p50 ms | p95 ms | p99 ms | Worst ms | Mean full scan ms |
|---|---:|---:|---:|---:|---:|---:|
| 4096 bytes | 49152 | 0.0100 | 0.0171 | 0.0201 | 0.1423 | 193.603 |
| 65536 bytes | 3072 | 0.1280 | 0.1951 | 0.2588 | 0.4221 | 142.002 |

Percentiles use nearest rank over the recorded steps. Raw samples and console
summary are stored beside this receipt. The 64 KiB budget matches terminal F6;
the command protocol also lets callers choose smaller budgets. Cancellation
checks occur between steps. These observations support the current byte budget
on this host; they are not a guaranteed cancellation deadline. Cold storage,
network filesystems, concurrent load, OS input latency and Mac performance were
not measured. No before/after speedup is claimed.

The separate Windows CI run 36871347317 at 032dc62 passed all 20 tests, including
the native owned-console test in 0.27 seconds. That test asserts completed F6
counting and cancellation on a 16 MiB read-only document, and restores console
modes. It is functional evidence, not this timing distribution.

Benchmark executable SHA256:
`c958080c7e83d0da12cd049ea31dd10084bab9d9eca2480ac87dcd3746f42476`.

Reproduce with the opt-in `swiftedit-word-count-bench` target and a destination
CSV filename. It is not registered as a timing-sensitive CTest gate.
