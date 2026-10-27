# Repeated formula viewport latency

Measured on local Windows, Release, installed provider SDK 0322371. The baseline
is application 574cad0 plus the benchmark's new --large fixture selector. The
changed implementation reuses successful identical formula displays within one
synchronous viewport refresh. Five warmup scrolls precede 31 measured scrolls.
Raw samples for all four runs are included here.

The larger fixture is 876,543 source bytes: 8,192 rows, 8 columns, first column 2,
remaining cells =SUM(A1:A8192). The 800×600 viewport renders 85 exact 16384 results.
The original fixture has 512 rows, 51,199 source bytes and 85 exact 1024 results.
The benchmark validates every visible result and rejects error cells after each
scroll. Timings cover wheel handling and viewport preparation, excluding the
subsequent observing-painter check, native rasterization and presentation.

| Fixture / implementation | p50 ms | p95 ms | p99 ms | worst ms |
|---|---:|---:|---:|---:|
| 8192 rows before |89.2768|92.6496|93.8218|93.8218|
| 8192 rows after |1.4212|1.6162|1.7624|1.7624|
| 512 rows before |5.2378|7.3340|7.5068|7.5068|
| 512 rows after |0.1623|0.1702|0.1711|0.1711|

Each refresh's temporary lookup borrows formula text only while the immutable
Csv and viewport remain alive. It points to already prepared display records,
not full dependency lists. Only successful evaluations are reused. Absolute
references are independent of formula location; if the graph reaches another
cell with the identical expression it necessarily repeats that same dependency
path, so a successful result cannot mask a root-specific cycle. Failures remain
individually evaluated. No cache survives source edits, viewport changes or the
refresh call, and exact source formula spelling is unchanged.

View regressions cover duplicate results, editing a dependency, restoring valid
source after cycles, cyclic identical formulas at different roots, and reference
hover text/highlighting on a reused display. All 19 local suites pass. This is a
specific repeated-formula improvement, not a bound on a viewport full of distinct
expensive expressions and not a complete application responsiveness audit.
