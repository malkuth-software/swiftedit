# Private prepared-window batch proposal

Status: read-only proposal requested by the GUI.Forms coordinator. No shared
source, CMake, branch, SDK, or public API changes were made. This is not evidence
of a working controller, renderer, print service, or SwiftEdit GUI migration.

## Exact review scope

The input proof is File Manager PR 14 at
`6ffc81c012fca703cd557a0fc3d76006fb506e1c`, fetched by the GitHub contents API.
Reviewed both `prepared_window_input.hpp` and `.cpp` in
`gui_forms/src/core/text/prepared_window/`. The review also read the current
prepared-text types/service/layout headers, private storage and reservation
implementation, shaping geometry, service submit/worker/adoption implementation,
recorded prepared-text commands, ObjectView detail painting, and File Manager
preview setup/adoption. Shared checkout files were read in place; no checkout
operation was performed. The relevant source fingerprints are recorded below.

Applied `planning/PROGRAMMING_HOUSE_STYLE.md`: typed records, explicit authority
and ownership, named operations, no anonymous callbacks or universal payloads,
checked bounds, immutable published data, and failure preserving caller state.
Existing implementation details are evidence to review, not new owner policy.

## Findings that constrain admission

PR 14 validates one contiguous mapped source window, up to 512 complete
paragraphs, 64 KiB permitted source, 16 KiB display, and combined metadata record
and byte limits. It checks CR/LF/CRLF coverage and certified separator endpoints,
including atomic display labels. It does not shape, establish a live controller,
or make unrelated filenames into a certified contiguous source document.

Its key includes controller instance, projection generation, layout authority,
and the complete PreparedTextKey. The current A2 key validator only admits
no-wrap (`wrap_width == 0`), its fixed tab policy, and the existing raster/font
profile. Paragraph content rejects actual tabs and additional separators;
producer-generated inert labels are ordinary display text. Do not silently
relax these checks in a batch implementation.

Current A2 submit reserves one full 8 MiB payload before moving the input. It
then releases the input-slot reservation without releasing the input bytes.
Three payload generations can remain retained. Recorded display commands keep
a typed shared storage owner and an authority value, so replacing a wrapper
does not retire the recorded allocation. Cancellation and memory retirement are
different operations.

**PR 14 cannot yet perform the same reservation transfer.** Its output is
`unique_ptr<const PreparedWindowInput>` and its reservation is a member of that
const object. It also does not expose the reservation's ledger identity for
cross-service admission checks. Calling `release()` requires mutation. Keeping
that input charge until every rendered result dies would prevent admission of
the next input while the current view remains visible. A const cast is not a
solution.

## Smallest proposed next implementation slice

First implement private aggregate ownership/admission/retirement, with no native
shaping or display-command changes. Establish these semantics before attaching
a worker or claiming useful rendering.

Split private input into a movable ownership record and immutable data. The
ownership record holds the input reservation, originating ledger identity, and
`unique_ptr<const PreparedWindowInputData>`. The data holds the existing key,
const arrays and active counts. Observers receive only a const data reference.
The admission operation can move the owner and transfer its charge; it cannot
mutate admitted text, mappings, endpoints or paragraph records. Update the
private PR 14 constructor/tests explicitly rather than introducing a second
uncertified input format or a hidden extra input copy.

Proposed new private files, relative to the File Manager repository:

- `gui_forms/src/core/text/prepared_window/prepared_window_batch.hpp`
- `gui_forms/src/core/text/prepared_window/prepared_window_batch.cpp`
- `gui_forms/tests/prepared_window_batch_tests.cpp`

Required narrow amendments, needing coordinator approval along with those files:

- The two PR 14 input files and their ownership tests, for the data/owner split.
- No changes to public headers, hosts, raster/compositor, recorded commands,
  current service session behavior, or SDK export in this first slice.
- CMake registration is a separate coordinator-owned step; none is authorized
  by this proposal.

Use a named private admission operation with explicit expected key, authority
state and ledger, a mutable unique input owner, and an empty unique job output.
An occupied output returns busy. Wrong ledger, invalid key, stale authority,
closing, exhausted budget and allocation failure preserve both caller input
and output. Check the full window key, not just the text revision.

Admission sequence:

1. Validate ownership, executor and exact desired window key under the authority
   protocol. Reject before allocation when possible.
2. Reserve one 8 MiB payload generation from the originating ledger.
3. Allocate the job owner and bounded row descriptors while the caller still
   owns its input. Charge descriptors, owner storage and input bytes against the
   same aggregate payload ceiling.
4. Recheck authority at the commit boundary. Move input only after all fallible
   admission work succeeds, release its input-slot charge, then publish the job.

The private aggregate job owns its reservation, immutable input, retained font
bank, full window key, authority state and row storage. No row independently
reserves an 8 MiB payload, owns a session/thread/font bank, or copies the input
paragraph bytes. An empty EOF paragraph remains a logical row with metrics and
zero glyphs; consumed separators do not become glyph rows.

Do not expose a successful renderable result in this first storage-only slice.
Test-only row data may exercise accounting and retirement, but must not be
mistaken for actual shaped geometry.

## Following shaping slice, after admission review

Add private `gui_forms/src/render/text/prepared_window_shape.hpp/.cpp` and
`gui_forms/tests/prepared_window_shape_tests.cpp` only after the first slice is
accepted. Reuse one exclusively owned text engine/font registration and one
bounded workspace across the batch. Shape complete paragraphs in source order,
checking cancellation/authority before and after each native operation. A
single shaping call remains noninterruptible; do not promise a millisecond
deadline or instant join.

For the smallest implementation, retain a bounded array of row geometry owners
using the existing BoundedShapedText form. Its per-row native output allocations
must be counted: owner arrays, runs/glyph capacities, immutable input and row
metrics together must fit one 8 MiB payload. Give each successive shape call
only the remaining output budget. Reuse the 16 MiB controlled workspace; report
its maximum live use rather than summing sequential peaks. The encoded font
bank retains the existing independent lifetime budget. Allocator bookkeeping
and vendor allocations are not covered by these controlled byte quotas.

This first shape implementation may still allocate runs/glyph arrays per row.
Report allocation counts and peak bytes honestly. A contiguous final arena or
caller-filled bulk shaper is a subsequent optimization only if measurements
justify changing that boundary. Never concatenate independent paragraphs into
one bidi paragraph to reduce the call count, and never shape each word as an
independent paragraph.

Publish one `shared_ptr<const PreparedWindowBatchStorage>` only after all rows
succeed and the complete authority/key still match. Row observations are
bounded borrows into that owner. Failure/cancellation produces no partial
renderable output. Keep the existing occupied failed/ready slot semantics until
explicit discard/release; completion alone is not retirement.

Future recorded integration needs a typed batch lease plus row index and
expected full authority. A recorded row keeps the aggregate owner alive. Stale
paint is refused even if the owner remains allocated, including source-identical
replacement desire. Do not reinterpret a batch as an ordinary PreparedTextLayout
or construct one full-payload A2 owner per row. That display change is not part
of either proposed first slice.

## Semantic tests before shared integration

1. Mutate/release every caller buffer after input ownership; all admitted bytes,
   mappings, endpoints, paragraphs and key values remain unchanged.
2. Wrong ledger, occupied output, invalid full key, stale authority, closing,
   exhausted generation budget and every injected allocation failure preserve
   caller input and output; reservations return exactly to their prior counts.
3. Successful admission empties the input owner, changes one input charge to
   one payload charge, and performs no per-paragraph text copy or payload
   reservation. A second input can be admitted while an earlier batch is held.
4. Retain three generations through independent simulated recorded leases;
   the fourth returns busy. Dropping the application wrapper alone does not
   free the charge. Dropping the final lease restores admission.
5. Exact boundary/one-over cases for rows, source/display bytes, combined
   metadata and aggregate output; overflow-safe subtraction/multiplication.
   Include blank EOF, CR/LF/CRLF and atomic-label/lookalike fixtures from PR 14.
6. Equal document bytes with a new authority, controller instance, projection,
   provider/font/context generation, viewport request or scale cannot adopt an
   older result. Epoch exhaustion refuses reuse.
7. Later shaping tests cancel between rows and during a controlled blocked
   native call; no partial result publishes, and owned memory survives until
   the executing call returns. Missing coverage or failure on the last row
   rolls back publication of the whole batch.
8. Repainting an unchanged completed batch performs zero shaping, input copies
   or font registration. Retained display retirement releases the aggregate only
   after its final command lease. These last assertions require the later real
   integration and cannot be proved by the admission unit test.

## Real scenes and measurement boundaries

**File Manager:** selected UTF-8 text in the selection-inspector preview.
`frontend/src/application.cpp` constructs `fm.inspector.preview.text`, limits
the Label to seven lines, and adopts preview text only after selection identity
and generation checks. Use an actual seven-short-line, mixed-script text file,
with each complete paragraph fitting the preview width. Compare current Label
per-line measurement/drawing, sequential A2 paragraph preparation in a harness,
and one aggregate preparation. Measure initial admission and first complete
paint, unchanged repaint, selection replacement before completion, allocation
count, peak controlled bytes and retirement with old display commands held.

This is a real bounded subcase, not replacement of the current word-wrapped
preview. Long/wrapped preview text still needs a reviewed wrapping stage. The
ObjectView details path is also a useful later benchmark: it currently draws
each visible cell separately. However, independent cells, per-column width and
truncation are not certified by PR 14's contiguous one-font paragraph window;
do not label that scene supported by this first candidate.

**SwiftEdit:** a visible no-wrap page of complete short plain-text paragraphs
from the existing DocumentProjection/Session source. Use a 16 MiB read-only
fixture and a viewport-sized projection containing distinct mixed-script lines,
blank rows and inert control labels. Compare sequential A2 paragraph preparation
with one batch, then repeated paint and one-page navigation, source revision
replacement, cancellation and retained-generation pressure. Verify source/display
endpoints and rendered row baselines in addition to timings. This is the next
useful core-to-GUI bridge; it does not prove GUI editing, IME, selection, or
malformed-byte save-policy migration.

The current Markdown distinct-word stress exposes why repeated native shaping
matters, but its styled spans and wrapping are outside this candidate's profile.
Do not claim that this first batch alone fixes that scene. Admission must reject
unsupported profiles rather than silently changing their presentation.

Record cold and warm samples separately, p50/p95/max with sample counts, native
UI observer gaps, completion-to-paint time, shape-call count, allocations, live
generations and settled idle. No fixed performance improvement is assumed in
advance; seven-line previews may not amortize the added service work.

## Source fingerprints for this review

SHA-256, read in place (the coordinator may subsequently change these files):

```text
79dc52f3053e382a12dcbf91ac9546bae941f5ad345165287396d1ed11162c87  prepared_text/types/prepared_text_types.hpp
904b65bd5cc7900fbd00d68282fd110ec1767ec5432b7fe5bf77601398772d65  prepared_text/service/prepared_text_service.hpp
545f8b13550b67f7f7fd1cf20d294ce13d91df6a3d6f9b25aadd589054110b9d  prepared_text/layout/prepared_text_layout.hpp
cce3e793e2e4f6035f01b3cbe717db7453cf2f167bb48ba28b9920664b9fd19e  src/core/text/prepared/prepared_storage.hpp
9aa404c16b0cbe414bbcab720895626b6e2ce1f88f95ee2111ea4037ceb4237d  src/core/text/prepared/prepared_storage.cpp
ec473805b419a1277c92fa8277d4a3fb8d0085627a17ec5f44987ab3c4b45252  src/render/text/prepared_service.cpp
94722aad2624a852a30b6227d2501a7955047ce86935a83ec1f91feb58df7c45  src/core/text/shaping/shaped_text_geometry.hpp
4a107851548cf1f09404fec03ab3887ce0c4172125a69b8a8ec6170f996b25fd  src/core/display/recording_painter/recording_painter.cpp
dfbd9398018c8ce94c382d508bc96b6b13f20e750cd77c57d545f34745fd7c82  src/controls/panel/object_view/object_view.cpp
b4d27b13d6cedff938cf099e1519aaa9a97dbaaeedc92c42a12492cec00021c7  frontend/src/application.cpp
```

Paths other than the final frontend path are within `gui_forms`; the first three
are within its `include/gui_forms` directory. The exact PR 14 revision, rather
than the mutable current checkout, governs the private input findings.

