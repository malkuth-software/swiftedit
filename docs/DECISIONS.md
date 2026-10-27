# Notepad development decision ledger

Historical baseline: the 2026-09-30 owner interview and
SWIFTEDIT_OBJECTIVES.md supersede conflicting provisional choices below.

Date: 2026-09-29. Status: provisional Windows implementation, not interview closure.

## Authority and scope

**GIVEN:** Current owner direction explicitly starts a conventional standalone
Notepad implementation in `C:/Users/Shadow/notepad`, using public GUI.Forms and
the installed shared File Manager Document Picker. It supersedes the historic
interview-first implementation gate for this development task. It does not
answer the eighteen older interview questions or declare a suite release.

**GIVEN:** One document per window; traditional menus; owned secondary dialogs;
no ribbon, tabs, IDE, browser runtime, sidebars, workers, or copied toolkit/picker
implementation. Source work stays in this repository. Provider changes belong to
the coordinated GUI.Forms and File Manager chats. No remote publication.

**OBSERVED:** Historical candidate material lives in
`C:/Users/Shadow/file_manager/text_editor/planning/`; the parent backbone is
`C:/Users/Shadow/file_manager/planning/APPLICATION_BACKBONE_AND_DOCUMENT_PICKER.md`.
ADR-013 fixes popup topology, ADR-014 lifecycle, and ADR-020 installed picker
consumption. Orchestrator APP/PCK/HLP proposals do not become implemented daemon
contracts because the editor exists.

## Provisional implementation choices

These are **CANDIDATE, implemented for development**, not architect-approved ADRs.

| Question | Current behavior | Future decision |
|---|---|---|
| Encoding | Strict UTF-8; optional UTF-8 BOM; BOM-marked UTF-16 LE/BE. Preserve source encoding/BOM. | Which legacy encodings and explicit conversion UI? |
| New document | UTF-8 without BOM; Enter inserts CRLF. | Cross-platform defaults. |
| Existing newlines | Keep every CRLF/LF/CR exactly, including mixed and missing final newline. Enter uses first observed ending. Pasted text retains its endings. | Mixed-ending edit policy and conversion commands. |
| Malformed input | Refuse malformed UTF-8/UTF-16, UTF-32 BOMs, NUL and ASCII controls except tab/CR/LF. Keep current document. Never use replacement characters. | Read-only inspection or explicit legacy decoding. |
| Size | Byte ingestion/output capped at 4 MiB; GUI.Forms editable UTF-8 capped at 1 MiB and 4096 bytes per logical line. Preflight before replacement. | Larger text control performance and user workloads. |
| Save | Write an exclusive sibling temporary, flush it, revalidate original bytes/identity, publish with Windows ReplaceFile for existing files and no-replace MoveFileEx for new files. | Full platform metadata, crash/durability contract. |
| Existing metadata | Use ReplaceFile's OS metadata merge; do not request ignore-ACL/merge-error flags. A temporary original backup is retained on replacement failure. Successful backup cleanup is best-effort. | Exhaustive ACL/ADS/compression/encryption corpus and explicit backup policy. |
| Links and privilege | Refuse hard-linked files, final reparse points, paths through reparse directories, and alternate stream paths. Read-only save fails. Never elevate. | User-friendly read-only or symlink workflows. |
| External change | Compare identity, timestamp and exact original bytes on Save; changed/deleted targets fail, keeping edits in memory. Save As is available. No automatic reload or watcher. | Reload/overwrite/merge interaction and detection cadence. |
| Save races | Keep a no-write-sharing handle open and revalidate the pathname immediately before publication. Windows permits delete-sharing so replacement is possible; a hostile concurrent rename in the final validation/publication gap is not eliminated. | Stronger filesystem reservation contract. |
| Undo | Public GUI.Forms bounded in-memory undo/redo. Save does not reset undo; New/Open does. Replace All is one replacement/undo action. No recovery session storage. | History budget, crash recovery and close promises. |
| Search | Literal forward search with one wrap; case-sensitive default; optional ASCII A-Z folding only. Replace All uses nonoverlapping matches in the original input. No regex/wildcards. | Unicode case folding, word/selection scope, whitecards grammar. |
| Display | Wrap off; status on; monospace 14. Font dialog selects toolkit font role, size, weight and italic. Status column counts Unicode scalars, not screen cells/graphemes. | Font-family picker, status semantics and durable preferences. |
| Picker | Shared installed FileManager::DocumentPickerView in owned windows. All-files default, hidden shown, session-only state; exact filename without forced extension. Explicit trusted local host authority, not a claimed daemon session. | Durable app profile, validated daemon grant and platform scope. |
| Lifecycle | One main document with owned reusable picker/find/font windows. Picker disables editor commands. New/Open/Close ask Save/Discard/Cancel; failed/cancelled Save As cancels the pending operation. | Multiwindow launch and duplicate-open policy. |

The editor does not register handlers, change OS defaults, create a settings
database, or claim Orchestrator help/settings availability. Help is local text.
Printing, page setup, character map, syntax colors, wildcards, session recovery,
Linux/macOS file adapters and release installers are deferred.

## Interview entrypoint

A future cloud interview should read this ledger and the original
`text_editor/planning/ARCHITECT_INTERVIEW.md`, then ask Round A (TE-Q01–08)
against these concrete behaviors and Round B (TE-Q09–18, including Q12A).
Record each answer as GIVEN or DECIDED only with actual owner confirmation.
Do not carry forward candidate recommendations as accepted answers. Changes to
encoding/newline/save policy must come with byte-preservation regression tests.
