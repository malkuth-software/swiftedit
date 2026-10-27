# Coalesced identity mappings

The D1 producer previously emitted one identity mapping per source grapheme.
It now combines adjacent unchanged text into one identity span. Each control or
illegal-byte label remains a separate atomic mapping, including adjacent labels
with identical display text. Source selection still applies its own grapheme
checks; an identity mapping is not a caret-boundary certificate.

For the 65,536-byte ASCII fixture, retained mapping records fall from 65,536 to
one. The same reduction applies to the tested 64 KiB page of short plain-text
lines. Dense control and illegal-byte fixtures retain all 65,536 atomic records.
The producer counts required mappings before reserving the output table. The
DisplayPage segmentation workspace is unchanged, so this does not eliminate
per-grapheme temporary metadata or establish a lower whole-process peak.

## Measurement and limits

Windows x64, MinGW Release, installed public SDK aaca5d0, existing build directory
`.build/swiftedit-sdk-aaca5d0`. Existing projection benchmark: 5 warmups and 30
recorded samples per 64 KiB fixture. Raw stage timings are retained in before.csv
and after.csv. Baseline source is ef47d42. The after benchmark checks mapping
equivalence against each original DisplayPage unit outside the timed interval,
instead of requiring one mapping per unit.

| Fixture | Before median projection ms | After median projection ms |
|---|---:|---:|
| ASCII | 5.1604 | 6.8528 |
| NUL controls | 6.1155 | 6.1720 |
| Illegal bytes | 5.8055 | 6.3039 |

This run does not show a latency improvement; the ASCII sample median is worse.
The machine was not isolated from other work, and no causal performance claim is
made. The reason for retaining the change is the directly checked reduction in
retained mapping records, not a speed claim. This is neither native GUI timing
nor the requested final lag audit. The complete 64 KiB projection remains one
synchronous processing step after bounded reads.

## Correctness and review

The new coalescing assertion failed against the old producer before the change.
After the change, all 29 configured Windows suites passed in 29.48 seconds.
Coverage includes full-page source extent, literal/generated label distinction,
interior positions in an identity span, adjacent atomic labels at the mapping
limit, four decoded encodings, Unicode grapheme selection refusal, cancellation,
stale revisions, occupied output and read-only pages. The installed public D1
view accepts the produced payloads and preserves atomic-label boundary checks.

Authored changes in src/document_projection.hpp/.cpp and the projection test and
benchmark were reviewed against docs/PROGRAMMING_HOUSE_STYLE.md for explicit
types, initialized state, bounded reservation before traversal, owned staged
output, checked bounded offset conversions, source/borrow lifetime and failure
preservation. No provider sources or SDKs changed. Native cross-platform checks
remain pending. GUI Session migration and prepared-window rendering remain open.
