# SwiftEdit house-style correction review

Baseline: `cbfbbef`, 2026-09-30. Status: uncommitted correction for independent parent review. No feature expansion, remote publication, published-archive replacement, frozen-SDK edit, or running-application replacement.

## Source authority

`docs/PROGRAMMING_HOUSE_STYLE.md` is an exact byte copy of the supplied
`C:/Users/Shadow/Downloads/programming-house-style (2).md`.
Both SHA256 values are `2B16CEAD4EA11983009B6B7433CE12AC48701EEEE63A00DF4620EA7CD928652C`.
AGENTS.md requires reading and applying it, including semantic rules.
`.clang-format` records layout without reordering dependency-sensitive includes.

## Baseline and resulting spelling audit

`tools/Audit-HouseStyle.ps1 -Baseline cbfbbef` examines the 17 authored C++
source/header/test files, excluding string/character literals and comments.
Its baseline counts are 207 inferred-type tokens, 252 arrows (including the
trailing return), 71 anonymous executable forms, and 2 defaulted comparisons:
532 findings. The same scan of the correction reports zero findings.
It also checks coroutine and ranges/view spellings. This scanner is not a proof
of semantic compliance; the review below remains separate.

## Semantic correction

- Production GUI callbacks are named listeners with explicit weak Editor context.
  Invocation locks a temporary strong owner and checks component liveness;
  disposal revokes subscriptions/accelerators, resets continuation state, and
  retains the base Control disposal contract. Expired/disposed owners receive no
  work. Posted close operations use owner-scoped dispatcher revocation.
- Save/New/Open/Close continuations use an explicit enum and named state transition
  instead of stored anonymous functions. Window-ready, closing, picker completion,
  commands, accelerator, text/selection and button callbacks have inspectable state.
- The native test has a named synchronous-run owner with named hooks/listeners.
  Timer stop occurs before native close; after Application returns, cleanup only
  revokes and destroys timer ownership. No method uses a shut-down Window.
- EditToken and DocumentRevision are distinct value records, converted explicitly
  at the command transport boundary. Comparisons are written explicitly.
- Open allocates candidate snapshots before replacing session state. Save prepares
  path/text baseline storage before disk publication. Edit prepares its old-text
  snapshot before pruning undo. Preview prepares returned/retained candidates
  before replacing the prior preview set and publishing token advancement.
- Undo/redo slot storage is reserved and bounded; preview counts and retained text
  remain bounded. Preview sizes are checked/hoisted before its match loop, with a
  division-based budget comparison. Find results reserve the admitted count.
- Invalid-byte counting/validation no longer builds a discarded sanitized copy.
  Escape/normalization/CSV quoting capacity multiplication is checked before reserve.
  CSV rectangular clear validates the region then erases in descending source
  order without allocating copies of each decoded field. Source and replacement
  containers remain independently owned so borrowed source offsets stay valid.
- Unicode count metadata is retained on the UI owner and reused for selection
  refreshes. CSV numeric execution makes operand parsing explicit; MIN/MAX choose
  the named standard algorithm before traversal. Checked rational arithmetic
  preserves the existing exactness/rounding rules.
- Return calculations use named values or direct field/element references. Locals
  and members are initialized explicitly; representation/platform casts are visible.
  Consequential core value results are nodiscard, with explicit discard at expected
  failure-test and sanitized-copy boundaries. Native argv now has a unique RAII owner.
- Tests expose expected-failure execution explicitly instead of passing lambdas.
  The PowerShell fingerprint loop no longer uses an anonymous pipeline callback;
  digest stages are named. CLI test helpers receive Process explicitly and cleanup
  tracks whether it started. PowerShell is not evaluated under C++ spelling rules.

## Required tests and review boundaries

The correction retains document/session/CSV/CLI/editor/native suites. New checks
cover callbacks retained after disposal and destruction, pending close cancellation,
weak lifetime (no retained Editor ownership), and bounded protocol field storage.
The existing cases exercise byte fidelity, failed saves retaining edits, stale edit
previews, ambiguous edits, undo boundaries, CSV quoting/exact arithmetic, GUI menu
behavior, picker cancellation/reopening and actual native shutdown.

Final measured run and binary hashes are recorded below after packaging. The
separate review build uses frozen dbe3766 GUI and picker prefixes and must not be
mixed with the upcoming provider ABI revision. A later matching versioned SDK pair
and clean integration rebuild remain pending parent coordination.

The six product suites plus spelling audit do not simulate allocation exhaustion
at every STL allocation or every possible native reentrancy. In particular, the
existing Windows save adapter can report a failure after successful publication if
its final filesystem reread fails; this pre-existing I/O outcome remains a separate
save-contract issue. No full acceptance, cross-platform or new feature claim is made.
The owner-requested independent semantic review has not yet approved a commit.

## Exact authored change list

- `.clang-format` (new)
- `AGENTS.md`
- `docs/PROGRAMMING_HOUSE_STYLE.md` (new exact source copy)
- `docs/HOUSE_STYLE_AUDIT.md` (this review record)
- `src/cli.cpp`
- `src/csv.cpp`
- `src/csv.hpp`
- `src/document.cpp`
- `src/document.hpp`
- `src/editor.cpp`
- `src/editor.hpp`
- `src/file_windows.cpp`
- `src/main.cpp`
- `src/session.cpp`
- `src/session.hpp`
- `src/windows.cpp`
- `tests/cli_tests.ps1`
- `tests/csv_tests.cpp`
- `tests/document_tests.cpp`
- `tests/editor_tests.cpp`
- `tests/native_tests.cpp`
- `tests/session_tests.cpp`
- `tools/Audit-HouseStyle.ps1` (new)
- `tools/Build-Windows.ps1`

Ignored work/ files are temporary local edit aids, not shipped tooling or part of
this source change. No provider source, frozen SDK or Plan Paint source is changed.

## Final measured checkpoint

All six functional suites pass: document 0.13 s, session 0.27 s, CSV 0.08 s,
CLI 1.60 s, editor (including revocation) 0.25 s, native 3.14 s; total 5.49 s.
Spelling audit: zero findings. Git diff whitespace check: clean.

Review stage: C:/Users/Shadow/notepad/dist/SwiftEdit-house-style-review.
SwiftEdit.exe SHA256: 8CBC2D30745086CC73380D2036DA07A82FF449E0B3C3C7AB761E4C18026EDCC6
swiftedit-cli.exe SHA256: 230400880764D69CAFAAF9114073BC6B48B287548F1290CED29800D1C54C2018
Combined frozen SDK fingerprint: CD8F39DA910556E56B401F783727BB4370BB363EBAE2F8E60B7C850908CEB654.

The prior SwiftEdit GUI/CLI, original Notepad and DPI Notepad executable hashes
were checked and remain identical to the previous published checkpoints.

## Independent-review follow-up: temporary-file acquisition

Corrected `src/file_windows.cpp` after parent review. The deletion guard now borrows
`temporary` through a nonallocating noexcept constructor, with explicit lifetime,
no-mutation and handle-before-delete destruction-order contracts. It cannot copy a
filesystem path after CreateFile succeeds. The inner Handle then owns the acquired
handle before any write/flush operation. Sequence advancement, conversion and
filename construction are separate statements; invariant parent/process data is
prepared before reservation attempts.

Rebuilt all affected binaries in `.build/swiftedit-house-style`. Focused document,
session, CLI and editor save suites passed 4/4 in 1.32 seconds (0.29/0.19/0.73/0.10).
Spelling audit remains zero. No native rerun was necessary for this file-adapter
ownership correction. The published/archive copies remain untouched. The separate
review stage and its hashes above precede this follow-up; the current correction
is in source and build output, awaiting the parent's final SDK integration/stage.

This does not change or solve the documented post-publication reread outcome.
No corrective commit or remote publication has been made.

## Integrated review SDK checkpoint (latest)

The parent supplied a completed matching GUI/picker review SDK pair under
`C:/Users/Shadow/file_manager/.build/sdk-checkpoints/house-style-review/windows-x64/`.
This is a review snapshot from corrected dirty source, not an immutable commit
checkpoint. A fresh `.build/swiftedit-house-style-integration` compiled all consumers
against that pair with two jobs. No compatibility source fixes were required.

The final parent-reviewed save-adapter sequencing correction was then compiled:
`fail` captures GetLastError before message allocation, native inspection/seek/read/
write/flush/replace/move results are explicit typed locals, and pre-publication
readback is a named snapshot. Conditional inspection, empty-file read skipping,
zero-byte write behavior and short-circuit file-time inspection remain unchanged.
This correction does not solve or broaden the post-publication reread contract.

All six suites pass after the latest source correction: document 0.08 s, session
0.16 s, CSV 0.04 s, CLI 0.65 s, editor 0.16 s, native 1.94 s; total 3.03 s.
Spelling audit: zero across 17 C++ files. Diff whitespace check: clean.

Stage: `C:/Users/Shadow/notepad/dist/SwiftEdit-house-style-integrated-review`.

- SwiftEdit.exe SHA256: `5FE58158865627E446A9E3B945F46B50B81CA97B776E7ED53B451020B5586CEE`
- swiftedit-cli.exe SHA256: `BACB7104021C6B58CB423893B88170AEC6339420D89997B5C932D40D32269932`
- GUI application DLL SHA256: `C9E79914273562042AE38BFFDFDD296B6CA7C2BFA0F28BF4D4B2EFDA9DBB9336`
- Combined SDK fingerprint: `630D8ADF12D403CB950EBDC8A68C7604CDF0510730AC5C1CAF12248FDE8958BF`

This new stage includes the temporary-handle ownership and latest sequencing fixes.
Earlier stages, published archives, frozen dbe3766 SDK and running applications
remain preserved. The old frozen GUI DLL and prior SwiftEdit GUI stage hashes were
rechecked unchanged. No correction commit or remote publication has been made;
parent independent review remains the remaining review step.

## Final sequencing pass and shutdown handoff

Source frozen after the final parent-requested execution-order corrections.
All authored test suites were inspected. Undo/redo, GUI dispatch, fixture creation,
file reads, process waits, page reads and allocating calculation results were
separated from assertions into typed locals. Short-circuit guards and original
failure diagnostics were preserved; paired MIN/MAX and FLOOR/CEILING checks now
assert the first result before evaluating the second.

Localized production corrections separate parser token consumption, cursor/count/
depth changes, paged-file native operations, CLI input and undo/redo, editor save/
replace operations, and confirmation dialogs from predicates. Conditional dialogs
still run only under their original guards, and parser alternatives retain their
original consumption order. No new feature behavior was introduced.

The complete incremental build and all six suites passed against the preserved
house-style-review SDK pair: document 0.07 s, session 0.15 s, CSV 0.03 s, CLI 0.60 s,
editor 0.15 s, native 1.64 s; total 2.64 s. C++ spelling audit: zero findings in 17
files. git diff --check passed (only CRLF normalization advisories).

Latest integrated-review stage:
- SwiftEdit.exe SHA256: 11C972A9569D340DB6CAFE89910B526F08D0D4109CB622A0C5434208886BC303
- swiftedit-cli.exe SHA256: E14DB1A875C031DA0FAFA149B375DED7D90FDC647725A242B52E5656ED062A7A
- GUI DLL SHA256 remains C9E79914273562042AE38BFFDFDD296B6CA7C2BFA0F28BF4D4B2EFDA9DBB9336

The parent subsequently supplied a house-style-final SDK pair incorporating its
latest model change. A clean consumer build against that final pair was NOT run:
the user required immediate freeze/push before machine shutdown. This is an
explicit remaining integration check. These results are for house-style-review,
not house-style-final. The spelling scan is not a proof of complete semantic
style compliance. Parent owns final commit/push; this chat made no commit/push.
Earlier published archives remain unchanged.
