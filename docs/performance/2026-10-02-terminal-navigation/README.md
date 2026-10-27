# Shared-loop navigation timing

`swiftedit-terminal-navigation-bench NEW_DIRECTORY` drives the real shared
terminal loop through an in-memory console. It creates three >=16 MiB read-only
fixtures, then performs 1000 Down movements and 1000 Up movements per trial.
There are five sequential trials per fixture, a viewport of 80 by 24, and at most
16 movements per slice. Each trial must complete both commands and produce exactly
126 measured screen submissions. Existing terminal-app tests independently verify
caret placement, selection bytes, cancellation and history reconstruction.

The steady-clock interval begins when a command is delivered, or just after the
previous screen submission is recorded. It ends at the next complete screen-string
submission. Vector insertion used to record a sample is excluded. Native console
input, terminal output, compositor/display work, physical input latency and idle
CPU are excluded. Final slices contain eight movements; other slices contain 16.
The source fixtures are created before timing. Cache state is uncontrolled.

Local Windows Release observation, SDK 723cd7f, shared-loop implementation 970cd73:

| Fixture | Samples | p50 ms | p95 ms | p99 ms | Worst ms |
|---|---:|---:|---:|---:|---:|
| ASCII | 630 | 4.4500 | 7.5572 | 8.7791 | 20.1720 |
| Unicode | 630 | 2.6094 | 4.5760 | 4.7672 | 5.2726 |
| Controls / invalid bytes | 630 | 3.7757 | 5.7644 | 6.1269 | 15.0270 |

Percentiles use nearest rank, pooling both directions and all trials. These are
batch times, not individual keystroke times. No universal latency threshold or
causal explanation for outliers is claimed. Raw samples are in samples.csv;
receipt.json records executable and sample hashes. Native CI now runs the same
benchmark and preserves its summary, raw samples and receipt without uploading
the large fixture files. Cross-platform results are pending that first run.
