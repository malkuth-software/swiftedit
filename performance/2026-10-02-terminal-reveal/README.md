# No-wrap reveal measurements

Windows x64 Release, local MinGW build, 2026-10-02. The implementation and
`tests/terminal_reveal_bench.cpp` are committed with this receipt. Base application
source before reuse: 901152d. Benchmark compares the same executable with and
without passing the completed previous viewport to the next reveal.

Each fixture is at least 16 MiB and opens through the real read-only Session.
Fixtures are repeated 79-character ASCII lines, one long ASCII line, and one
long NUL-control line. Two trials per fixture start at byte 0 and byte 8388608;
each then reveals the next byte. Every result verifies the requested source
caret against the completed viewport's mapping and refuses changed source.

| Adjacent caret after byte 8388608 | Fresh preparation, ms | Reused preparation, ms |
|---|---:|---:|
| Long ASCII line | 686.117–736.581 | 0.2485–0.4012 |
| Long control line | 743.935–746.442 | 0.2117–0.2357 |

These are two observed values per mode, not statistical guarantees. Runs were
sequential (fresh before reused), without cache flushing or machine isolation.
The improvement comes from avoiding repeated line discovery and prefix
segmentation: completed same-source row/grapheme mappings seed replacement
preparation. Reuse copies bounded retained metadata and does not borrow the
previous owner. Stale identities/revisions are rejected. Tests compare fresh
and reused output, including clipped tabs, controls, wide/combining characters,
backward movement and destruction of the old owner before completion.

Initial distant reveals remain slow: long-line initial preparation in the
reused run took approximately 741–814 ms at byte 8388608. Reuse does not improve
an initial jump without a known mapping. The worst individual preparation step
in that run was 1.2206 ms; the fresh run's worst was 1.5356 ms. These observed
step times support cooperative cancellation, not a hard latency bound.

`total_work_ms` includes task construction and the sum of timed steps. It
excludes Session open, fixture creation, recording samples, final task-owner
destruction, terminal screen-string assembly, OS input and physical presentation.
The benchmark uses an 80-column, 20-row viewport; maximum supported dimensions
and cold/slow storage require separate measurement. This is not final GUI or
terminal responsiveness signoff.

Validation after reuse: full Windows Release build, 26/26 headless tests
(19.55 seconds), and the 138-file spelling audit passed. Native validation of
this optimization is pending; the preceding F2 integration passed all platforms
in run 36989937839.

Reproduce from a Release build:

```
swiftedit-terminal-reveal-bench NEW_FRESH_DIRECTORY --fresh-next
swiftedit-terminal-reveal-bench NEW_REUSED_DIRECTORY
```

The checked-in summaries retain every case. Gzip-compressed CSV files retain
90,444 fresh and 45,236 reused raw step measurements with fixture, caret, trial,
step index and milliseconds. Fixtures are deterministic and are not checked in.

## Off-screen ASCII prefix follow-up

After the reuse checkpoint at 0da6631, the width-only path skips proven
standalone ASCII units outside the visible slice without creating glyph strings
or segmenting an entire ASCII prefix. It retains the final ASCII byte before
non-ASCII or an unfinished chunk, allowing combining/variation suffixes to reach
normal grapheme segmentation. Visible content still follows the normal renderer.
The width-only policy is checked against rendered output for all 128 ASCII
bytes at all four tab-stop positions (and a repeated stop cycle).

`ascii-prefix-summary.csv` and `ascii-prefix-samples.csv.gz` preserve a subsequent
run of the same benchmark and fixtures, with 45,236 raw steps. Initial distant
ASCII reveals measured 95.569–95.605 ms, versus 740.959–813.822 ms at the reuse
checkpoint; control-line reveals measured 92.694–94.116 ms, versus
751.999–753.039 ms. The worst individual step was 0.7038 ms. These sequential
local observations are not a hard guarantee or controlled cache-isolated ratio.

The optimization does not accelerate arbitrary Unicode clusters, cold storage,
native presentation or the remaining full GUI migration. Initial view discovery
still reads long logical lines to find their ends. The cancellation architecture
and source/grapheme refusal bounds remain unchanged. A regression specifically
checks an ASCII base plus combining mark split across the 8192-byte read edge,
including refusal of a source caret inside that cluster.

The full local Release build and 26/26 headless tests passed after this follow-up
(19.21 seconds), with the 138-file spelling audit clean. Native validation of
this latest change is pending.
