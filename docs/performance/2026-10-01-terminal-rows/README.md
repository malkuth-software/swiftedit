# Terminal long-line rendering measurements

Measured 2026-10-01 on the Windows development host, reported CPU AMD EPYC 9354,
4 exposed cores / 8 logical processors. GCC 16.2.0 MSYS2 Rev4, CMake Release
(`-O3 -DNDEBUG`), C++20. Public GUI.Forms and picker packages are the frozen
`house-style-final/windows-x64` SDK pair. No provider source was changed.

The opt-in `swiftedit-terminal-bench` target contains both the complete-line
reference traversal (`--reference`) and the indexed production path (default).
Both runs used the same binary, SHA256
`072D2AFF3A720A412E97BA832E8818FA88B048EE8D1C5C90DC0773D3F4D40534`.
The reference run preceded the indexed run. Normal host scheduling noise is
included; this is one paired run, not a hardware-independent performance promise.
Individual samples and nearest-rank p50/p95/p99/worst summaries are retained here.

## Workloads and timing boundaries

* ASCII: one logical line of 102,400 or 1,048,576 `x` bytes. Mixed: 8,192 repeats
  of ASCII `a`, tab, CJK U+754C, `e` + U+0301, ESC, U+1F600 (106,496 bytes).
* First row: one sample for an 80-cell viewport, **after navigation metadata
  has already been built**. Indexed results include initial sparse index
  construction. This is not file-open or full cold-start latency.
* Warm rows: 101 samples for 100 KiB/mixed and 31 for 1 MiB; the viewport traverses
  the whole line, including its right edge. Each sample includes returned row
  construction, clipped source mapping and glyph strings. Selection is collapsed.
* Insert and row: 11 successive single-character insertions at the beginning,
  each followed by an 80-cell row. Includes Session snapshot undo, metadata
  reconstruction, index invalidation/reconstruction and visible layout.
* Samples use `steady_clock`, milliseconds. Source fixtures are in memory, no
  desktop input/console writes are timed. With 11 samples, p95 and p99 equal the
  maximum; they are not statistically strong tail estimates.

## Results

| Workload | Reference p50 / p95 ms | Indexed p50 / p95 ms |
|---|---:|---:|
| ASCII 100 KiB warm row | 11.1316 / 12.9134 | 0.0326 / 0.0357 |
| ASCII 1 MiB warm row | 127.2604 / 139.9425 | 0.0279 / 0.0564 |
| Mixed warm row | 6.0060 / 6.1830 | 0.0208 / 0.0368 |
| ASCII 100 KiB insert + row | 15.9148 / 48.0629 | 4.9019 / 5.3453 |
| ASCII 1 MiB insert + row | 171.5926 / 176.6674 | 47.2718 / 49.7710 |
| Mixed insert + row | 9.3496 / 9.6729 | 8.4702 / 8.6671 |

The 1 MiB edit path remains too costly for a claim of consistently snappy typing.
Metadata rebuild and undo copying remain synchronous. Complex huge graphemes,
maximum-size files, many visible rows, native console output, cold starts,
GUI/CSV/Markdown, cancellation and shutdown need their own final measurements.
This checkpoint does not complete the requested end-to-end lag/bug scan.

## Correctness and bounded storage

Production `TerminalRowCache` caches at most 320 lines, with a source/cell
checkpoint every 256 graphemes. Entries belong to one document identity/revision;
any edit or document change invalidates the cache. With source strictly below
16 MiB, checkpoint storage is roughly 1 MiB plus bounded entry overhead. ASCII
index construction skips glyph work only where a following non-ASCII scalar
cannot extend the ASCII grapheme. Unicode adjacency remains on the full path.

Regression tests compare indexed and full traversal for tabs, combining marks,
CJK, emoji, control labels, narrow clips, selections, resize, caret source
positions, edits, undo, document replacement and eviction beyond 320 lines.
Fourteen headless suites passed. The coordinated owned hidden-console smoke also
passed with renderer integration; smoke SHA256
`9CC55EBCD5261C810E049B2F7BC431CC5FF5F99767DB2CCA03B6EFB665BA8EB2`,
log `.build/swiftedit-resumed/console-20261001-030519.stdout.txt`. It exercised
input, save, search/replacement/undo, large-file paging and console restoration,
not visual quality or native input-to-paint timing.

## Reproduce

From the repository, with the installed SDKs and runtime DLL path configured:

```powershell
cmake --build .build/swiftedit-resumed --target swiftedit-terminal-bench --parallel 2
& ./.build/swiftedit-resumed/swiftedit-terminal-bench.exe --reference --samples .build/swiftedit-resumed/reference-samples.csv
& ./.build/swiftedit-resumed/swiftedit-terminal-bench.exe --samples .build/swiftedit-resumed/indexed-samples.csv
```

Summary CSV goes to stdout; `--samples` saves individual measurements to the
specified file. This benchmark is deliberately outside default CTest execution.
