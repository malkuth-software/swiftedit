# Shared interactive terminal implementation

The interactive editor loop now lives in `src/terminal_app.hpp` and depends on
`TerminalConsole`, not Windows console records. Editing, prompts, search/replace,
wrap, paging, copying, word count and cancellation still use the same Session
operations as before. This is the integration boundary for the pending POSIX
terminal host; it does not by itself make an interactive Mac/Linux terminal
available.

The caller owns the console for the whole Terminal lifetime. The editor borrows
it synchronously; the existing wrap controller borrows that same console.
The Windows implementation owns and restores its screen/input handles as before.
The adapter translates native events into explicit key identities, modifiers,
repeat count, resize notifications and a lossless UTF-16 input stream. The shared
loop validates surrogate pairs and encodes complete scalars as UTF-8. Filename
construction declares UTF-8 explicitly on every platform. Neither source bytes
nor clipboard content are passed through the input-event encoding.

Special key identities are independent of Windows virtual-key numbers. Letter
commands use uppercase ASCII identities while typed text preserves its original
case. Other native keys remain nonzero identities so command/input state resets
retain their previous behavior. Key releases are ignored; input order and repeat
limits are unchanged.

The new headless `terminal-app` test uses an owned scripted console with the
actual editor loop. It covers non-key events, resize, supplementary Unicode,
undo/redo, grapheme navigation/delete, a non-ASCII filename, save/exit and recovery
after invalid surrogate input. It writes only its unique disposable fixture.
Local Windows Release builds and all 21 tests pass in 4.59 seconds; the spelling
audit passes for 108 source/header files. The existing native Windows console
smoke remains the OS boundary regression and requires native CI validation.

Remaining POSIX work includes raw-mode lifetime/restoration, bounded escape and
UTF-8 decoding, terminal resize and input readiness, conventional key sequences,
safe exit behavior, and owned pseudo-terminal smoke tests. It must run the shared
loop rather than a separate editor implementation. Read-only horizontal
selection/search and the other previously recorded terminal gaps also remain.

## POSIX host implementation checkpoint

The macOS/Linux executable now uses the same Terminal loop. Its host owns raw
mode, alternate-screen and bracketed-paste enablement, restores the prior modes
on normal exit and exceptions, and handles resize and termination signals with
signal-safe flags. It blocks in poll while idle. A 4 KiB input buffer amortizes
reads; no idle polling timer is installed. Escape sequences have a 50 ms
continuation wait, while split UTF-8 and bracketed paste wait for actual input.
External SIGTSTP currently exits with restoration rather than implementing
suspend/resume; keyboard Ctrl+Z remains Undo. This limitation is explicit.

The incremental decoder recognizes conventional xterm navigation, modifier and
function sequences, UTF-8 and Ctrl-letter commands. Backspace uses DEL (0x7f);
Ctrl+H invokes Replace. Ctrl+slash/underscore (0x1f) toggles the wildcard slot.
Shift+selection requires the terminal emulator to send modified key sequences.
Unsupported or oversized sequences produce a bounded error. Bracketed paste
is one exact insertion, including control bytes, never an interpreted command
stream. Its 16 MiB input bound is drained before refusal, preserving source and
preventing the remaining bytes from becoming commands. Existing editable-file
limits still apply. Pasting into an exit choice cannot accept that choice.

New decoder and shared-loop tests cover split supplementary Unicode, malformed
input, modifiers, sequence bounds, embedded command bytes, atomic paste undo/redo
and recovery after oversized paste. The owned Python pseudo-terminal smoke
checks real raw mode, Unicode/control paste, save/exit, SIGTERM cleanup and exact
terminal-mode restoration on Mac/Linux. It has bounded waits and kills/reaps only
its own child on failure. Native compilation and this PTY smoke remain pending
CI; no POSIX interactive availability or packaging is yet claimed from the
Windows headless results. The prior extraction passed native/core tests on all
three platforms (runs36951475029 and36951475071).

## Native validation and Mac package checkpoint

Native run 36952915521 compiled the POSIX host on macOS and Linux. Windows and
Linux passed; macOS failed the first PTY mode comparison. Its diagnostic showed
that only local flag 0x20000000 differed: Darwin's transient PENDIN state, set by
the kernel when restoring ICANON. All other flags, speeds and control characters
matched. Apple's `bsd/kern/tty.c` sets this state in `ttioctl` and processes it
in `ttnread`, the FIONREAD path. The test now queries the input queue before both
mode snapshots. This neither consumes nor flushes input, and the comparison
still checks every field without masking any flags. Both normal and SIGTERM
cases must pass native CI before packaging is accepted.

The Mac packager now includes `Contents/MacOS/SwiftEdit-terminal`, resolves its
dependencies, signs it, records its hash and runs the owned PTY smoke against the
packaged executable with library-path overrides removed. This addition remains
unvalidated until the new native run passes. It is not included in the existing
v0.2.6 dogfood download. Portable-core CI only covers newline, CSV and file-reader
tests; its success does not establish terminal or GUI availability.

Validation completed at `6fbdecc3f78c783ac93c695a7a80dc47d6e89f2e`: native run
36953195677 passed all three platforms, including all 30 Mac tests and the
packaged terminal PTY smoke. Portable run 36953195651 also passed all three.
The resulting verified archive is published as v0.2.7-dogfood.20261001. Normal
and interrupted exits passed the exact mode comparison after the queue query;
no production restoration change or ignored mode bits were required.

## Signal wait race correction

Source `927ffc57f053ad10ca003be2dd1b30e1921d2a57` closes the gap between checking
termination/resize flags and entering an indefinite input wait. Handled signals
are blocked during that check; pselect restores the previous mask atomically
while waiting. A scoped owner restores the caller's mask on every exit. No
periodic idle wakeup is introduced; Escape continuation retains its 50 ms bound.

The owned PTY regression now covers SIGINT, SIGTERM, SIGHUP and SIGTSTP during
idle input, an incomplete Escape sequence, split UTF-8 and incomplete bracketed
paste. Every case also sends SIGWINCH, requires bounded exit, verifies the source
is unchanged, and compares all restored terminal mode fields after the Darwin
queue-state transition described above. These 16 cases exercise cleanup states;
they do not claim exhaustive timing interleavings or suspend/resume support.

Native run 36953856671 passed Windows, macOS and Linux; portable-core run
36953856558 passed all three. Mac passed all 30 native tests and repeated the
expanded PTY smoke against the packaged terminal. The change is on master and
in that run's Mac artifact; the earlier v0.2.7 release remains unchanged.

## Large paste confirmation

Terminal-emulator paste and Ctrl+U now use the same threshold as the GUI:
more than 500,000 bytes requires an explicit typed Y before insertion. Exactly
500,000 bytes does not prompt. Read-only and oversized resulting documents are
rejected before staging. The pending choice owns the original pasted bytes and
records the document identity, revision and selection. Confirmation rechecks
those values before one insertion. N/Escape discard the pending bytes, and
pasted Y, Ctrl+Y or other commands cannot approve or bypass the choice.

The editable terminal now binds Ctrl+A to its existing Select All operation;
the read-only path already had that binding. Shared-loop regression scenarios
exercise both clipboard paths, exact threshold, cancellation, one undo returning
to a clean document, redo and exact saved bytes. Source
`7c4fedafe9f5b8dd3ede41a1d7e16ad529c415a3` passed all 22 local tests (6.11 s),
native run 36954678518 on Windows/macOS/Linux, and portable-core run 36954678433
on all three platforms. This verifies the shared-loop behavior on each platform;
physical terminal-emulator clipboard interaction still needs broader dogfooding.

## Cancellable read-only end navigation

Ctrl+End now scans to the final viewport of a read-only file. Shift+Ctrl+End
extends the source selection to EOF; Ctrl+C uses the existing cooperative copy
task. The released v0.2.8 scan reads at most 64 KiB per step, checks document identity/revision
and size, and retains at most 32768 preceding row cursors plus the final viewport.
Any key cancels between steps, and resize cancels the old-width scan. Incomplete
or stale work cannot publish a destination. Source bytes are never modified.
Up/Page Up work after completion within the retained history in v0.2.8; the
reconstruction below removes that history limit. Horizontal read-only caret
motion and large-file search remain work.

The scan shares page geometry but skips off-screen paint strings and processes
more rows per source chunk than the visible viewport. The initial control-heavy
16 MiB shared-loop regression took 12.82 s locally; eliminating repeated decoding
reduced the same test to 1.44 s. These are single local test durations, not a
native latency distribution or a general performance guarantee. The final local
suite passed all 22 tests in 6.92 s, including Shift+Ctrl+End/full-file copy,
Escape cancellation, stale/incomplete refusal, label continuation and Page Up.
Source `ef156ec2ecbef1740c5301e3ec1dd4188b2bc4cb` passed native run 36955734691
on Windows, macOS and Linux, and portable-core run 36955734686 on all three.
The verified Mac package is published as v0.2.8-dogfood.20261001.


## Smaller end-scan steps and read-boundary geometry

The next implementation uses 8 KiB ordinary source steps. When the first
complete grapheme needs more context, it doubles the read up to the existing
64 KiB ceiling (at most 120 KiB of reads in one default adaptive step).
This is a work bound, not a wall-time deadline. Cancellation remains between
steps. The scan now carries its visual column between chunks: a source read
boundary in the middle of a wrapped row must not create a new visual row.
Regression tests compare the final viewport against analytical ASCII geometry
and cover long combining graphemes, CRLF, labels and small adaptive budgets.

The benchmark compares 8 KiB and 64 KiB on the same file and process using
ABBAABBA order, four trials per budget. Both variants must produce the same
final cursor and preserve the document. Raw wall and process CPU samples are
retained separately. Local measurements are recorded in
[the paired scan evidence](performance/2026-10-01-terminal-end/paired-8k.md).
Source 40197b7 passed native run 36958176696 and portable-core run 36958176790
on Windows, macOS and Linux. This active terminal
work does not resolve the owner's reported blank GUI idle CPU usage.


## Rebuilding expired backward history

Up and Page Up now prepare a cooperative reconstruction when the preceding
rows are no longer in bounded history. The scan starts at the document's first
row and keeps only the preceding 32768 row cursors plus the requested rows.
It stops after reaching the original viewport, publishes the earlier row/page
only on completion, and replenishes history. Shift preserves the selection
anchor. Any key cancels between steps; resize cancels the old-width task.
There is no fixed distance beyond which the user must return to Ctrl+Home.
Reconstruction is linear in the source distance and may take time on very large
files; an index for faster repeated distant navigation remains future work.

Regression coverage exhausts all 32768 retained rows, rebuilds exactly one
page or row, verifies inert-label fragments sharing a source byte, and checks
cancellation leaves the viewport unchanged. The shared input-loop test opens
an actual 16 MiB read-only file, reaches EOF, exhausts the row history and
finishes Shift+Page Up cooperatively. Native validation is pending.

Local reconstruction validation: all 22 tests passed in 8.14 s; source spelling
audit passed all 113 files. Source c05031b (including reconstruction 1648b12)
passed native run 36958634172 and portable-core run 36958634241 on all three
platforms. The earlier 1648b12 runs were superseded, not successful evidence.


## Resize invalidation

Read-only viewport resizing preserves its source anchor. A width change now
invalidates retained wrapped-row history, so subsequent backward navigation
reconstructs rows at the new width. A height change invalidates page-jump
history while preserving valid row positions. Reconstruction publication
records its width, and a session reset clears all cached dimensions.

Regression tests verify a height change uses the new page size, a width change
preserves the source anchor, and Up reconstructs and retains the correct rows
at the new width. The complete local suite passed 22/22 in 7.80 s and the
source spelling audit passed 113 files. Native validation is pending.


## Streaming read-only Find

Ctrl+W Find and F3 now search paged read-only files through SessionSearch. The
query retains the existing 1..4096 UTF-8 byte limit, flagged one-grapheme
wildcards, literal punctuation and ASCII-only case folding. Source graphemes
and active pattern prefixes survive read boundaries; invalid bytes remain
unchanged and can be matched atomically by a wildcard. Metadata placeholders
cannot produce false literal matches because comparisons use original bytes.

The task owns one bounded source context and at most one candidate offset per
pattern slot. Ordinary acquisition reads/parses 8 KiB. An incomplete first
grapheme grows context on later steps up to 64 KiB; a larger grapheme produces
an explicit refusal. Each step either acquires one context or performs up to
the requested comparison work (4096 by default). These are work bounds, not
wall-time guarantees. The task validates source identity, revision and size.
It begins parsing from the start to preserve grapheme boundaries, even when
candidate matching begins later. Starting at EOF completes immediately.

The terminal keeps the visible page and selection until a complete match is
available, then highlights the exact source range and reveals it. F3 begins
at the selection end and wraps once. Any key cancels pending search; Escape
is consumed. Completed, cancelled and failed work cannot publish a partial
match. Replace remains unavailable for read-only files.

Validation includes 4096 streaming/reference comparisons with one-operation
steps, overlapping candidates, literal and wildcard Unicode/CRLF matches across
read boundaries, case handling, start offsets, invalid-byte/control distinction,
adaptive long graphemes, context refusal and stale source rejection. An actual
16 MiB file is searched through its final bytes. The shared terminal loop finds,
wraps, copies exactly the final one-byte match, and cancels a separate search.
The full local suite passed 23/23 in 9.46 s; spelling audit passed 116 files.
Native validation is pending. CLI search-start/next/cancel now expose this same task with explicit case
and wildcard flags (see COMMAND_PROTOCOL.md); large GUI integration remains
pending.

Resize source c2fb0aa passed native run 36958985720 and portable-core run
36958985706 on Windows, macOS and Linux.

Streaming terminal search source 53abb7c passed native run 36959673036 and
portable-core run 36959673110 on all three platforms. Its verified Mac package
is published as v0.2.9-dogfood.20261001. The CLI protocol follow-up (source c2569a3) passed native run 36960180849
and portable-core run 36960180791 on all three platforms.


## Horizontal read-only caret and selection

Left/Right now move between source grapheme boundaries in paged read-only
files. Shift extends selection; an unmodified arrow collapses a selection to
its corresponding edge. CRLF stays one unit, combining text stays atomic, and
control labels select their source bytes rather than label characters.
Page frames retain source ranges and actual run cell extents/first-last flags,
so a visible source caret can be drawn without inventing a position in a
partial control label. Movement reveals an off-screen destination.

Most moves use cached visible geometry. Right beyond the current frame reads
one bounded source frame. Left outside known geometry reconstructs the preceding
row cooperatively from the source start, with unchanged viewport/selection until
completion. Any key cancels between steps; resize cancels old-width work.
This fallback is linear in source distance and still needs further latency
assessment for very large files. Existing 64 KiB grapheme context limits apply.

Tests cover combining graphemes, CRLF, wrapped control labels, partial label
rows and EOF after a wrap/newline. Shared-loop tests on actual 16 MiB files
verify Shift+Right copy lengths of 3/2/1 bytes, selection collapse, Shift+Left
from off-screen EOF, and cancellation before backward publication. Native
validation for this feature is pending.

Horizontal-navigation local validation: 23/23 tests passed in 9.30 s; source
spelling audit passed all 117 files. Native validation remains pending.

Read-only caret boundary correction (2026-10-01): a wide grapheme that wraps
below the viewport no longer leaves a false caret at the preceding row's end.
Explicit newline boundary positions preserve the caret on empty lines following
a full-width row and its suppressed newline. Painted frames own this bounded
metadata; off-screen scans do not allocate it. Regression fixtures cover clipped
and visible wide glyphs and consecutive newlines after wrapping. All 23 local
headless tests passed in 9.86 seconds; spelling audit passed across 117 source
files. The preceding df74c8b checkpoint passed native run 36961373954 and portable
run 36961373917. Native validation of this correction is pending.

Read-only logical-line navigation (2026-10-01): Home and End now scan to the
current source line's start or end instead of falling through to editable-only
commands. Shift preserves the anchor. Each cooperative step reads at most 8 KiB;
Home scans backward from the caret, so it does not rebuild from the file start.
CR, LF and CRLF delimit logical lines; End excludes the separator. The caller
supplies a proven source grapheme boundary. Cancellation, resize and source
replacement do not publish a partial caret. Completion reveals the exact target.
These are logical-line commands, not visual wrapped-row Home/End commands.

Tests cover a 20,000-byte line requiring multiple steps in both directions,
CRLF, trailing empty lines, stale source rejection, actual read-only terminal
Home/End with Shift/copy, and cancelling a backward scan on a 16 MiB file.
All 23 local headless tests passed in 10.03 seconds, with zero spelling findings
across 117 source files. Native validation of this checkpoint is pending.

Follow-up review: Home/End retain the current viewport when the destination is
already visible. Differential tests compare both commands at every legal caret
in all 729 six-byte text/CR/LF arrangements against TerminalBuffer's established
logical-line navigation, using opened byte-faithful files so consecutive CRs are
not submitted through the separate replacement-payload marker policy. All cases
passed (terminal test 4.22 seconds); the terminal application test passed in
4.55 seconds, and the 117-file spelling audit remained clean. The preceding
c36b0ba caret correction passed native run 36962090425 on all three platforms
and portable run 36962090399. Home/End native validation follows this push.

Ctrl+T Save Text Copy now supports read-only paged files. Conversion/writes run
in at most 64 KiB source steps, with any-key cancellation before publication.
The output is a new file, each illegal byte becomes one space, valid UTF-8 split
across reads is preserved, and the original session is unchanged. The final OS
flush/publication is synchronous. Shared-loop tests cover a real 16 MiB success
and Escape cancellation; physical terminal and native integration QA are pending.
