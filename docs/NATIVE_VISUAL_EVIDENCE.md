# Native visual evidence

The native CSV regression now creates an AppKit view PNG on macOS after its
settled-idle observation. The fixture shows numeric formula results, a division
by zero error, a referenced formula, literal text and triple-backtick code
shading. It uses the installed public GUI.Forms SDK and production CsvView.

The test-only Objective-C++ adapter finds exactly one visible window with the
fixture's unique title in this process's NSApplication window list. It captures
only that window's content view, using AppKit's bitmap caching API. It uses no
private provider header, global input, screen-recording request or other
application's window. Temporary Objective-C objects use ARC and a scoped
autorelease pool; no native pointers survive the callback.

PNG encoding and writing occur after the idle metrics have been sampled.
Missing/ambiguous windows and capture/write failures fail the native test.
Artifacts remain in a newly created PID-specific build directory and are
uploaded even when a subsequent build/test step fails. Existing evidence is
never overwritten by a rerun using the same directory.

This is a native view redraw into a bitmap, not a compositor screenshot.
Successful capture alone does not establish correct pixels, keyboard behavior,
desktop placement or acceptable latency. The artifact still requires inspection.
It is also a direct CsvView fixture rather than the full editor menu topology.
The new Markdown fixture waits for an observed visible native paint before
capture and includes headings, emphasis, a quotation, task markers, a table,
code, an inert link and image alt text. It also runs native paint/shutdown checks
on Windows and Linux; PNG capture is currently Mac-only.

Apple documents the capture semantics in
[cacheDisplayInRect:toBitmapImageRep:](https://developer.apple.com/documentation/appkit/nsview/cachedisplay(in:to:)?language=objc).

Initial implementation: the local Windows Release build passed all 20 headless
tests in 4.99 seconds. The spelling audit covered 103 C++/Objective-C++ files
with zero findings. Ownership/failure review checked UI-thread admission,
unique window selection, scoped native lifetime, post-measurement capture and
failure propagation through shutdown. macOS compilation, artifact generation
and visual inspection remain pending native CI.
No local native window launch is required or claimed.

## First artifact revealed invalid font provisioning

Source `27655d1`, native run `36949493970`, passed all three platforms and core
run `36949493974` passed. Mac visual inspection nevertheless FAILED: the 800x600
CSV PNG shows the grid, selection, code background and warning triangle, but
no text anywhere. The log reports `incomplete bundled font pack`. This proves
why a successful native test is insufficient visual evidence.

The Mac host loads fonts from NSBundle. The packaged app has fonts, but bare
test executables did not. The test environment's GUI_FORMS_FONT_DIR is not used
by the Mac host. All five Mac GUI fixture executables now receive app bundles
with the installed fonts, and public renderer diagnostics reject missing fonts
before native visual/idle tests. Earlier controlled idle results are reclassified
in the idle evidence README; no product CPU fix is claimed.

The failed artifact is preserved as `performance/2026-10-01-mac-idle/fontless-csv-27655d1.png`.
The original downloaded log is in `.build/native-27655d1-evidence/mac` and the
immutable CI artifact remains attached to the run. These corrections and the
Markdown fixture passed the local build and all 20 headless tests in 4.03 seconds;
105 authored source/header files passed the spelling scan. Mac bundle execution
and corrected pixel inspection are pending the next native run.

## Loaded fonts exposed text baseline defects

Source1504e2b passed native36949960740 on all three platforms, and core36949960720.
The Mac CSV and Markdown PNGs now show text, confirming bundle provisioning.
Visual inspection found product defects: CSV labels clip against row tops,
Markdown table text sits across borders, the ruler is clipped at the top and
text decorations are displaced. The custom search field used the same erroneous
top-origin draw convention. The native draw API expects a baseline.

The correction uses public resolved ascent/descent metrics for CSV and query
vertical alignment. Markdown retains a baseline and decoration positions with
each measured run and caches the ruler baseline with layout. Warm Markdown
painting still performs no new layout measurements. No file/formula/search
semantics changed. A headless regression injects tall renderer metrics and
checks glyph extents against cell/block/field bounds; all20 suites pass in4.66s,
and the105-file spelling audit passes. Native corrected-pixel inspection is
still required before accepting the fix.

The before images are retained beside the idle evidence as
`csv-baseline-defect-1504e2b.png` and `markdown-baseline-defect-1504e2b.png`.
The loaded-font idle result is recorded separately in that directory's README;
it does not establish a product CPU improvement.

## Corrected native capture: 38c53c4

Native run36950568367 passed Mac, Windows and Linux, and core36950568341 passed.
Both Mac captures were inspected: CSV numeric values, the complete #ERROR label,
literal triple backticks and header/status labels are now positioned inside
their rows. The triangle remains legible beside the error. Markdown heading,
table labels, ruler, inline code, strike and link underline align with the text.
The fixed fixture is accepted; this is not exhaustive scale/theme/Unicode QA or
a physical keyboard test. The search field has the metric regression and native
Find/Replace smoke coverage, but no separate visual capture yet.

The corrected images are `csv-corrected-38c53c4.png` and
`markdown-corrected-38c53c4.png` beside the earlier images in the Mac idle evidence
directory. SwiftEdit0.2.6 dogfood publishes this source with unchanged SDK723cd7f.
