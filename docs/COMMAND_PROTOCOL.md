# SwiftEdit command session, protocol 1

Run `swiftedit-cli.exe` without arguments. It reads stdin and writes stdout;
the process owns one in-memory document. Startup: `ready<TAB>SwiftEdit<TAB>1`.
There is no shell, network service, remote control, auto-commit, or background
save. Closing stdin or `quit` drops memory, including unsaved edits.

Requests are one line, fields separated by literal tabs. Escape field content
using `\\`, `\n`, `\r`, `\t`, or `\xHH`. Responses encode control and non-ASCII
bytes as `\xHH`; decode to recover the exact UTF-8/file bytes. This prevents
file content from injecting terminal control sequences or extra protocol rows.

Each request finishes with `ok<TAB>command<TAB>revision` or
`error<TAB>escaped explanation`. A command may first emit data rows. `quit`
returns `ok<TAB>quit`. Requests exceeding 32 MiB of transport text are rejected.

| Request fields | Behavior |
|---|---|
| `info` | Emits revision, byte size, dirty 0/1, read-only 0/1, path |
| `word-count` | Emits `word-count, count` for valid UTF-8 editable text; observes working text without changing revision/history; use the incremental commands for paged files |
| `word-count-start` | Replaces any previous count task for the current document. Emits `word-count-progress, 0, size` without reading source bytes, or `word-count, 0` for an empty document |
| `word-count-next, budget` | Reads at most 1..65536 bytes. Emits `word-count-progress, offset, size`, or final `word-count, count`. Rejects a changed document identity/revision, file read failure or malformed UTF-8; no partial count is published. A failed task must be restarted |
| `word-count-cancel` | Discards the count task immediately between steps. Does not change document content, selection, revision or undo history; harmless without an active task |
| `open`, path | Opens an existing file and emits stamped context for first 4096 bytes; dirty session requires save or explicit discard first |
| `discard` | Explicitly drops the current document and starts empty |
| `page`, offset, byte-budget | 1–65536 bytes; emits `page, offset, next, total, bytes` |
| `context`, byte-budget | Restarts sequential stamped context at byte zero; 1–65536 source bytes |
| `context-next`, byte-budget | Continues that bounded read; edits, save, open or discard require a fresh context read |
| `search-start`, query, start-byte, match-case, flags | Starts the shared streaming search without reading source. Query is 1..4096 UTF-8 bytes; case is 0 (ASCII fold) or 1 (exact); flags are empty for literals or one 0/1 per query grapheme, where 1 marks a one-grapheme wildcard. Emits `search-progress, 0, size` |
| `search-next`, work-budget | 1..65536 comparison operations, or one source acquisition. Emits `search-progress, offset, size`, `search-match, byte-offset, byte-length`, or `search-not-found`. No implicit wrap. Rejects stale identity/revision/size; restart a failed task |
| `search-cancel` | Drops the task between steps without changing source or revision; harmless without a task |
| `find`, exact-text | Up to 100 matches in editable document, each with bounded context; no automatic wrap or mutation |
| `preview`, before, old, after, replacement | Matches exact concatenated context; emits all bounded candidate previews |
| `commit`, token, revision | Applies one explicitly chosen current preview; subsequent attempts with stale/used tokens fail |
| `undo` / `redo` | Session history, only back to last successful save |
| `restore-opened` | Replaces working bytes with original open snapshot; doesn't write disk |
| `save` | Snapshot-checked save to current path; illegal UTF-8 bytes block it |
| `save-as`, new-path | Creates a new file, never replaces an existing target |
| `save-text-copy`, new-path | Creates sanitized copy, one space per illegal byte; leaves current document and original untouched |
| `text-copy-start`, new-path | Reserves a new-file copy task and emits progress without reading source |
| `text-copy-next`, byte-budget | Advances one read/convert/write step; budget 1..65536 |
| `text-copy-publish` | Publishes a ready copy after source validation; never overwrites |
| `text-copy-cancel` | Drops the pending/ready copy and removes its unpublished temporary |
| `csv-get`, A1 | Decoded CSV field from an editable `.csv` file |
| `csv-set`, A1, text | Previews a correctly quoted field update; requires commit |
| `csv-clear`, A1, B3 | Previews contents-only rectangular clear; requires commit |
| `csv-calculate`, expression | Returns exact result and referenced cells, no document mutation |
| `csv-value`, A1 | Returns the cell's displayed value and direct formula references; stored formula source is unchanged |
| `csv-convert-to-value`, A1 | Previews replacement of a formula by its exact value; explicit commit required; errors preserve source |
| `quit` | Ends session; unsaved state is discarded |

Preview rows contain `preview, token, revision, byte-offset, removed-byte-count,
inserted-byte-count, before-context, removed-prefix, inserted-prefix,
after-context`. Prefixes are capped at 240 bytes, surrounding context at 80.
Use pages to inspect longer source regions. Candidate count <=100 and retained
preview payload <=32 MiB. A newer preview request replaces earlier candidates
on success. A mutation, open, discard or successful save expires all candidates.
CRCR is reserved for line-marker metadata and prohibited in replacement payloads;
existing source CRCR can still be read and saved unchanged.

Files >=16 MiB are read-only via a retained file handle and bounded reads.
The older `find` command and edits remain restricted to smaller files.
`save-text-copy` supports paged files with bounded streaming conversion and
new-file publication; the command runs synchronously until completion.
The streaming `search-start/next/cancel` commands support paged files. There is
no background thread for these tasks; callers advance them cooperatively.
Read-only paging follows the opened file
handle if its directory entry is replaced. It does not silently follow a new file.

Word count uses nonempty runs separated by Unicode whitespace, including NBSP
and ideographic space. Apostrophes, hyphens and punctuation do not split a run.
Zero-width space is not a separator. This deliberately documented first policy
does not provide language-specific segmentation for scripts without spaces.
Malformed UTF-8 is refused, without sanitizing or changing the document.

Example (the separators below are actual tabs):

```text
preview				Hello\nworld
commit	1	1
info
page	0	4096
```

For a newly started process this inserts `Hello` + LF + `world`. Always take
token/revision from the actual preview response in real clients. A command
session isn't the promised full-screen nano-like terminal interface.

CSV uses comma delimiters, doubled quotes and CRLF/LF/CR row endings. Local
edits retain all unrelated source spelling, quotes and endings. Missing cells
in ragged rows are errors; clearing never manufactures or shifts cells. Math
supports strict decimal literals, parentheses, arithmetic, rectangular ranges,
comma lists and the function subset in SWIFTEDIT_OBJECTIVES.md. Stored formula cells evaluate recursively with cycle, depth and work limits;
references observe current source values.
Example `ROUND(SUM(A1:B2)/3,2)` explicitly authorizes rounding. `10/3` alone
fails. Error messages name invalid referenced cells. The reference list can
later drive native hover highlighting.


## Blank-line context metadata

`open`, `context` and `context-next` emit a `context, document-identity, revision`
row, one ordinary `page, offset, next, total, bytes` row, then zero or more
`blank, source-byte-offset, logical-line-number, escaped-marker` rows. The marker
is exactly CR CR L<number> CR CR before transport escaping. The source field is
always separate and escaped, including literal CRCR already in an opened file.
Never concatenate marker fields into a document edit. Mutation payloads containing
the reserved marker delimiter are refused intact.

Only terminated, truly empty logical lines receive markers. Spaces or tabs make
a line nonempty. CRLF counts as one ending even when split between pages; bare
CR and LF each end a line. No synthetic trailing line is added at EOF. Line
numbers are one-based observations of that exact document identity/revision,
not persistent edit addresses. Changing the source invalidates the traversal;
use `context` to restart. Exact context plus preview/commit remains the edit
addressing mechanism. Large read-only files use the same bounded reader without
loading or pre-indexing the whole file. `page` and `find` retain their original
raw-byte context response; they do not guess line numbers for arbitrary offsets.

## Cooperative source copying

`copy-start<TAB>offset<TAB>length` captures the current document identity and
revision and reserves clipboard capacity, without scanning the selection.
Offsets and lengths are unsigned 64-bit source byte values. Read-only large
files are supported. The response is `copy-progress<TAB>copied<TAB>length`.
An empty range completes immediately with `copied<TAB>0`.

`copy-next<TAB>budget` reads at most 1..65536 bytes and yields. Completion emits
`copied<TAB>length` and transfers the full result into the process clipboard.
`copy-cancel` drops pending work. Starting, cancelling or failing a copy leaves
the previous completed clipboard intact. Source edits or replacement invalidate
pending work. Clipboard bytes preserve malformed input, NUL, controls and line
endings exactly; they are not interpreted or sanitized.

`clipboard-page<TAB>offset<TAB>budget` returns
`clipboard<TAB>offset<TAB>next<TAB>total<TAB>escaped-bytes`, with budget 1..65536.
No partial pending copy is exposed. This clipboard belongs to the command
process, survives document close, and is not the operating-system clipboard.
Capacity reservation may fail before reading; no clipboard is replaced on that
failure. Each read is synchronous and can inherit storage latency, but later
steps and cancellation remain under the caller's control.


Streaming search keeps the document unchanged and returns the first complete
match whose start is at or after `start-byte`. It parses from the beginning to
preserve grapheme boundaries, so progress may initially be below that byte.
Progress is the end of the last fully compared source grapheme; source
acquisition or a partially compared grapheme may leave it unchanged. Starting
at EOF yields not-found on the next step. A client can explicitly restart at
zero to wrap; the protocol never wraps silently.

A search step either reads/parses one source context (normally 8 KiB) or charges
comparison work. Incomplete first graphemes grow context on later requests to
64 KiB; exceeding that ceiling is an explicit error. Wildcards match one source
grapheme, with an invalid UTF-8 byte treated atomically. Literal comparisons use
original bytes. Work limits are not wall-time guarantees. Cancellation,
failure, and a new successful start do not publish a partial match. Completed
results may be requested again while the document stamp remains current.

The command-process regression covers literal case folding, one-operation
steps, invalid budgets/modes/flags, no implicit wrap at EOF, cancellation,
stale document identity, and wildcard search in an actual 16 MiB paged file.
The complete local suite passed 23/23 in 9.05 s; source spelling audit passed
116 files. Source c2569a3 passed native run 36960180849 and portable-core
run 36960180791 on Windows, macOS and Linux.


Cooperative text copies use `text-copy-start/next/publish/cancel` for editable or
paged sources. Start and unfinished steps emit
`text-copy-progress<TAB>source-offset<TAB>source-size<TAB>illegal-bytes`.
The final step emits the same fields with `text-copy-ready`; the destination is
still absent. UTF-8 carry can defer up to three source bytes, so offset is source
bytes consumed, not bytes already written. Illegal-byte count includes only
resolved conversion output. `text-copy-publish` revalidates the source and emits
`text-copy-saved<TAB>source-size<TAB>illegal-bytes` after installation, then drops
the task. Final file flush/publication is synchronous. A premature publish is
refused without losing the task. Cancel is harmless when no task exists; quitting
also destroys an unpublished task. Invalid start arguments or a failed destination
reservation preserve the preceding task. A successful new start replaces it.
A changed source causes step/publication failure and temporary cleanup. Existing
names, including names created after start, are never replaced. The older
`save-text-copy` remains a synchronous convenience command using the same task.
