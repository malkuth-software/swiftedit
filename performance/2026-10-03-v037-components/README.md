# SwiftEdit v0.3.7 component responsiveness check

Measured locally on Windows x64 at source 6272487e10af9cf8829c973143b30b2d6064a42e,
GCC 16.2 Release, using the frozen aaca5d0 SDK. The existing headless benchmarks
ran sequentially after rebuilding; no desktop window or native compositor was
launched. Other system activity was not controlled. Compressed CSVs preserve
all retained samples. Percentiles use the benchmark's documented sorted indices.

| Component | Retained samples | p50 / p95 / p99 / maximum milliseconds |
|---|---:|---|
| Caret/status navigation, 266240 bytes / 4096 lines | 1000, after 100 warmups | 0.0023 / 0.0025 / 0.0075 / 0.0507 |
| ASCII query insertion plus fallback paint, 4095 bytes | 200, after 20 warmups | 0.9687 / 1.7495 / 1.9788 / 2.8015 |
| ASCII query undo plus fallback paint | 200 | 0.5418 / 1.1569 / 1.3702 / 1.8572 |
| Unicode query insertion plus fallback paint | 200 | 0.4802 / 0.5747 / 0.6252 / 0.7723 |
| Combining query insertion plus fallback paint | 200 | 0.3368 / 0.4357 / 0.4869 / 0.6173 |
| Distinct-formula CSV viewport completion | 31, after 5 warmups | 46.4606 / 46.9699 / 77.4796 / 77.4796 |

Navigation verifies the resulting status line. Query checks source restoration
and all wildcard flags on every undo; its Painter uses estimated fallback
metrics, so native shaping and raster cost are excluded. The CSV fixture is
657927 bytes, with 4096 rows and seven distinct SUM formulas per row. Every
sample verifies 85 visible exact results and no displayed errors. It alternates
wheel direction and waits for the owned worker using one-millisecond sleeps;
its completion interval includes polling delay.

CSV initial source setup took 3.7722 ms, layout 0.1265 ms, and initial completion
48.7258 ms. Across retained wheel samples, initial event work had a 0.1232 ms
median / 0.1830 ms maximum; the worst UI adoption slice per sample had a 0.8502 ms
median / 1.2939 ms maximum. Fallback paint submission had a 0.0137 ms median /
0.0339 ms maximum. Those are not physical scroll-to-display measurements.

Commands (from the repository, with the frozen runtime DLL directories on PATH):

```powershell
.build/swiftedit-sdk-aaca5d0/swiftedit-editor-navigation-bench.exe .build/responsiveness-v037/navigation.csv
.build/swiftedit-sdk-aaca5d0/swiftedit-query-edit-bench.exe .build/responsiveness-v037/query.csv --paint
.build/swiftedit-sdk-aaca5d0/swiftedit-csv-view-bench.exe .build/responsiveness-v037/csv-distinct.csv --distinct
```

These checks support narrow application-work conclusions. They do not close the
native performance, 16 MiB GUI integration, physical keyboard/clipboard, or full
bug audit. Markdown native results and their limitations are recorded separately
in ../2026-10-02-markdown-visibility/README.md.

## Release native visual inspection

Inspected the two saved macOS view captures from release run 37102666871:
`macos-markdown.png` and `macos-markdown-menu.png`. The Markdown fixture shows
readable heading/body styles, table columns, code text/backgrounds, inert link
text, alt-text-only images and the rendered ruler. The editor capture shows a
checked Markdown Rendered View menu item, no initially highlighted menu command,
no extra filename row below the menu, and the bordered/inset status bar. These
are screenshots of the toolkit view; they do not include or prove the native
title-bar filename, physical menu interaction or idle process CPU.
