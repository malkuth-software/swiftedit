# CSV viewport refresh baseline

`swiftedit-csv-view-bench samples.csv` constructs a production CsvView and
headless Window at 800x600. The 51199-byte CSV has 512 rows and 8 columns:
column A contains 2, and every other cell contains =SUM(A1:A512). The viewport
shows 17 rows and 6 columns, including 85 formulas. After 5 warmup scrolls,
31 alternating three-row wheel movements measure synchronous viewport refresh.
A recording Painter verifies exactly 85 displayed results equal 1024 and no
error labels after every movement. Painting, validation and output writes are
outside the timed interval. This does not measure a native event/paint loop.

| Statistic | Milliseconds |
|---|---:|
| p50 | 4.9806 |
| p95 | 5.3842 |
| p99 / worst | 5.3898 |

Raw data: baseline.csv. Nearest-rank percentiles use sorted indexes15/29/30 of
31 samples. One run on Windows11 Home10.0.22621, AMD EPYC9354, MinGW GCC16.2.0,
CMake Release, build .build/swiftedit-sdk-6def54a and provider SDK6def54a.
Other host activity/power state were not controlled. This is a measured baseline,
not a before/after optimization claim or proof of responsiveness at maximum
viewport/formula/dependency limits. Formula preparation remains synchronous.

The measured core includes the dependency-depth cache correction: cached numeric
results carry their longest dependency path, so prior shallow evaluation cannot
bypass the 64-cell path bound when reused deeper. Regression tests reproduce
the previous failure and cover accepted64/refused65 in both reference orders;
failed Convert to Value retains the formula.

Source blobs:
csv.cpp ddcf5587b36c83abf40ec86e1848c612d8b96a73;
csv_view.cpp e6b428e4078469dae103a6159c7b6bee2e4f4038;
csv_view_bench.cpp b1fcdaa4b02c4c22c84675f108ea53845c823958.
Benchmark executable SHA256:
F16B5688E4D08EEB9F6E7CBFEBF587D01B720FAC72639085CCFC22A58EFD027C.
All17 headless suites passed2.71s;78-file spelling audit clean. No desktop
launch was needed for this component workload. Broader CSV native/lag coverage
remains part of the full owner objective.
