# CSV rectangle Clear measurement

The prior Clear erased selected fields in reverse source order, preserving
offset validity but shifting a growing suffix once per cell. The replacement
validates the entire rectangle, reserves one result buffer, and appends the
unchanged gaps between selected source spans in forward order. Delimiters,
record endings and unselected quoted fields stay byte-exact. Source is immutable;
failure does not publish partial changes or invent missing fields in ragged rows.

`swiftedit-csv-clear-bench samples.csv` constructs a 1000-row by 100-column CSV
(100000 cells,899999 bytes) before timing. Every field contains eight ASCII
characters. Three warmups precede31 measured full-rectangle clears. Each output
is checked against the exact expected commas/newlines, and the original final
cell is checked unchanged. Validation and CSV sample writes are outside timing.
Parsing, GUI publication, undo, painting and native input are not measured.

| Milliseconds | Before | After |
|---|---:|---:|
| p50 | 85.4735 | 0.3082 |
| p95 | 87.0957 | 0.3360 |
| p99 / worst | 87.4842 | 0.3371 |

Raw samples: before.csv and after.csv. Nearest-rank indexes15/29/30 of31 sorted
samples. One sequential run per implementation, same Windows11 Home10.0.22621
host (AMD EPYC9354), MinGW GCC16.2.0, CMake Release and pinned6def54a public SDK.
Background activity/power state uncontrolled. This is a component improvement,
not an end-to-end Delete latency bound or the final application lag audit.

Baseline production commit4e15c3036132c83c9da6c3d410ff8c6c9b1fdca5.
Candidate csv.cpp blob a99927bcb470d90156335ad0ec0166ad697c15f6.
Shared benchmark blob49e241031f237d73fd34c8d5aca1e86589f99022.
Baseline executable SHA256:
5BECF095DB023A02C0BE47EF44AD8A25FD1DBB56AF0A2AC8145EF3AF4439F393.
Candidate executable SHA256:
471ABB4ADCEBE3668C61A7B302C5BC536C35C82E1C507AC225A9AA72F2244758.

All17 headless suites passed2.48s. Added regressions cover quoted multiline
selected cells, escaped quotes, unselected quoted fields, mixed record endings,
empty-field no-op and atomic refusal of ragged rectangles. Existing editor
rectangle-clear behavior remains covered.79-file spelling audit clean.
