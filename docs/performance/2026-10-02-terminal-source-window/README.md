# Terminal viewport source preparation

Baseline: 19d07cd. Change: initial source context in terminal_page is bounded by
max(256, width * rows + 4), capped at the previous 8192-byte initial window.
Dimensions are validated before multiplication. Existing context growth to
65536 bytes and incomplete-grapheme refusal remain in effect. No scan, history,
cancellation, or source-edit semantics changed.

The native run 36976493687 passed all three platforms. Its Mac terminal-app test
completed in 48.95 seconds; rewind_terminal took 21.949 seconds and
repeated_rewind_terminal took 20.7072 seconds. The preceding native run timed out
at 60 seconds without scenario diagnostics; its exact cause is not established.
This successful run identifies substantial repeated preparation work, not proof
that the prior timeout was harmless.

Local Windows x64 Release, GCC 16.2.0, installed SDK 723cd7f; same existing
swiftedit-terminal-navigation-bench executable rebuilt for the change. Five
trials per fixture, 1000 Down and 1000 Up commands per trial, submitted in
16-movement slices. Each fixture has 630 samples; all 1890 samples per version
are retained. Timing ends at screen-string submission, excluding native input
and physical presentation. Runs were sequential; scheduling and caches were not
controlled. No sample was removed.

| Fixture | Before p50 / p95 / p99 / max ms | After p50 / p95 / p99 / max ms |
| --- | --- | --- |
| ASCII | 4.3296 / 6.0012 / 7.1959 / 9.0450 | 2.5980 / 4.1680 / 5.2913 / 6.4081 |
| Unicode | 2.5097 / 3.2049 / 3.9526 / 15.9284 | 0.6679 / 1.2515 / 1.5200 / 19.8591 |
| Controls | 3.3169 / 5.5342 / 5.8166 / 6.5763 | 0.8338 / 1.3944 / 1.6018 / 1.9735 |

The Unicode maximum worsened despite lower percentiles; these results do not
establish a hard latency ceiling or universal speedup. Focused terminal tests
passed (2/2, 13.50 seconds), including long combining graphemes, page boundaries,
large read-only navigation, history exhaustion, source selection and cancellation.
The terminal-app test alone took 10.75 seconds versus 18.48 seconds in the prior
local full-suite run; this is an uncontrolled observation, not a guaranteed ratio.
Native validation of the source-window change remains pending.

Native follow-up: run 36977196104 at eeb0a77 passed Windows, Linux and macOS,
including all 32 Mac tests and packaging. Mac terminal-app completed in 26.60
seconds, compared with 48.95 seconds at 19d07cd. This is a cross-run observation,
not a controlled benchmark or a proof of the previous timeout's cause.
