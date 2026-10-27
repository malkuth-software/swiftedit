# Native-run terminal navigation evidence

Source revision: `ea3a73c4d3d2eff810c165e864b71d9c00c3b2c3`.
Provider SDK: `723cd7f9f8016d854f667f41e0c5dbcbe4cc0adf`.
Source: native-evidence artifacts from
[run 36969494053](https://github.com/falseywinchnet/swiftedit/actions/runs/36969494053).

Each platform directory preserves the benchmark receipt, summary, and 1,890 raw
samples. The receipts were checked for the exact source revision, platform,
completed status and zero exit code before copying. These are measurements of
the headless shared terminal loop running on native CI hosts. They are not GUI
timings or physical terminal presentation measurements.

The benchmark uses an 80×24 viewport over three files of at least 16 MiB. Each
fixture runs five trials of 1,000 repeated Down movements followed by 1,000 Up
movements. A dispatch slice performs at most 16 movements. There are 630 samples
per fixture, pooled across trials and directions; percentiles use nearest rank.
The measured interval ends at screen-string submission and excludes sample
vector insertion, OS input delivery, console output and physical presentation.

| Platform | Fixture | p50 ms | p95 ms | p99 ms | Worst ms |
|---|---|---:|---:|---:|---:|
| macOS ARM64 | ASCII | 7.115 | 8.000 | 8.596 | 41.014 |
| macOS ARM64 | Unicode | 4.182 | 4.972 | 5.923 | 23.463 |
| macOS ARM64 | Controls/invalid bytes | 6.570 | 7.267 | 8.349 | 12.072 |
| Windows x64 | ASCII | 5.286 | 6.699 | 7.113 | 9.363 |
| Windows x64 | Unicode | 3.118 | 3.263 | 3.887 | 4.193 |
| Windows x64 | Controls/invalid bytes | 3.923 | 4.101 | 4.582 | 5.364 |
| Linux x64 | ASCII | 2.912 | 3.374 | 3.430 | 4.015 |
| Linux x64 | Unicode | 1.628 | 1.715 | 1.733 | 1.927 |
| Linux x64 | Controls/invalid bytes | 2.356 | 2.497 | 2.525 | 2.740 |

Cache state and CI scheduling are uncontrolled. Different hosted machines do
not establish an operating-system performance ranking. No hard performance
threshold was configured. The Mac tail outliers remain in the evidence; this
run does not distinguish application CPU work from scheduling delay. The data
supports bounded event batching and an observed latency distribution, not a
hard wall-time guarantee or completion of SwiftEdit's full responsiveness audit.

Git may normalize CSV line endings; row values are preserved. Receipts retain
the measured executable hash, while artifact byte hashes must be checked against
the original downloaded artifacts rather than line-ending-normalized repository
copies.
