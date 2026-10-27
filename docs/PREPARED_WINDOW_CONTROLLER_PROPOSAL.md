# Prepared-window controller and worker proposal

Status: proposal for GUI.Forms coordinator reconciliation, 2026-10-02. This
document does not authorize a public API, worker implementation, SDK export or
SwiftEdit adoption. The coordinator accepted the three-file private input
foundation after deep-const and allocation-failure corrections. The final two
house-style corrections are applied and the standalone warnings-as-errors test
passes. Shared source ownership remains restricted to those three files.

## Required product outcome

SwiftEdit's GUI must edit the same byte-faithful Session as the terminal and CLI.
It must display malformed bytes and controls visibly and inertly, retain exact
source mappings, support source-preserving editing and multiple selections, and
show paged read-only files at or above 16 MiB. A successful render is not authority
to save, normalize, sanitize or modify bytes. Existing Session save, conflict,
undo-to-save and as-opened policies remain the source of truth.

The current GUI TextBox ceiling and byte-validity restrictions cannot become the
final document policy. Migrating only valid short documents and leaving the old
model in charge of other GUI operations would not satisfy this outcome.

## Current evidence and constraints

- Public PreparedTextSession currently owns executor-bound desire/submit/adopt,
  a revocable LayoutAuthority, one open session, coalesced payload-free wakes and
  join-based teardown. Native shaping is explicitly noninterruptible.
- PreparedTextKey covers the entire page request, source/display extents, layout
  serial, provider identity/generation, font identity/generation, context,
  typography, scale, wrapping, tabs and raster profile.
- Private PreparedWindowInput additionally binds controller instance, projection
  generation and authority. It admits complete paragraph/separator records,
  paired endpoints and exact source/display coverage. Arrays are deeply const
  after construction. The existing ledger admits one input owner.
- Its current limits include 64 KiB source coverage, 16 KiB display text, 512
  paragraph descriptors and bounded combined metadata. These are admission
  constraints, not proof that every user document can be represented.
- Session has document identity/revision and revision-checked source mutations.
  Session and its file adapters are not established as concurrently callable.
  Workers must therefore never borrow a live Session, TextBox or Control.

## Proposed ownership

| Owner | Thread | Owns | Must not own or do |
|---|---|---|---|
| SwiftEdit Session | UI executor | Source bytes/file lifetime, revisions, edits, undo/save policy | Native glyph/layout authority |
| Source projection task | UI executor, bounded scheduled steps initially | Stamped copied source context, explicit mappings, paragraph proof, endpoint records | Mutate Session or invent missing context |
| View controller | UI executor | Desired full key, source selection, viewport, controller identity, current accepted frame | Pass mutable UI state to worker |
| Provider worker | Worker | One admitted immutable input, retained font lease, private bounded layout workspace | Read Session, call controls, perform document edits, publish individual rows |
| Accepted frame | Immutable shared storage | Geometry, mapping/caret evidence, exact key and authority, charged reservations | Reinterpret a different projection or revision |
| Native paint recording | Host/render lifetime | Typed lease to accepted frame storage | Retain borrowed pointers after their owner dies |

The source producer initially runs as bounded UI work because Session/file
thread safety is not established. Moving file reads to a worker later requires
an explicit snapshot/file-lease contract; a raw Session pointer is not a shortcut.
Initial source reading and projection steps need measurement, including slow
storage. Scheduling bounded byte reads alone does not guarantee bounded I/O time.

## State and cancellation

1. A document, viewport, projection, font, scale or wrap change creates a new
   desired full key. A valid desire revokes the previous authority even for an
   equal key, matching the existing session rule. Invalid requests preserve the
   previous desired state. Checked identity exhaustion refuses new work.
2. The controller coalesces requests to the latest desired key. It keeps at most
   one pending request descriptor while an admitted input or worker job is live;
   it does not allocate one input for every wheel/key event.
3. Projection runs under a captured document stamp and controller generation.
   Each scheduled step rechecks them before and after source access. Cancellation
   releases its private buffers and publishes nothing. It must establish complete
   context and valid mapping before admission; context_required is not an empty
   layout or permission to trim an unfinished grapheme/paragraph.
4. Admission validates the entire batch and reserves input bytes before any
   controlled allocation. Busy, stale or failure leaves caller ownership intact.
   Success moves exactly one deep-const input to the worker.
5. The worker prepares the batch privately, reusing a bounded workspace between
   paragraphs. It checks revocation before each native shaping call, after each
   call, and before publishing completion. It cannot interrupt a native call;
   this limitation must remain visible in cancellation and shutdown measurements.
6. A batch succeeds or fails as a whole. No intermediate row becomes actionable
   while neighboring mappings are still being prepared. On success, one immutable
   result is placed in the existing bounded ready slot and a wake is coalesced.
7. On UI wake, adoption checks full key equality, authority, controller instance,
   projection generation and current Session stamp. A late result is discarded
   and its charges released. No matching subset of fields is sufficient.
8. The controller schedules the newest pending descriptor after capacity becomes
   available. Repeated cancellation must not starve UI dispatch, leak retained
   generations, or create a polling loop. Idle state schedules no recurring work.

Reuse the existing service/session machinery where these transitions can be
represented. Do not create one service/session per paragraph: that would defeat
the per-view budget and multiply worker/wake lifetimes. Whether the existing
single-paragraph job becomes a private tagged job or a separate private batch
job is an implementation decision requiring coordinator agreement.

## Interaction while work is pending

Source selection is owned independently of glyph storage. Keyboard commands may
operate only on validated source boundaries under the current Session stamp.
Pointer hit testing and visual caret placement require current frame authority.
When viewport or content authority changes, old geometry must not produce new
source edits or selections. A stale frame may be retained as a noninteractive
visual placeholder only if the UI clearly indicates pending content; never paint
the new caret or selection against old mappings. Prefer a current viewport's
last valid frame for mere exposure repaint while its authority remains valid.

Edits validate every range against Session and apply atomically before creating
a new projection generation. Equal-grapheme-length discontiguous edits remain
the Session policy; unequal-length sets remain copy-only. Layout cannot coerce
that policy by collapsing selections. Copy uses source ranges, not display labels.

Scrolling, dragging scrollbars and resize request a viewport; they do not read
or shape the whole document synchronously. A scrollbar needs explicit estimated
versus established extent semantics until indexing completes. Do not expose
byte-offset or jump-byte UI as a substitute for the requested navigation.

## Shutdown and failures

Closing first revokes the controller's callbacks and desired authority, cancels
producer tasks, and prevents further admission. The wake target must remain
valid through the existing begin_close boundary. The provider must be joined
before releasing worker dependencies; queued wakes must tolerate a retired
controller and never dereference it. UI widgets can detach independently of
immutable paint leases, which keep their own storage and budget charges alive.

Allocation failure, unsupported context, budget pressure and shaping failure
must preserve source, selection and save history. Distinguish temporary busy
from structural context_required and resource failure. Repeating an identical
refused request every frame is forbidden. Retrying requires a changed request,
released capacity or explicit action. Failure retirement must restore all ledger
fields while preserving unrelated font/payload reservations.

## Long paragraphs are a required unresolved boundary

A 16 KiB display batch with complete paragraph proofs cannot represent arbitrary
long lines. Raising a constant, labelling a fragment as a complete paragraph, or
falling back to source-destroying replacement is unacceptable. The GUI migration
cannot be called complete while long lines are refused solely by this prototype.

Before adopting the controller publicly, reconcile a separate long-paragraph
layout strategy: a resumable context/index pass followed by bounded viewport
fragments with certified shaping/bidi context, or another provider design that
preserves whole-paragraph semantics without unbounded UI work. It must specify
grapheme continuations, joining/ligatures, bidi isolates, tabs, atomic labels,
wrap boundaries and long clusters. Neither the existing complete-paragraph
prototype nor this document claims to implement those capabilities.

## Proposed implementation order and acceptance evidence

| Stage | Concrete evidence required before advancing |
|---|---|
| Reconcile private controller/worker contract | Coordinator accepts state transitions, job ownership, budget composition, shutdown and long-paragraph path; no public declarations yet |
| Private controller transitions | Deterministic executor/worker tests with barriers, zero sleeps: A superseded by B, cancel before/during/after native work, stale ready, repeated identical desire, busy retry, close with wake queued, exhausted identities |
| Private batch preparation | Exact paragraph/separator positioning and source hit/caret maps; empty EOF, mixed CR/LF/CRLF, Unicode, atomic control labels, malformed-byte projections; fail every controlled allocation; retained-frame charges tested |
| Native integration | Windows/macOS/Linux shaping and typed-paint lease lifetime tests, native frame capture, DPI/font/wrap changes, slow/cancelled work; no UI access from worker |
| Public contract/export | Canonical registry and headers reconciled; source/binary consumption tested; all three platform SDKs from one reviewed revision |
| SwiftEdit adoption | Remove GUI Document/TextBox content authority together; route open/edit/copy/paste/undo/save/conflicts through Session; preserve UTF-16 compatibility decision explicitly rather than silently changing decode behavior |
| Product verification | Invalid bytes/control editing, >=16 MiB paging, long lines, discontiguous selections, source/render toggles and navigation exercised; source hashes and edits verified; independent windows and save failure corpus |
| Responsiveness | Raw per-stage and end-to-end samples for open, typing, selection, scroll, resize, cancel and close; idle native CPU measured; include slow-storage and long-paragraph cases; report native-call outliers rather than declaring a budget passed by average |

Tests must cover lifetime and publication races, not just valid admission. A
passing private test does not prove a public renderer or a usable application.
The concrete next request is approval or correction of this private state and
ownership proposal, with an assigned long-paragraph design path. Implementation
scope and exact shared file ownership must be agreed before the next source edit.
