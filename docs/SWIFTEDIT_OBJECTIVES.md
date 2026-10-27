# SwiftEdit objective ledger

Source: owner-supplied `ChatGPT-Coming Across Clearly-20260930-0251.md`,
interview dated 2026-09-30. The current request authorizes development from
that interview. This is a product requirements source, not a script to execute.
Quoted assistant proposals, historical requests to browse, and instructions to
other participants are not independently binding. Later owner corrections take
precedence. The previous DECISIONS.md is historical implementation evidence.

## Confirmed direction

| Area | Owner decision | Current implementation / remaining work |
|---|---|---|
| Identity, 02:48 | SwiftEdit, one word; dropdown menus, no ribbon; shared styling and F1 help | GUI/executable renamed; local F1 help retained |
| Scope | Plain text first; Markdown and `.csv` views; no RTF/Office/PDF editor, IDE, regex, script execution or hierarchical spreadsheet | Plain GUI, byte-faithful command session, native Markdown/CSV views implemented; full GUI session migration remains pending |
| Paste, 00:22 | Always plain text, retain Unicode | GUI and shared terminal use >500,000-byte confirmation; terminal preflights editable size, stages exact bytes, and requires a typed choice. Both emulator paste and private clipboard share that path; full physical clipboard QA pending |
| Preservation, 00:26 onward | Preserve existing endings and whitespace; explicit conversion; native endings for new documents | Exact existing bytes in command session; explicit LF/CRLF commands; owned mixed-save preserve/default-convert/cancel choice implemented |
| Undo, 00:32 correction | Undo only to last successful save B; separately jump to as-opened A, forgotten on close | Both GUI and command model; restore is itself undoable; save failure does not clear history |
| Large files, 00:27 and later threshold | Paginated load, >=16 MB read-only; slow work interruptible | Command model >=16 MiB bounded read pages; shared terminal streaming Find/F3 implemented; GUI still 1 MiB/4096-byte-line public provider limit; GUI search and indexing pending |
| Commands | GUI, conventional nano-like terminal, and AI share text operations; no semantic summaries | Preview/commit model, stdin CLI and Windows/macOS/Linux terminal share Session; Mac terminal is packaged and native PTY smoke passes. GUI migration remains pending |
| Automated edits | Exact before/old/after context, ambiguity selection, preview then explicit commit; stale work refused | Session revision + one-use preview tokens; all edits local to this process |
| Line metadata | Blank lines alone get `CR CR L<number> CR CR`; markers never become content | Stamped sequential context emits separate escaped blank markers; source CRCR stays source and echoed mutation markers are refused |
| Binary bytes, later correction | Open as text; don't hide/collapse; save blocked while illegal bytes remain; Save Text Copy replaces each illegal byte with space | Byte-faithful command session and explicit copy implemented; GUI invalid-byte glyph/mapping view pending |
| Controls, later correction | Existing Unicode controls remain real editable content, shown visibly and inert | Session preserves controls; terminal displays inert labels; GUI inspector/pickers implemented, document-wide visible glyph view pending |
| Selection | Ctrl/Cmd+drag discontiguous selection; equal lengths parallel edits, unequal lengths copy-only | Shared editable-session selection policy implemented; provider only has one anchor/caret pair, so GUI gestures remain pending |
| Search | Flagged single-character wildcard slot; literal punctuation remains literal; no regex | Native flagged query field/right-click, grapheme Find/Replace and wrapping; normalized Ctrl+? tested, native host mapping and interruptible orchestration pending |
| Status | Grapheme character count, selection length; separate word-count tool | GUI count, selection length and column use graphemes; line lookup uses cached line starts; Document > Word Count reports document/selection; CLI shares counting core |
| Navigation | Paged navigation, draggable scrollbar; no jump-byte/jump-line product UI | Low-level CLI byte page cursor implemented; GUI document scrollbars shipped in v0.3.204 with native checks on all three platforms; GUI paged virtualization pending |
| Wrap, 02:32 | No wrap / wrap to window; intelligent breaks at spaces by default, visual only | GUI wrap and editable terminal F2 implemented. Read-only F2 connects no-wrap logical navigation, source-caret reveal, resize and cancellation. Read-only wrapping prefers spaces/tabs with matching backward reconstruction, source preservation and scripted host checks. Native tests and packaging passed on all three platforms at b32be9c and 31acdfb; physical host QA remains pending. Component mode-change measurements are recorded |
| Insert, 02:32 | Date and time as plain text | GUI Document menu/F5 and terminal F5 share a local plain timestamp formatter; terminal selection replacement is one undoable edit |
| Filename, 02:27 | Exact nonempty bracket-only first line suggests first untitled save; named files keep names | GUI Save As suggestion with Windows filename checks; no hidden document mutations |
| Copies | Dot version before extension, increment existing dot-number | Versioned filename suggestion in conflict New Copy; editable filename and collision refusal |
| External changes, 02:28–29 | Frozen views; two-stage destructive save, first over/new-copy, then editable filename/metadata + explicit warning; no compare/merge | Owned two-stage UI implemented: over/new copy, editable filename/encoding/endings, fresh review warning and explicit Save; stale/raced confirmation refused; CLI never forces overwrite |
| Markdown | Source/rendered toggle, common blocks/tables/tasks; no SVG/live HTML; inert links with URL tooltip; ruler only in rendered view | Native MD4C source/rendered view implemented; full ruler/image/layout/desktop audit remains pending |
| Print | Native OS print, plain/rendered; separate Markdown layout preview without source mutation | Session-owned immutable source capture implemented with explicit selection parts and bounded copy steps; GUI capture/projection and public native print/preview service remain pending. See PRINT_CONTRACT_REVIEW.md |
| CSV | `.csv` only, real quoting, flat table, rectangle selection; Delete clears cells without shifting | Native source/table toggle, bounded visible grid, rectangle clear, source entry, scrollbars and CLI; broad desktop QA pending |
| CSV math, 02:43–46 and 2026-10-01 corrections | Enter stores formula and displays result; menu and right-click Convert to Value; strict exact math/errors/reference hover | Formula dependency evaluation, cycles/depth/work refusal, native formula entry/results/hover highlights and both conversion menus implemented; viewport performance and desktop QA pending |
| CSV code shading, 02:36 | Triple backticks in a cell give it a gray background; no HTML formatting or hierarchy | The table uses its themed code background for cells containing triple backticks. Source characters remain visible and unchanged; selection highlighting takes precedence. Native visual QA pending |
| Lifetime | Multiple independent document windows; no tabs or persistent recovery/history | New Window launches the exact running executable independently; native launch, unsaved-parent preservation, routed Unicode clipboard and surviving-window lifetime tests pass on all three platforms. Physical keyboard and visual placement dogfood remain pending; see NEW_WINDOW.md |

## Deliberate implementation choices, not additional owner answers

* “16 MB” is currently 16 × 1024 × 1024 bytes; “500 kilobytes” is 500,000
  bytes. Exact thresholds are documented and tested, not hidden assumptions.
* Session history is bounded to 256 snapshots / approximately 32 MiB of text.
  As-opened and last-save snapshots are separate, session-only allocations.
* Command transport is versioned, tab-delimited and escaped. It is a development
  protocol, not a claim that the requested blank-line-marker presentation or
  terminal UX is complete. Byte pages may split a UTF-8 sequence; concatenate
  decoded fields before rendering the source. Offsets are transport cursors.
* Valid UTF-8 controls, including NUL, are preserved as controls. “Illegal bytes”
  means bytes that cannot participate in a valid UTF-8 scalar sequence. No
  encoding guessing or byte normalization occurs in the command model.
* The older GUI still supports BOM-marked UTF-16 and refuses malformed input.
  Migration to the byte-faithful session plus a visible-control view must happen
  together; don't silently replace invalid bytes just to fit the current widget.
* Math currently supports `+ - * /`, SUM, AVERAGE, MIN, MAX, COUNT, ABS,
  ROUND, FLOOR, CEILING, MOD. The interview's larger YES/MAYBE list is not
  blanket authorization for every assistant suggestion. COUNTA and further
  functions remain outside this first core. COUNT requires numeric operands too.
* ROUND uses half away from zero, 0–15 decimal places. Exact rational arithmetic
  has checked bounded integers; capacity overflow is an explicit error. This is
  not an arbitrary-precision engine. ROUND allows an inexact intermediate such
  as `10/3`, then deliberately rounds the final argument. No implicit rounding.
* The owner's 2026-10-01 clarification supersedes the earlier formula ambiguity:
  Enter stores a formula and displays its result. Convert to Value is available
  in the CSV menu and the cell context menu, replaces the formula with its exact
  result as one undoable edit, and refuses erroneous formulas unchanged. Formula
  dependencies evaluate against the current table; cycles, excessive dependency
  depth (64 cells) and reference work (100000) produce errors. No numeric coercion.
* The interview assistant's final RTF-detection prompt, earlier overwrite/compare
  suggestions, rejected walk-back-to-A undo, and speculative extra math functions
  were not adopted as confirmed owner requirements.

## Next development order

2026-10-03 decoded paging checkpoint: Session now supports read-only decoded
UTF-8/BOM and UTF-16 LE/BE sources when encoded or logical size reaches 16 MiB.
An incremental source owner builds scalar-boundary checkpoints using <=64 KiB
encoded reads, supports cancellation before adoption, and serves logical UTF-8
pages without retaining the whole decoded file. Adoption checks the original
Session stamp and preserves both owners on refusal. The synchronous convenience
open also uses this path; GUI opening must use incremental preparation instead.
All 30 local suites passed, followed by focused threshold/adoption checks.
Cross-platform validation is pending. This closes the shared model's paged
decoding gap, not GUI Session adoption, the new renderer, or the final lag audit.

Current checkpoint, 2026-10-03: the entries below retain historical states.
GUI Find/Replace already advances in cancellable steps and rejects changed
query/selection state (editor tests cover both). CSV evaluation now runs on an
owned cancellable worker; both Convert to Value commands use its current exact
result. Markdown parsing/display preparation also runs on an owned worker,
with indexed visible drawing and shared word metrics/URLs. Native rendering,
settled-idle and pending-work close checks pass on all three platforms; these
do not establish the full physical dogfood or responsiveness audit.

v0.3.7 is published and independently verified at
6272487e10af9cf8829c973143b30b2d6064a42e; release run 37102666871 passed all
three native jobs and publication. Download verification checked all three
archive hashes, source/SDK identities, 44 Windows files, 98 Linux files and
three macOS executables, plus all nine GitHub asset digests. See
docs/releases/v0.3.7-verification.json. Markdown layout now advances in cancellable
UI slices without repainting each pending step; full-paint readiness and routed
Escape/close during unfinished layout pass all three native platforms. This is
not a complete lag audit or a hard slice deadline. The private GUI.Forms
prepared-window input proof is checkpointed by its owner as 6ffc81c in draft
PR #14, not a public controller or SDK update. Full GUI Session/encoding
migration, paged mapped editing, multiselection and native print/Markdown preview
remain primary implementation work. SwiftEdit continues to use frozen aaca5d0.
The explicit Session decoded-open path now retains UTF-8 BOM/UTF-16 output
encoding, preserves valid controls, and keeps encoded conflict snapshots separate
from logical UTF-8 edit offsets. Default terminal/CLI raw opening remains
unchanged. SaveReview can now publish that Session model with stamped source,
reviewed destination checks, encoding/ending conversion and preserved save/undo
boundaries. These are migration foundations, not a GUI switch or paged UTF-16
implementation; see ENCODED_SESSION.md.

2026-10-01 resumed checkpoint: the separate Word Count tool is implemented with
an explicit whitespace-run policy, shared by GUI and CLI. It does not mutate
content, selection, revision or history. Language-specific segmentation remains
outside this initial policy. Paged whole-document counting is available through
the incremental command protocol and terminal F6; large GUI integration remains
pending.

The shared count now uses a constant-space streaming UTF-8 accumulator. Words
and partial scalars survive chunk boundaries; malformed, overlong, surrogate,
out-of-range and truncated sequences fail without publishing a partial count.
Tests cover every split of representative Unicode text, single-byte chunks and
failed/finished counter states. A document-stamped task reads at most 64 KiB per
step, rejects stale documents and supports immediate cancellation between steps.
The command protocol exposes start/next/cancel, tested with an actual 16 MiB
read-only file. Terminal F6 uses the same task in the cooperative input loop;
any key cancels pending counting. Large GUI counting remains pending.

1. Migrate GUI to the session command model with source/display position mapping;
   preserve toolkit ownership and negotiate virtual/paged text rendering.
2. Native multi-selection, visible controls, scrolling, wildcard slots and the
   full two-stage conflict/save UI; interruptible indexing and bounded searches.
3. Conventional terminal screen on the same session, and blank-line metadata
   presentation with collision-safe source transport.
4. Native Markdown/CSV views with inert links, strict math errors/reference hover,
   character/control dialogs, and native print/layout preview.

An objective is not complete merely because it appears in this ledger.

2026-10-02 native Find follow-up: run 36981155243 at 58c22fd observed the
correctly titled Find NSWindow created but hidden after one toolkit timer turn;
the other 31 Mac tests passed. This does not establish the underlying cause.
The next test revision checks command admission through seeded query text and
the reported show result, then observes actual AppKit visibility for at most
two seconds without forcing presentation. Timeout retains the window inventory
diagnostic. The observation interval is a readiness bound, not an input latency
measurement. Windows compilation and the 129-file spelling audit pass; native
execution remains pending. The reviewed provider title/menu patch has its final
receipt and const correction handed back for coordinator CI and SDK export.

Native follow-up c7b81ed, run 36981694678: all 32 Mac tests and Linux passed.
The Mac log records Find visible after 257.114 ms, followed by successful
synthetic AppKit wildcard toggle, matching search and toggle-back. Command
admission passed. The earlier one-timer-turn assumption was insufficient;
the observation does not establish physical keyboard latency or a production
host visibility defect. Windows was still running when recorded.

The next native editor revision adds CSV entry through Window key dispatch,
formula preservation on Enter, the declared CSV menu command and cell context
command, single-step Undo for both conversions, and formula-preserving Save.
Its disposable fixture uses a .csv extension; it adds a Mac formula-view capture
after bounded calculation readiness. This is native-window integration using
synthetic framework input and semantic command invocation, not physical mouse
or keyboard dogfood. Windows compilation and the spelling audit pass; execution
and inspection of the new capture remain pending.

Read-only source copying now has a shared cooperative task and command interface.
It reserves one clipboard buffer, reads at most 64 KiB per step, rejects stale
source identities/revisions and transfers the completed result without another
whole-selection copy. Cancellation/failure preserves the previous clipboard.
Actual 16 MiB file tests cover whole-selection copying, source-close lifetime,
malformed/control bytes, CRLF, split Unicode and undoable byte-faithful paste.
Terminal row/page selection and cooperative copying are implemented below;
horizontal read-only caret movement is now implemented below; GUI integration remains pending.

The shared Windows/macOS/Linux terminal connects paged copying to Shift+row/page navigation,
Ctrl+A whole-document selection and Ctrl+C cooperative copying. Visible runs
carry exact source ranges for highlighting; inert label fragments map back to
one source control byte. The owned-console regression checks successful copying
and cancelled whole-document copying preserving the prior clipboard. Horizontal
read-only caret/selection movement is now implemented below; GUI integration remains open.

CSV viewport computation now yields between formulas in a revocable frame queue.
Source/view changes revoke stale work; leaving table view cancels pending work.
Results appear progressively, with explicit placeholders while pending. Each
slice permits at most eight uncached formulas and checks a two-millisecond budget
between formulas. One formula is still synchronous under the existing expression,
dependency-depth and reference-work bounds; this is not full parser preemption.
See performance/2026-10-01-csv-cooperative for measurements and test scope.

CSV formula failures now display the requested warning triangle with an
exclamation mark beside #ERROR. The indicator uses native drawing primitives
and the theme's text color, so it does not require a warning glyph in the font.
Existing hover error detail and unchanged formula source remain intact. Native
visual inspection of the indicator is still pending.

Interactive SelectionSet now validates up to 1000 ordered, disjoint source
ranges against one document identity/revision. Equal source grapheme counts
permit a single atomic parallel rewrite; unequal counts remain copy-only.
Copy returns separate byte-faithful parts without inventing a join separator.
UTF-8 sequences, combining clusters and CRLF cannot be split. Illegal bytes
each count as one inert unit without changing source. Undo invalidates old
selection stamps. Generic automated range replacement retains its separate
semantics. Construction synchronously copies/indexes editable source metadata;
paged selection metadata, clipboard joining and GUI gestures remain pending.


Read-only terminal navigation now includes cancellable Ctrl+End and
Shift+Ctrl+End, with 8 KiB ordinary scan steps and adaptive context for long
graphemes. Expired backward history is reconstructed cooperatively rather than
forcing Ctrl+Home. Resizing invalidates row/page geometry according to its width
and height dependencies while preserving the source anchor. Detailed tests and
remaining performance outliers are recorded in TERMINAL_PORTABILITY.md and
performance/2026-10-01-terminal-end/paired-8k.md. These earlier changes did not complete
horizontal read-only caret navigation or GUI virtualization; horizontal movement
is implemented in the later checkpoint below. Streaming terminal
large-file Find/F3 is now implemented through SessionSearch; CLI search-start/next/cancel expose the same task;
large GUI integration remains pending (see TERMINAL_PORTABILITY.md).

Provider coordination checkpoint: public SDK723 remains pinned. The next
retained DocumentView depends on native transactional presentation integration,
exact revision/source-display mapping and retained view lifecycle, followed by
public hit-test/selection geometry and installed-consumer validation. Source-only
models or private adapters do not satisfy the GUI requirement. Print/preview
still needs an executable snapshot/projection backend. The separate blank GUI
CPU investigation has not established a fix for the owner's 7% observation.


Consumer review of the proposed retained visible-window batch requires exact
revision/projection/config identities, real empty-row baselines and EOF anchors,
CR/LF/CRLF source ownership, and lossless completion delivery under a full
owner-dispatch queue. A practical initial cap is 512 visible/overscan row
descriptors within the same aggregate budgets; this is an implementation
checkpoint, not an owner-approved document limit. Typed unavailable for long
paragraphs leaves long-line support explicitly open. CSV/formula and rendered
Markdown displays need separate derived projections where display offsets are
not raw source offsets. Mirroring is valid only for the exact same immutable
projection; independent controls must not revoke the document's layout work.
These conditions were sent to the provider coordinator; no new SDK is accepted.


Horizontal read-only Left/Right and Shift selection are now connected to source
grapheme boundaries and visible caret geometry. An off-screen backward move
reconstructs the earlier row cooperatively; source and pending selection stay
unchanged until completion. Details and validation are in TERMINAL_PORTABILITY.md.
This closes the basic terminal horizontal-motion gap; GUI integration and broader
physical terminal dogfooding remain unfinished.

Read-only terminal Home/End now use cancellable, bounded logical-line scans with
Shift selection. This removes their editable-buffer fallback; desired-column
vertical caret motion and physical terminal dogfooding remain unfinished.

Large-file Save Text Copy implementation stage (2026-10-01): TextCopyStream now
accepts up to 64 KiB per append, retains at most three trailing bytes, preserves
complete UTF-8 across chunk boundaries, and replaces each illegal source byte
with exactly one space. Finalization consumes incomplete trailing sequences;
finished streams refuse further input. Validation/allocation failure preserves
pending state. The result reports illegal-byte count for that emitted chunk.

Tests compare all 65,536 split two-byte inputs with the established text_copy
operation, plus every chunk width for valid emoji/CJK/combining text, CRLF,
surrogates, out-of-range code points and truncated sequences. Oversized-input
refusal preserves a pending emoji. All 23 local tests passed in 11.35 seconds;
spelling audit passed across 119 source files. This does not yet enable the
paged Save Text Copy command: remaining work is a sibling temporary streaming
writer with atomic no-overwrite publication, source-stamped cancellable task,
terminal/CLI wiring and cross-platform failure/cancellation tests. The existing
16 MiB snapshot writer is not suitable for arbitrarily large copies.

The streaming copy writer is now implemented for Windows and POSIX in
NewFileWriter. It owns a sibling temporary, accepts bounded output chunks, and
publishes only to an absent destination. Windows renames the owned open handle
with replacement disabled; cancellation marks that handle for deletion. POSIX
retains the parent directory descriptor, validates temporary identity, links the
new name without replacement, removes the temporary, and flushes the directory.
Write/publication failure prevents further writes or publication. Final flush is
synchronous and is not yet an end-to-end responsiveness claim. Cancellation
cleanup is best effort in destructors; explicit publication/flush failures report
whether the new name was already installed.

Windows tests published 16 MiB + 64 KiB + 4 bytes, confirmed cancellation cleanup,
existing-name refusal, a destination created after preparation remaining intact,
repeated-publication refusal and absence of temporary leftovers. Full local
suite: 23 passed in 11.80 seconds. Final Windows path-size overflow guard passed
the session test again in 0.45 seconds. House-style audit: 120 files, no spelling
findings. Native POSIX validation is pending this push. The preceding Home/End
checkpoint 828dfb7 passed native run 36962515925 and portable run 36962515920.
The paged Save Text Copy command remains disabled until the source-stamped task
and terminal/CLI orchestration are connected and verified.

Paged Save Text Copy is now connected (2026-10-01). SessionTextCopy validates
source identity/revision/size for each bounded read-convert-write step and again
before publication. A ready task does not publish automatically; dropping it
cancels its owned temporary. The terminal Ctrl+T command advances paged copies
cooperatively and checks queued input before publication. Any key cancels; Escape
is consumed. Successful output leaves the open source, selection and revision
unchanged. CLI save-text-copy uses the same task synchronously; it supports paged
files but does not yet expose cooperative protocol advancement. Final OS flush
and atomic publication are synchronous and require separate latency measurement.

Tests cover one-byte task steps, cancellation without publication, a source
change after readiness, >16 MiB Session publication, actual 16 MiB terminal save
and Escape cancellation, plus CLI SHA-256 equality and existing-target refusal.
Full local suite: 23 passed in 11.15 seconds. Expanded CLI test passed in 1.15
seconds. House-style audit: 122 source files, zero spelling findings. Native
integration validation is pending. This supersedes earlier ledger statements
that paged Save Text Copy is unavailable; large GUI integration remains pending.

Cooperative text-copy protocol is now exposed as text-copy-start/next/publish/
cancel. Preparation and publication are separate, so clients can cancel even a
fully prepared copy. Invalid starts/budgets and premature publication preserve
the existing task; stale source publication fails and cleans up. CLI tests cover
a 16 MiB copy in 256 steps with SHA-256 equality, no destination before explicit
publish, one-byte reads across an emoji and illegal/truncated UTF-8, ready-state
cancellation, failed replacement start and source identity invalidation. The CLI
integration passed in 1.44 seconds; the 122-file spelling audit passed. This closes
the earlier cooperative-protocol gap; final flush/publication remains synchronous.

Text-copy timing now has a reproducible headless benchmark and retained local
raw evidence in performance/2026-10-01-text-copy. Conversion/write steps were
sub-millisecond in this sample, but final flush/publication reached 62.3312 ms.
This is an identified remaining interactive pause, not a completed lag audit.
Next implementation must move final publication off the interactive thread
without weakening destination refusal, cancellation boundaries or ownership.
The b5d0a88 paged-copy integration passed native run 36963297860 and portable
run 36963297895; cooperative CLI changes and this benchmark await their next run.

The terminal's final text-copy publication now runs on one owned background
thread. Source validation and the final cancel opportunity precede dispatch;
after dispatch the copy finishes and reports its result. Ctrl+X defers exit,
while failure cancels the deferred exit and keeps the terminal open. Worker state
owns the prepared writer and completion synchronization, never Session/UI
borrows. Destruction joins any outstanding publication. Only active publication
uses short condition-variable waits, so no idle polling is added. CLI publication
remains synchronous to preserve command response semantics. Local dispatch and
completion timings and limitations are in performance/2026-10-01-text-copy.
Full 23-test suite passed in 18.83 seconds; the added exit-during-publication
shared-loop test passed in a final terminal-app run of 5.09 seconds. Native
validation and physical input latency remain pending.

Native CI now runs the copy benchmark after the existing search benchmark and
retains only summary, raw samples and a source/provider/platform/executable
receipt. The fixture/output files stay in the runner workspace. Failed exits and
timeouts retain an explicit receipt and still fail the build. No numerical
performance threshold is asserted. This wiring awaits its first native run.

Additional publication-owner tests verify that a worker failure is reported,
a raced destination remains unchanged, and resetting the source Session after
publication dispatch cannot invalidate the worker's prepared-file ownership.
The focused session test passed in 0.96 seconds. House-style audit passed across
123 source files, and the updated native build script parsed successfully.

Publication-thread signal review: POSIX workers now inherit a blocked mask for
SIGWINCH/SIGINT/SIGTERM/SIGHUP/SIGTSTP, with the creating thread's mask restored
immediately after thread creation. This keeps lifecycle signals on the terminal
input thread and preserves its flag-check/pselect contract. Added POSIX mask
preservation assertions and an owned PTY SIGTERM case after publication starts.
Windows session tests passed in 0.96 seconds, house-style audit passed (123
files), and the PTY script parsed. POSIX execution is pending CI, not claimed from
Windows tests. The preceding ee23416 native run 36964143324 and portable run
36964143300 both passed.

A separate remaining input issue was identified by source review: POSIX
input_ready can report raw partial input, after which read waits for a complete
UTF-8/escape/paste event. During a background publication this can delay displaying
completion until more input arrives. A bounded nonblocking decode path and native
partial-input completion tests are the next fix; no idle polling is desired.

POSIX active-work readiness now decodes at most 4096 available bytes and reports
only a complete event, lifecycle signal or closed input. Incomplete UTF-8/paste
input returns control to background work rather than entering blocking read.
Escape-sequence deadlines survive both nonblocking probes and blocking waits;
the idle terminal continues using its ordinary indefinite input wait without
polling. Owned native PTY regressions withhold the remainder of an emoji and a
bracketed paste until a 16 MiB copy reports completion, then verify exact copy
contents, exit and terminal restoration. These POSIX-only edits passed source
review, the 123-file spelling audit and Python syntax validation. Native build
and runtime verification are pending; no Windows test is claimed as proof of the
POSIX behavior. The preceding native signal/benchmark run is still active.

2026-10-02 validation update: native run 36965181568 and portable run
36965181557 passed at 9474eacb6bda1fb0c0aadf3b491324e2cb34f388. This validates
the POSIX partial-input completion regressions on macOS and Linux and supersedes
the pending-native statements above. All three packaged startup checks passed.
The public dogfood-9474eacb6bda release contains Windows x64, macOS ARM64 and
Linux x64 archives with checksums and source/SDK manifests. Anonymous downloads
of all three archives were independently checked against their SHA-256 sidecars
and matching source revisions. Master pushes now publish an immutable prerelease
only after the complete native matrix succeeds. This is release infrastructure
completion, not completion of the product or its lag audit.

Mac CPU investigation update: provider Time Profiler run 36963650387 completed,
but attempt 076b0fda4a7943d1b97fffc20b12c0d8 still reports incomplete and
comparison_accepted=false. Both recorder processes exited successfully and the
traces contain time-profile and os-signpost table schemas. Focused profiled work
completed its 120.084828291-second span; symbolized sample analysis is pending.
Both cleared-focus workloads failed hold_duration_invalid (131.926753125 seconds
profiled, 131.528282875 seconds unprofiled). They cannot support a valid comparison.
The provider owner was asked to export/analyze the existing focused trace and
investigate the cleared timing failure without weakening its acceptance gate.
No Mac CPU fix or attribution is established by this capture.

Read-only vertical navigation review found that Up/Down currently move to row
starts rather than preserving the desired display column. A bounded frame-to-source
target mapper now resolves shared wrap boundaries in the same order as caret
rendering, clamps to legal grapheme boundaries, and handles blank rows and empty
EOF after wrapped CRLF. It does not yet change interactive navigation: persistent
desired-column state and cooperative offscreen movement still need integration.
Five focused regressions passed, together with all six terminal test targets
(8.50 seconds); a final rebuilt terminal target passed in 2.28 seconds. The
123-file house-style spelling audit passed. This is navigation groundwork, not
a claim that the reported behavior is fixed.

Read-only Up/Down now uses that mapping and retains the desired display column
through short lines and viewport scrolling. Shift retains the original source
anchor. Exhausted upward history uses the existing cancellable reconstruction
task before publishing the new caret; other commands and changed viewport
dimensions reset the desired column. The row mapping indexes only candidate-row
boundaries while applying canonical wrap/newline overrides from the full frame.
Shared-loop tests verify short-line column restoration, scrolling down and back
up, and the exact 156-byte Shift selection. All 23 local tests passed in 57.07
seconds; the 123-file spelling audit passed. Native validation is pending.
The existing 32768-arrow history stress case now makes terminal-app take 49.01
seconds locally versus roughly five seconds before caret-aware movement; these
are whole-test timings, not per-key latency. Repeated-key scheduling and frame
mapping need explicit measurement in the remaining lag audit. Page Up/Down still
use their older viewport-start semantics and remain a separate navigation task.

Visible-page decoding now starts with 8 KiB and doubles its source context only
when needed to fill the requested viewport or complete a grapheme. The maximum
individual window remains 64 KiB; retries total at most 120 KiB. Previously each
arrow-triggered rebuild decoded 64 KiB regardless of the visible text required.
The unchanged terminal-app workload fell from 49.01 seconds at 6cd2c9c to 10.08
seconds in the first local adaptive-window run and 10.64 seconds in the final
full-suite run. These are sequential, uncontrolled whole-test wall times, not
paired per-key latency samples or a platform-wide performance guarantee.
All 23 local tests passed in 18.40 seconds. Regression coverage includes a
12001-byte combining grapheme requiring growth beyond 8 KiB, subsequent visible
rows, and a large viewport requiring the full 64 KiB with an unfinished trailing
grapheme. House-style audit passed (123 files). Native validation remains pending;
the prior vertical-navigation native run 36966475497 is still in progress.

Page Up/Down now retain the visible caret row and desired column across page
changes, clamping to an available legal source boundary on a shorter final page.
Shift preserves the source anchor, including cooperative reconstruction of
expired backward history. The shared-loop round-trip regression verifies the
rendered cursor and exact 140-byte selection. All 23 local tests passed in 17.48
seconds; house-style audit passed (123 files).

Native run 36966475497 at the earlier 6cd2c9c checkpoint failed its Mac terminal-app
test at the unchanged 60-second timeout; the other 30 Mac tests passed. That
checkpoint predates adaptive visible-page decoding. Linux passed, while Windows
was still running when this result was inspected. The adaptive change must pass
a fresh Mac run before the timeout regression can be considered resolved.

Read-only navigation key repeats now run in slices of at most 16 movements,
with redraw and Escape cancellation between slices. Unexecuted repetitions stay
owned by the main loop and wait for cooperative row/line reconstruction; they
no longer enter key dispatch and cancel their own outstanding task. A genuine
new event that cancels reconstruction also supersedes its queued repetitions.
No idle timer or thread was introduced. Tests verify all 23 requested movements,
the unchanged source selection, Escape stopping a 1000-repeat event after the
first 16 movements, and reconstruction inside a repeated Up event. The expanded
terminal-app test passed in 17.49 seconds. The other 22 tests passed in the
preceding full-suite run; final test-only edits split the two distinct history
reconstruction cases. House-style audit passed across 123 files. Native validation
is pending; these event-count bounds do not establish a wall-time latency bound.

Native validation update: 11d8a9ca747a4681ed31a57828a41eb2629e5ac7 passed all
three native jobs in run 36966883286 and portable run 36966883356. Mac terminal-app
completed in 27.60 seconds under the unchanged 60-second timeout; all 31 Mac
tests passed in 65.42 seconds total. This clears the earlier CI timeout for the
adaptive decoding/page-navigation checkpoint, not the separate idle-CPU issue.
The automated dogfood-11d8a9ca747a release is public with all nine expected
archives/checksums/manifests. Repeat slicing at 970cd73 was pushed afterward
and requires its own native validation.

Provider ownership check: the File Manager coordinator's latest report identifies
the multiline-text chat as idle and the multi-paragraph document view as unfinished.
SwiftEdit requested an explicit owner and public-capability checkpoint for its
remaining document-view/mapping, multi-selection and print dependencies. These
must not be described as actively progressing without fresh evidence. The frozen
SDK remains unchanged and no provider source was modified.

DisplayPage source/display selections now use complete source graphemes rather
than individual Unicode scalars. Combining accents and joined emoji cannot be
split by either mapping direction. Byte-preserving segmentation metadata treats
each illegal byte as an independent control sentinel; displayed labels and source
bytes remain unchanged. Regressions cover combining text, joined emoji, illegal
bytes adjacent to combining marks, literal label-looking source, CRLF and maximum
expanded pages. All 23 local tests passed in 31.44 seconds; spelling audit passed
(124 files). This does not supply missing page-boundary context or claim GUI
DocumentView integration.

The coordinator subsequently transferred bounded ownership of the provider
prepared-window input/identity/admission prototype and focused tests to this
SwiftEdit chat. Initial edits must be new files only, with exact paths reported
before editing. Existing host/rendering/A2-worker files, canonical contracts,
public SDK availability, root CMake and workflows remain coordinator-owned.
Provider commits/pushes are prohibited for this assignment; source-review and
test evidence go back for independent integration. No workers or subagents.
This supersedes the idle-prototype ownership statement, not the capability gaps.

## Owner dogfood follow-up: GUI polish and versioned releases

The owner reports that the Mac blank-window CPU issue appears cured. This is
owner-observed improvement, not a new controlled measurement. Remaining GUI
feedback: visible Markdown mode state, inconsistent passive menu highlighting,
status-bar depth, and moving the filename from the client row into the native
window title.

Markdown, CSV table, Word Wrap and Status Bar commands previously set checked
state on ordinary command items, whose renderer omits the checkmark. They now
use check items. A headless regression exercises Markdown on/off and verifies
all four item kinds. The status strip now has a theme-colored face and two-tone
top edge, with no periodic work. The initial full suite passed all 23 tests in
27.84 seconds. Native visual validation remains pending.

The installed SDK exposes creation titles but no dynamic window-title update.
The provider coordinator has been asked to own that cross-platform capability
and the pointer/keyboard menu-focus investigation. The filename row is retained
until its replacement can preserve document identity and dirty-state visibility.

Release policy now follows versioned application tags: master builds do not
publish commit-named releases. A matching v0.3.0 tag builds Windows x64, macOS
ARM64 and Linux x64, then publishes one immutable release only after every
platform passes. Archive hashes and same-source manifests remain mandatory;
historical releases remain intact. Expanded product implementation is unfinished.

GUI polish validation follow-up: the native smoke now opens the production
Markdown view and View menu, then returns to source view. Mac CI captures both
the rendered menu and source/status surface using the existing owned AppKit
view-capture helper. These are view redraws, not compositor screenshots or
automated visual assertions. The extended native target builds on Windows;
native execution and Mac capture inspection remain pending.

Provider source inspection found that ContextMenu::Impl::show always passes
first_focusable(0) into begin_focus_scope, including mouse opening. MenuRow paints
focused rows with the same highlight as hover. All-disabled menus have no such
row. This explains a source-level inconsistency and has been sent to the provider
owner; pointer-versus-keyboard presentation still needs a provider correction.

Validation update: versioned release v0.3.0 (e33262a498fc) passed all three native
jobs and publication in run 36969276926. Its public, non-draft, non-prerelease
download set contains Windows/macOS/Linux archives plus three checksums and three
manifests. Follow-up ea3a73c passed all three native jobs in run 36969494053 and
correctly skipped publication on master. The downloaded Mac AppKit captures
visually confirm Markdown/Status Bar checkmarks and the status-strip edge. They
also show the passive focused row and duplicate filename row still present.

The assigned private provider prototype is now implemented in the three announced
new files and handed back for independent review. It validates aggregate inputs,
paragraph/separator coverage, explicit source/display identity and ownership,
using the existing input reservation ledger. Standalone GCC builds with
-Wall -Wextra -Werror and focused tests pass. Mixed endings, empty EOF, 512 empty
rows, budgets, UTF-8 boundaries, projected separator labels versus literal
lookalikes, late refusal, busy/closing, copying and charge retirement are covered.
Provider integration, allocation-failure injection, broader identity permutations,
controller/worker/rendering/host behavior and native latency remain unverified.
No provider commit, public SDK change, DocumentView or print availability is claimed.

Rendered Markdown navigation follow-up: the preview now handles arrow keys,
Page Up/Down and Home/End (including Ctrl/Cmd document endpoints). Clicks restore
preview focus without activating links. Navigation is refused while layout is
pending/failed and fitting content cannot enter the scrollbar's artificial range.
Horizontal scrolling now moves the ruler's coordinates with the document.
Headless editor/views tests passed in 1.97 seconds; navigation regressions verify
clamped endpoints, stale-hover clearing, fitting content, horizontal code scrolling,
ruler alignment and unchanged text-measurement count on warm navigation. Build
and house-style spelling audit passed (124 files). These checks prove layout
reuse, not an end-to-end physical keyboard latency bound; native validation is
pending for this checkpoint.

CSV navigation scan: Page Up/Down now move by the visible row count, Home/End
select the first/last existing cell in a row, and Ctrl/Cmd Home/End reach the
table endpoints. Shift preserves the rectangle anchor and ragged target rows
clamp to real cells. Zero vertical wheel movement no longer spuriously scrolls
upward. Focused tests verify these cases, including exact copied selection bytes.
The F1 help describes both Markdown and CSV navigation. About now uses the CMake
project version rather than the stale hardcoded 0.2 label. Build and editor/views
tests passed (2.05 seconds); house-style audit passed (124 files). Native validation
of this checkpoint is pending. The preceding Markdown native run 36970454027 has
passed Linux; Windows/macOS were still running at this inspection.

Native run 36970454027 for Markdown navigation commit 4112c6d subsequently passed
all three platforms. The earlier ea3a73c navigation receipts and all 5,670 raw
samples are preserved under performance/2026-10-02-cross-platform-navigation.
Observed p99 repeated-movement slices were below 8.6 ms on Mac, 7.2 ms on Windows
and 3.5 ms on Linux. Mac worst case was 41.014 ms; no outlier was removed and no
hard deadline is inferred. This headless-loop evidence excludes native input and
physical presentation and does not close the final GUI responsiveness audit.

Mac packaging parity: the v0.3.1 candidate bundles swiftedit-cli alongside the
GUI and terminal, includes COMMAND_PROTOCOL.md in application resources, resolves
its runtime dependencies, signs it, and requires packaged info/quit smoke success
without SDK library overrides. The manifest records its executable hash and
validation scope. v0.3.0 remains immutable and has no bundled Mac CLI. Packaging
tool syntax checks passed locally; actual Mach-O fixup/signing and packaged launch
require the v0.3.1 native tag build. This checkpoint also includes the Markdown/CSV
navigation improvements and project-version About text.

Validation update: v0.3.1 at 1a0e1e9d1bd1 passed all three native platform jobs
and release publication in run 36971134655. The public, non-prerelease GitHub
release contains all three archives, their checksums and platform manifests
(nine assets). Mac CLI bundle fixup, signing and packaged launch passed. The
preceding master run 36970933135 also passed; master now includes the release
commit. This does not close the final physical GUI responsiveness audit.

GUI polish provider checkpoint: the coordinator granted bounded title/menu
ownership. Uncommitted GUI.Forms changes implement validated dynamic native
titles on Windows/macOS/Linux and explicit pointer-versus-keyboard menu opening.
Pointer opening focuses the accessible menu container without selecting a row;
keyboard opening retains first-enabled-row focus. Focused application contract
and menu tests passed 2/2 (0.06 seconds); Windows native test executable compiled
without a local GUI launch. The 19-file patch and proposed public contract were
handed to the coordinator for independent review and native platform validation.
It is not an exported SDK and is not included in v0.3.1. SwiftEdit's duplicate
filename row remains until adoption of a reviewed public SDK. Markdown check
state and recessed status-bar painting are already included in v0.3.1.

Wildcard bug scan: TerminalQuery formerly preserved flags by equal text prefix
and suffix. Inserting an identical character before a flagged occurrence could
transfer its flag to the insertion; deleting that occurrence could leave a flag
on its neighbor. Preservation now maps unchanged source ranges through the exact
edit and requires the original whole grapheme to survive. Combining-character
edits that merge a grapheme clear its prior flag. Terminal regression suite passed
(4.52 seconds); house-style spelling audit passed all 124 source files. Source
review confirmed private preparation before publication and bounded query size.
The GUI QueryField has the same old matching heuristic and remains an open bug:
its string-only notification needs edit provenance and query undo reconciliation.
This is a terminal correction, not a claim that GUI wildcard editing is complete.

GUI wildcard follow-through: QueryField now maps flags through the routed edit's
selection and operation, including duplicate insertion, forward/backward deletion
and clipboard replacement. It owns a bounded 64-entry history of text, flags and
selection; Ctrl/Command undo and redo restore them together, and wildcard toggles
are undoable. Programmatic set_text starts a literal query and clears query
history. Combining edits clear flags on changed graphemes. The 4096 UTF-8 byte
budget now also applies to interactive insertion, preserving the prior query and
history on refusal. Headless tests cover routed typing, duplicate deletion,
clipboard insertion/identical replacement, combining edits, undo/redo and byte
budget refusal. The full local run passed 22/23; the remaining view test used
set_text to simulate typing and was corrected to route actual text input.
Editor and views then passed together (2.34 seconds), covering all 23 suites
across these checks. No native GUI launch or physical responsiveness claim.
House-style audit passed 124 files; source review checked bounded history,
owned snapshots, literal reset semantics and rollback of failed routed edits.

Query edit responsiveness: added a reproducible headless routed-input benchmark
with 4094/4095-byte ASCII, Unicode and combining queries, every grapheme flagged.
After 20 warmup pairs per fixture, 200 insert/undo pairs per fixture produced
1200 checked raw samples. Windows x64 Release observed worst insertion 3.2811 ms
and worst undo 1.7033 ms; details and all percentiles are preserved under
performance/2026-10-02-query-edit. Timing includes the query operation and history,
but excludes painting, native input and physical presentation. No hard latency
guarantee or final GUI performance signoff follows from this component run.

Query paint follow-through removes repeated measurement of identical displayed
labels within a rebuild and unused literal display preparation for flagged slots.
The metrics cache is local to the current painter/font/rebuild. Focused editor
and view checks passed; repeated bullets measure once, visible submissions stay
bounded by the viewport, and caret scrolling reuses prepared geometry. Before/
after paint-preparation samples are recorded beside the query-edit measurements.
Native CI run 36973356334 for 166b912 passed all three platforms; the paint change
still requires its own native build validation.

Shared-session GUI migration producer checkpoint: consumer-owned DocumentProjection
now prepares stamped Session source for the installed public D1 DocumentPage API,
with bounded read steps, complete line-boundary checks, exact identity/atomic
source mappings, cancellation and stale/single-use publication checks. The new
headless test publishes a real payload into the installed DocumentViewState and
checks malformed/control bytes, CRLF, read-boundary Unicode, cancellation and
16 MiB read-only paging. It passes (0.22 seconds); house-style audit passes 128
files. This is producer integration only; no private provider API or current GUI
content authority was substituted. See DOCUMENT_VIEW_FIXTURES.md for remaining
rendering, timing and long-context limitations. Native validation is pending.

Producer timing checkpoint: 990 raw construction/read/projection/publication
samples are preserved in performance/2026-10-02-document-projection for full
64 KiB ASCII, NUL and malformed-byte pages. Worst final projection in the recorded
Windows Release run was 7.1728 ms; worst resident-source read was 0.0947 ms.
This includes label/map construction and temporary cleanup, but not slow I/O,
native shaping or presentation. The component result is not a completed GUI
latency audit, and D1 admission does not establish the private renderer's tighter
display/metadata admission. The coordinator confirmed controller reconciliation
remains queued; shared worker/controller/API expansion is not yet assigned.

Native validation follow-up: run 36975543860 at 452e0b0 passed Windows and
Linux. macOS compiled and passed 31 of 32 tests, including document-projection,
the native GUI checks and idle probe, but terminal-app exceeded its 60-second
limit. This is an unresolved failure, not a successful native validation.
The terminal integration test now flushes scenario names and elapsed times to
preserve the active scenario when CI terminates it. Local Windows execution
passed; the two history-reconstruction scenarios took 9.166 and 8.231 seconds.
No timeout or assertion was relaxed. The next native run must establish whether
the Mac failure is aggregate runtime or a stalled scenario before remediation.

Native run 36976493687 at 19d07cd passed Windows, Linux and macOS packaging;
macOS passed all 32 tests. Its terminal-app timing was 48.95 seconds, including
42.6562 seconds in two long rewind scenarios. The previous timeout's exact cause
remains unknown. A measured follow-up now sizes initial terminal source context
to the viewport instead of always segmenting 8 KiB per scroll. Existing bounded
growth and complete-grapheme checks remain. Focused tests pass; 3780 raw before/
after samples and limitations are recorded in performance/2026-10-02-terminal-source-window.
Native validation of this optimization is pending.

Markdown layout audit: a quoted table could exceed the 250000-run retained
layout limit because final table borders and quote decorations were appended
after the text-only check. A 125000-word quoted-table regression failed against
the previous implementation and passes with checks before text shaping, border
append and quote append. Refusal leaves the source unchanged and a later normal
source renders successfully. View tests pass (0.18 seconds including CTest
execution); house-style audit passes all 129 files. This closes the decoration
accounting bug, not the outstanding full Markdown desktop/layout/print audit.

Native run 36977196104 at eeb0a77 passed all three platforms and packaging.
Mac terminal-app completed in 26.60 seconds versus 48.95 seconds in the preceding
run; this supports reduced repeated work but is not a controlled speed ratio.
The packaged Mac idle probe measured 1.297% of one core over ten quiet seconds;
focus/occlusion were uncontrolled and there is no performance pass threshold.

Dynamic GUI labels (filename, status, Find diagnostics and conflict warnings)
now disable toolkit mnemonic parsing so literal ampersands are not consumed as
keyboard markers. Menus retain their intentional mnemonic behavior. Editor tests
pass (2.04 seconds including CTest); house-style audit remains clean at 129 files.
Native validation of these label settings and the Markdown bounds fix is pending.

Release v0.3.2 at 2000b72 is public with all nine Windows/Linux/macOS assets;
release workflow 36977875265 passed all native jobs and publication. Master now
matches the release source. Full local headless validation passed 24/24 in
18.83 seconds. This release does not close the remaining feature objective.

Native query validation now includes a macOS-owned-window AppKit Control+Shift+/
event pair, asserting exact wildcard-slot toggling, unchanged query text, a source
match, and toggle-off. It sends through NSWindow's event path, not a normalized
GUI.Forms test event or global keyboard injection. Physical keyboard layouts and
OS event delivery remain separate dogfood evidence. The Windows native test
executable compiles; execution of this new Mac-only path is pending native CI.

AppKit shortcut validation follow-up: native run 36978612315 compiled the Mac
helper but failed before keyboard dispatch because the owned Find window was
not yet visible. The model smoke previously used controls in the same timer
callback that queued show(). The test now returns to the native event loop after
opening Find and delivers input on the next timer callback. The Windows test
executable compiles and the style audit passes; the Mac shortcut remains
unverified until the corrected test passes. No released application code changed.
