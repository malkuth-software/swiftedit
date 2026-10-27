# Query editing component measurements

Measured on Windows x64, AMD EPYC 9354, using GCC 16.2.0 (MSYS2 Rev4),
CMake Release (`-O3 -DNDEBUG`) and public GUI.Forms SDK
723cd7f9f8016d854f667f41e0c5dbcbe4cc0adf. Application source is commit
166b912b8b576e7d95ebd2e5171c1017ee4120ec plus the accompanying benchmark and
CMake target additions. The source changes in this checkpoint do not change
query editing behavior. Receipt recorded 2026-10-02T06:26:18Z.

Benchmark executable SHA256:
`2331de7a0b6f4f6b1bf78102fed9b259378d140e6436a50b85ab3eeb82a1f650`.

Each fixture starts near the 4096-byte query limit with every grapheme flagged
as a wildcard. Each trial inserts literal `x` at the beginning via Window text
dispatch, then performs Ctrl+Z via Window key dispatch. Assertions verify the
inserted character is literal, the original occurrence retains its flag, and
undo restores all original text and flags. Each fixture has 20 unrecorded warmup
pairs followed by 200 recorded pairs. The 1200 raw operation samples are in
`samples.csv`; no outliers were discarded. Percentiles use nearest ranks.

| Fixture | UTF-8 bytes | Graphemes | Operation | p50 ms | p95 ms | p99 ms | Maximum ms |
|---|---:|---:|---|---:|---:|---:|---:|
| ASCII | 4095 | 4095 | Insert | 1.1843 | 2.0242 | 2.8021 | 3.2811 |
| ASCII | 4095 | 4095 | Undo | 0.6826 | 1.1333 | 1.5267 | 1.7033 |
| Unicode é | 4094 | 2047 | Insert | 0.4710 | 0.7025 | 0.7777 | 0.9539 |
| Unicode é | 4094 | 2047 | Undo | 0.2575 | 0.4436 | 0.4976 | 0.7104 |
| Combining e + acute | 4095 | 1365 | Insert | 0.3377 | 0.4237 | 0.4889 | 0.5423 |
| Combining e + acute | 4095 | 1365 | Undo | 0.2300 | 0.2794 | 0.3719 | 0.3813 |

Timing begins immediately before routed dispatch and ends on return. It includes
query snapshot preparation, edit validation, flag mapping, history and UI
invalidation. It excludes fixture preparation, correctness checks, file output,
painting, OS input delivery and physical presentation. No native window is
launched. This single-machine component measurement is not a hard deadline,
cross-platform comparison or completed GUI responsiveness audit. Scheduling and
cache state are uncontrolled.

Reproduce with the installed public SDK configured normally:

```powershell
cmake --build .build/swiftedit-sdk-723cd7f --target swiftedit-query-edit-bench --parallel 2
.build/swiftedit-sdk-723cd7f/swiftedit-query-edit-bench.exe samples.csv
```

The executable requires the same compiler and SDK runtime DLL search paths as
the other SwiftEdit headless test executables.

## Paint preparation follow-through

The accompanying `--paint` mode includes QueryField.on_paint immediately after
each edit and undo, using public fallback text metrics and a counting painter.
It submits visible text but performs no native rasterization or presentation.
The same fixtures, 20 warmup pairs and 200 recorded pairs apply. `paint-before.csv`
and `paint-after.csv` each preserve 1200 samples. The baseline uses the query
implementation from 166b912; the after run adds per-rebuild label-width reuse and
avoids constructing unused literal display text for wildcard bullets.

| Fixture | Operation | Before p99 ms | After p99 ms | Before maximum ms | After maximum ms |
|---|---|---:|---:|---:|---:|
| ASCII | Insert + paint | 4.2066 | 1.3909 | 4.6180 | 1.4885 |
| ASCII | Undo + paint | 3.9868 | 0.8767 | 4.7633 | 0.9907 |
| Unicode | Insert + paint | 1.4664 | 0.7131 | 1.5338 | 0.7891 |
| Unicode | Undo + paint | 1.1522 | 0.4801 | 1.2268 | 0.5500 |
| Combining | Insert + paint | 1.2473 | 0.8212 | 1.2740 | 0.9137 |
| Combining | Undo + paint | 1.1348 | 0.4950 | 1.1509 | 0.6745 |

These sequential single-machine runs have uncontrolled scheduling/cache state;
they do not establish a guaranteed speedup. Tests independently verify one
wildcard-label measurement per rebuild, viewport-only text submissions, and
reuse on caret scrolling. Baseline metrics are still resolved each paint.
Use `swiftedit-query-edit-bench samples.csv --paint` to reproduce this mode.
