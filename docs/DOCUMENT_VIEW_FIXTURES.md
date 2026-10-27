# Installed document-view adoption fixtures

Run tools/Prepare-DocumentView-Fixtures.py with a new output directory. It refuses
to overwrite an existing directory and writes seven deterministic source files
plus a manifest containing sizes, SHA256 hashes, source probes and scenario names.
Generated files belong in .build, not the repository. No private layout engine,
provider development headers or geometry expectations are introduced.

Fixtures cover literal control-label collisions versus actual NUL/bidi controls,
invalid UTF-8 bytes, mixed newlines, literal CRCR metadata markers, combining text,
CRLF and emoji split across64KiB boundaries, a grapheme longer than64KiB, a100000-
byte logical line, and a16777227-byte read-only file. Same-sized revision A/B
sources support late-result/token invalidation tests. The manifest describes the
sequence; executing it requires the forthcoming installed token/ownership API.

Two fresh generations produced matching manifests and every byte probe passed.
The existing shared Session CLI opened the large fixture read-only (info size
16777227,dirty0,read_only1) and returned the exact final11 bytes, last page +CRLF,
at offset16777216. This proves fixture/source-model facts, not DocumentView
availability, rendering, cancellation or stale-token cleanup. Future consumer
validation must exercise those behaviors against an independently installed SDK.

Long-context cases must verify the provider's documented bounded outcome without
inventing a grapheme boundary, collapsing bytes or silently changing source.
Do not call a permitted refusal full long-line rendering support.

## Session producer checkpoint

`DocumentProjection` now consumes an installed D1 DocumentPageRequest on the
Session executor. It validates the document stamp and exact permitted range,
proves logical-line boundary context without splitting CRLF, reads at most 8 KiB
per step (plus up to four boundary-probe bytes initially), then projects in a
separate bounded step. It emits absolute source mappings, identity text spans
and atomic visible control/illegal-byte labels. Literal label-looking source
stays ordinary source. No provider-private header or development checkout is
used by this consumer.

Publication is single-use, requires an empty output and a still-current Session,
and releases source/scratch storage before handing over the prepared page.
Cancellation and structural refusal release retained preparation storage. I/O or
allocation exceptions preserve external output; the caller must cancel/destroy
the task before acknowledging the provider's producer slot with finish. Success
hands the page to DocumentViewState.publish; it does not finish a slot separately.

The installed SDK accepts an actual produced payload in the new
document-projection test. The corpus covers NUL, malformed bytes, literal labels,
CRLF/context refusal, nonzero absolute source offsets, split-read combining text,
partial cancellation, stale revision, replaced document, occupied output, empty
document, single-use publication and a 64 KiB page from a 16 MiB read-only file.
A 70,000-byte logical line explicitly returns context_required when truncated
at the current 64 KiB source allowance; that is a known unsupported context case,
not successful long-line rendering.

The producer is not yet connected to the visible GUI. Final DisplayPage
construction still processes the bounded batch synchronously and needs timing;
8 KiB reads alone do not bound slow-storage latency. Paragraph descriptors,
provider shaping, visible controls in the editor, multiple selections and the
long-paragraph strategy remain part of the controller/SDK migration work.
