# SwiftEdit terminal development build

Run `swiftedit-terminal.exe [file]` from an interactive Windows console.
`--help` prints usage without changing console modes. The executable owns a
separate console screen buffer and restores the caller's input mode and active
buffer when it exits normally or unwinds an exception.

| Input | Operation |
|---|---|
| Arrow keys, Home/End | Grapheme/line navigation; visual rows when wrapped |
| Ctrl+Home/End | Beginning/end of document |
| Page Up/Down | Move by the visible row count (visual rows when wrapped) |
| F2 | Toggle no-wrap / wrap-to-window for editable documents |
| F6 | Count document words, including paged read-only files; any key cancels pending counting |
| Shift plus navigation | Extend selection |
| Typing, Enter, Tab | Replace selection or insert text |
| Backspace/Delete | Delete one complete grapheme or selection |
| Ctrl+S | Save; unnamed documents prompt for a new filename |
| Ctrl+T | Save Text Copy to a new filename; each illegal byte becomes a space |
| Ctrl+H | Replace All: query, replacement, then Enter applies; Escape cancels preparation |
| Ctrl+W / F3 | Edit Find query / find next, wrapping at EOF |
| Ctrl+? in Find | Toggle one grapheme wildcard at the query caret, or last slot at query end |
| Ctrl+R | Open; unsaved changes must first be saved |
| Ctrl+X | Exit, with Save/Discard/Cancel when dirty |
| Ctrl+Z / Ctrl+Y | Undo/redo, bounded by last successful save |
| Ctrl+C | Copy selected source into the terminal's private clipboard |
| Ctrl+K | Cut selection, or the whole current logical line |
| Ctrl+U | Paste from that private clipboard |
| Escape in a prompt | Cancel |
| F1 | Short help |
| F5 | Insert local date and time as plain text, replacing the selection |

The terminal uses the same Session source/edit/save model as the command
interface. Existing mixed endings remain exact; Enter uses the opened file's
preferred ending. Illegal bytes in opened files remain source and are visibly
labeled. Saving while illegal bytes remain is refused. Terminal controls and
bidi controls are inert labels, not terminal commands. Printable Unicode uses
the declared Unicode 17 cell-width policy; compatibility with individual fonts
and terminal hosts still needs visual QA. Filenames/status are escaped metadata.

Files at or above 16 MiB use bounded read-only pages. Down scrolls one visual
row and Up returns one retained row. Page Down advances a frame; Page Up returns
to the preceding retained page start (or up to a screen of row history when no
page jump precedes it). Ctrl+Home returns to the first page. Each frame reads at
most 64 KiB. History retains at most 32768 row cursors and 1024 page starts;
trying to go farther back reports the limit and offers Ctrl+Home. Cursors retain
their source positions across resize; earlier rows reflect their original wrap
width. Full graphemes
are preserved across reads. Oversized graphemes that exceed the context limit
are reported unavailable, preserving the source. Shift+Up/Down and Shift+Page
Up/Down extend a source selection between visual row/page starts; Ctrl+A selects
the entire document. Selected visible glyphs are highlighted. Control-label
fragments retain their original atomic source range and never become copied
text. Ctrl+C copies exact source bytes to the private terminal clipboard using
at most 64 KiB per cooperative step. Any key cancels pending copying; Escape is
consumed, while other keys then act normally. Failed/cancelled copying preserves
the previous clipboard. Clipboard capacity is reserved before reading, and
completion transfers storage without a second full-selection copy. Individual
horizontal caret selection in read-only pages still needs implementation.

F5 shares the GUI's formatter and inserts `YYYY-MM-DD HH:MM:SS` using local
calendar time. The timestamp is ordinary source text, with no retained clock
link or automatic updates. Insertion is one undoable edit and is refused in
read-only documents through the normal editing guard.

F6 counts nonempty runs separated by Unicode whitespace without changing source,
selection or history. The event loop reads at most 64 KiB per counting step and
checks input between steps. Escape cancels; other keys cancel and then perform
their normal action. No repeated wake is scheduled after completion/cancellation.
Words and UTF-8 scalars may cross page boundaries; malformed source fails without
showing a partial count. This bounds byte work per step, not filesystem latency.
The owned-console smoke includes completed counting and read-only cancellation;
it runs only under the opt-in native-test configuration, including Windows CI.

Wrap is visual only: source spaces, tabs and line endings remain exact. Space/tab
boundaries are preferred, with whole-grapheme breaks for longer words. Up/Down
retain a desired display column across short rows; Home/End target the current
visual row. A full-width ending uses an empty continuation for the end caret.
Resize and edits invalidate geometry. The viewport retains at most 296 rows,
with sparse source checkpoints for up to 320 logical lines. Warm reverse row
location starts near the requested position. Checkpoints extend only through
the requested source position; displaying the start does not index the unused
suffix. Distant jumps and Unicode metadata preparation remain synchronous.
Edits and width changes preserve the caret's last painted row when possible;
a height reduction reveals it within the new viewport. Interruptibility needs
further work.
See performance/2026-10-01-terminal-wrap/README.md for component measurements.
F2 is session-local.

Escape can interrupt wrapped index preparation and navigation. The input loop
then shows a cancellation message without publishing a partial frame or moving
the selection. Ctrl+Home returns to the start without retrying the cancelled
end layout; subsequent editing/navigation commands retry layout. Save, Open and
Exit remain usable while layout is suspended. The probe consumes only ignored
key releases and Escape at the input queue head, preserving other input order;
it is disabled for active prompts so their Escape retains its normal meaning.
Layout checks roughly every 4096 processed source bytes plus one visual row.
One indivisible source grapheme may be larger, and first-use Unicode metadata
construction is still synchronous. This is not a strict wall-clock deadline or
complete interruptibility for all document work.

This executable is unfinished. Persistent wrap preferences,
full conflict review, external
clipboard integration and further interactive/accessibility checks remain.
Internal Copy/Cut captures an owned source clipboard, so malformed UTF-8 and
existing CRCR bytes paste back exactly. New external insertions remain validated. Warm logical-line rendering now uses bounded revision-checked sparse indexes;
see performance/2026-10-01-terminal-rows/README.md. Metadata rebuilding after edits
remains synchronous and needs further lag work. A display-layout
failure retains the input loop so Save and Exit remain available.

Save Text Copy (Ctrl+T) suggests the next dot-version filename and lets you edit
it before Enter. Escape cancels without writing. An existing destination is
refused; choose another filename instead. The separate copy includes unsaved
edits, replacing each illegal UTF-8 byte with one space while retaining valid
Unicode, controls, whitespace and line endings. It does not change the open
document's path, bytes, selection, dirty state, undo or redo history. Normal Save
still refuses illegal bytes. Read-only paged Text Copy remains unavailable;
the command reports that limit without beginning a write.

`swiftedit-terminal-smoke.exe` is an opt-in test, not a user editor. It detaches
from any caller console, allocates its own hidden console, injects paired key
press/release records only into that console, and uses disposable files. Run
only in a coordinated desktop slot with a 20-second external process deadline.
It verifies source results and mode restoration; it does not prove pixel layout,
physical keyboard behavior, accessibility, or responsiveness.


Find uses the shared flagged-grapheme search engine. Punctuation is literal until
its slot is flagged; `[wild]` in the prompt is a display label, not query syntax.
Left/Right, Home/End and Backspace/Delete edit the query at grapheme boundaries.
Ctrl+? means Ctrl+Shift+slash on the currently tested Windows virtual-key path.
Enter searches, F3 searches again, Escape or another key cancels pending work.
Search selection publication checks document identity/revision and original
selection; stale work cannot move the caret. The scan yields after 4096 work
units to service input. Snapshot preparation remains synchronous. Search currently
requires editable valid UTF-8 source; large readonly/malformed-byte search remains
unfinished. Case-insensitive matching currently folds ASCII only.


Replace All uses the same flagged query. The replacement prompt accepts an empty
string to delete all matches. Preparation is incremental and private; Enter starts
it, Escape or another key cancels while pending, and publication is one undoable
Session edit. Source/selection changes revoke the prepared result. A failure
preserves the document. The final whole-result publication remains synchronous
and must be measured. Replacement validates newly inserted UTF-8 and rejects inserted metadata
markers. Session-issued one-use preparation preserves existing source CRCR
without treating it as new metadata. Only the issuing document revision can
commit the completed operation.
