# Caret status refresh component measurement

Claim tested: caret-only changes should reuse document metadata, rather than
rescanning the document for unchanged dirty state, line endings and text counts.
This is not end-to-end input, rendering, scrolling or accessibility evidence.

`swiftedit-editor-navigation-bench <samples.csv>` constructs a production Editor
and Window, loads 4096 lines of 64 ASCII characters plus LF (266240 bytes), and
alternates a collapsed selection between columns 63 and 64 of the final line.
Each iteration verifies the actual status label. The first 100 selections warm
the path; the next 1000 record steady-clock duration for TextBox::select and its
synchronous editor callbacks. Disk writes and correctness checks are outside
the measured interval. No native window or paint loop is run.

| Milliseconds | Before | After |
|---|---:|---:|
| p50 | 0.1635 | 0.0035 |
| p95 | 0.2006 | 0.0045 |
| p99 | 0.2759 | 0.0114 |
| Worst | 0.4902 | 0.1602 |

Raw samples: `before.csv`, `after.csv`. Percentiles use sorted zero-based
indices 499, 949 and 989. One run per implementation, sequential on the same
host; other host activity and power state were not controlled. These results
are evidence for this warm callback workload only, not universal latency bounds.

Environment: Windows 11 Home 10.0.22621, reported AMD EPYC 9354 32-Core Processor;
MinGW GCC 16.2.0, CMake Release, `.build/swiftedit-sdk-6def54a`, pinned SDK provider
6def54ad2bf2089b57c09337c0b3f82cc1178187. Measurement time approximately
2026-10-01 11:16–11:18 UTC.

Baseline production source is commit `deeb5fe` with editor.cpp blob
`b3f0b6bc5eafa42a041fc6e513b7e81f7c4643f7`. Candidate editor.cpp blob is
`36faf4628a402e04ef24d0bfcca9f9e0ad706e3e`. The same benchmark source blob is
`bd177da5c0ce0707556c58f49f624d546b8d9240` for both.

Baseline executable SHA-256:
`c274ada72294f4fc4a2865a9653153b80cc427b308c355acb809e27f0274ef17`.
Candidate executable SHA-256:
`8635cbe2a1bdabcd6e7dacff16a22713ca9c77537c7c60bdddef1c94370dda30`.

The candidate separates selection refresh from document refresh. Text changes
and save/new/open transitions refresh content metadata and format information;
selection callbacks only query cached line/grapheme metadata and update selection
commands. Failed metadata preparation invalidates the cache for retry. The
pinned provider emits text changes before selection changes; regressions cover
same-byte-length replacement, undo and line-ending changes, as well as combining
marks, joined emoji, mixed endings, empty documents and trailing empty lines.

Full local build and all 16 headless suites passed in 2.04 seconds. Semantic
review checked callback ordering, invalidation on failure, document transitions,
and the absence of retained source borrows. C++ spelling scan: 72 files, zero
findings. Full document edits still rebuild metadata; other input, paint and
large-file latency work remains open.
