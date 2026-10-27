# Mac idle CPU baseline — unresolved defect

## Evidence correction: native fixtures lacked bundled fonts

The native CSV view capture from source `27655d1`, run `36949493970`, exposed
missing text despite successful tests. Its renderer, and the controlled idle
probe renderer, report `incomplete bundled font pack`. The Mac host resolves
fonts from NSBundle; our bare test executables did not have bundle resources.
`GUI_FORMS_FONT_DIR` in the test environment does not override that Mac path.

**All earlier controlled native idle observations below are fontless-fixture
measurements, not representative measurements of the packaged application's
normal text-rendering path.** Preserve their raw counters, but do not use their
lower CPU percentages to discount the owner's 7% report or assess a product fix.
The separately launched packaged app already has fonts; its measurements retain
their stated uncontrolled-focus/occlusion limitations. No numerical correction
or loaded-font baseline can be inferred from these older observations.

The correction bundles each Mac native GUI fixture with the installed font pack
and refuses visual/idle runs when the host reports incomplete fonts. The first
corrected result is recorded below. The provider coordinator independently confirmed
the same resource gap in its idle fixture and is correcting it there too.

## First font-loaded controlled baseline: 1504e2b

Source `1504e2b187507086f25fba01fcf2e51a6c52a656`, unchanged SDK723cd7f,
passed native run `36949960740` on all three platforms and core run `36949960720`.
All 18 Mac window rows have the positive bundled-font renderer identity.

| Observation | Focused blank | Text focus cleared |
|---|---:|---:|
| CPU seconds | 0.023676 | 0.001006 |
| Elapsed seconds | 5.06026 | 5.01014 |
| One-core CPU | 0.467881% | 0.0200793% |
| Main paints / deadlines / wakes | 9 / 9 / 9 | 0 / 0 / 0 |
| Each of eight hidden dialogs: paints / deadlines / wakes | 0 / 0 / 0 | 0 / 0 / 0 |

Main focused painted area is342 and recorded presentation wall time4,039,414ns.
All hidden dialogs remain occluded. Rawrows are
`baseline-1504e2b-fonts-controlled.txt`. This is one CI observation, not a
statistical distribution, an improvement comparison with fontless results,
a reproduction of the owner's7%, or a CPU fix. Model focus clearing still
does not mean switching applications. Owner-reported focused idle remains open.

## Second font-loaded baseline: 38c53c4

Native run36950568367 passed all three platforms; core36950568341 passed.
The Mac observation was1.39116% focused (0.069677 CPU seconds /5.00857 elapsed)
and0.036735% after text focus clearing (0.00187 /5.09052). All18 rows retain the
loaded-font renderer. Main focused paints/deadlines/wakes remain9/9/9, area342,
recorded presentation wall2,553,624ns; the cleared main and all8 hidden dialogs
are quiet. Raw rows: `baseline-38c53c4-fonts-controlled.txt`.

This variation from the preceding0.467881% sample is not a controlled regression
or improvement comparison. The product changes affect CSV/Markdown/search text
alignment; no blank-editor CPU optimization was made. Single-run percentages
cannot establish resolution of the owner's report. The packaged-app uncontrolled
ten-second observation was0.897989%; its distinct scope remains unchanged.

## Historical observations before native fixture font correction

Owner report: SwiftEdit consumes approximately 7% CPU on an Apple silicon Mac
running macOS 26 while showing a blank document. No claim of acceptability or
resolved cause is made.

Native CI 36864154136 built application 5d09897b312dea056f60106ec71ff973877bc4c2
against provider 6def54ad2bf2089b57c09337c0b3f82cc1178187. All three native jobs
passed. The Mac packager ran the bundled app with an empty text fixture, removed
SDK font/dynamic-library environment overrides, waited five seconds, and sampled
process CPU time with /bin/ps around ten quiet seconds. The owned child was
terminated and reaped after observation.

- Runner: macOS 26.6.2 ARM64.
- Elapsed interval: 10.104475374999993 s.
- Process CPU delta: 0.24 s.
- CPU: 2.375185163930395% of one core, not normalized across machine cores.
- Focus, caret focus, occlusion, minimization and window backing scale were not
  controlled or captured. This is one observation, not a distribution or a
  reproduction of the exact owner machine's 7% reading.
- No pass threshold was inferred from this result.

The exact artifact manifest is baseline-manifest.json. Downloaded ZIP hash
matched its checksum file:
1e034a23307147ddfa0c398f2e01f8de83b79fad7a29cea1b637b4910be89faf.

Shared toolkit coordinator owns Mac host/caret/timer investigation and trusted
picker symlink navigation. Source inspection shows focused TextBox schedules
caret redraw at 530 ms; this is a lead, not a proven explanation. Hidden owned
window scheduling also requires profiling. Consumer Home/start path correction
is implemented and tested, independently of these unresolved provider defects.

## Native stack observation

Application57f297157a8562199bbe9dd0c3b6028ad80c62c1, same SDK 6def54a,
Mac job in native run36864723389 passed. Before profiling, CPU delta0.12s
across10.063658417s gave1.192409311% of one core. The difference from the earlier
2.375% observation is not an improvement claim: focus/occlusion/backing scale
remain uncontrolled and no provider fix is present.

After that CPU interval, /usr/bin/sample captured five seconds of the owned
child at a requested1ms interval. Main thread had3488 observations,3459 in the
mach_msg2_trap event wait. These are sampled stacks, not CPU-time percentages.
Active stacks include GUIFormsView drawRect/drawRetainedRect, TextBox on_paint,
RecordingPainter measure_text_utf8, SkiaRaster resolve_text_layout_utf8,
HarfBuzzFontEngine resolve/shape and FreeType glyph loading. Raster fills and
macOS image/color conversion also appear. No per-window identity was captured;
there is no evidence here to attribute the work to a particular hidden dialog.
The sample does not prove the cause of the owner's7% reading or establish an
acceptable idle budget. Raw sample, sampler status and manifest are retained.

Provider investigator separately found an initial-hidden-window occlusion
synchronization gap. Its causal contribution requires a native comparison;
it must not be conflated with this sample's text shaping/raster observations.
Both reports were delivered to the coordinator and Mac host investigator.

## Controlled focus comparison (first attempt; final phase failed)

Run 36865627733 / app 7b59e46 used the actual nine-window SwiftEdit topology and
unchanged SDK 6def54a. Three seconds settled startup; each measured interval
lasted about five seconds with a single explicit measurement timer.

| State | CPU seconds | Elapsed seconds | One-core CPU | Main paints/deadlines | Hidden Save picker paints/deadlines |
|---|---:|---:|---:|---:|---:|
| Document focused |0.142749|5.01702|2.84529%|9/9|9/9|
| Document text focus cleared |0.0328|5.01246|0.654369%|0/0|9/8|

Main painted damage totaled 4796820 area units during the focused interval;
Save picker totaled 115776 in each interval. All other owned dialogs had zero
paints/deadlines. Both pickers retained model focus and occluded=false; only
Save picker recorded repeated painting. Main had no measure/arrange passes,
so source/geometry layout was not repeatedly rebuilt in these intervals.

This is direct evidence of hidden Save picker drawing and focused main redraw
cost. It does not assign exact CPU fractions to individual windows, establish
physical MacBook equivalence, or prove the 7% owner reading fully explained.
An initial truncated-output summary incorrectly said every hidden child was
quiet. Full-row extraction revealed Save picker activity; the user and both
provider investigators received an immediate explicit correction.

The intended final hidden-main phase failed because the installed public handle
refused primary-window hide. The native run therefore FAILED; only the first
two observations exist. No hidden/minimized CPU result is claimed. Follow-up
bdbe270 removes the unsupported phase and reruns the supported two-state probe.
Raw first-attempt rows and failure are in controlled-first-attempt.txt.


## Completed controlled probe

The corrected two-phase probe, app bdbe270 in run 36866101259, completed on
macOS with unchanged SDK 6def54a. Its raw output is controlled-completed.txt.

| State | CPU seconds | Elapsed seconds | One-core CPU | Main paints/deadlines | Hidden Save picker paints/deadlines |
|---|---:|---:|---:|---:|---:|
| Document focused | 0.108723 | 5.06108 | 2.14822% | 9/9 | 9/9 |
| Document text focus cleared | 0.02113 | 5.0724 | 0.416568% | 0/0 | 9/8 |

Damage totals and active windows match the first attempt. This reproduces
hidden Save picker painting in a completed probe. It also isolates a repeatable
change when main text focus is cleared, while keeping the app and hidden-dialog
topology present. The measurement timer and sampling/reporting remain fixture
overhead. No minimized/hidden-primary state or performance acceptance is claimed.
Provider fixes must be compared against this same fixture and SDK provenance.

## Provider visibility regression evidence (not yet integrated)

Provider 579c3304b636f3cfd5fbd71f437a200529cc0c25, native run 36866574453,
completed its Mac idle visibility test in 4.87 seconds. The six original host
snapshots are retained in provider-579c330-visibility.txt. Initial hidden state
was synchronized. Between stages 3 and 4, a 750 ms hidden interval, cumulative
wakes stayed at 9, native draws at 6, damage collections at 22, and display ticks
at 0. Native callback faults were zero. Visible caret activity and a second
show succeeded; explicit UI timers continued while hidden as required.

Mac and Linux picker model/view tests also passed with actual directory symlink
fixtures. Their POSIX fixture helper creates links or throws; it cannot silently
skip these assertions. File symlink acceptance is outside this directory fix.

This was not an all-platform green candidate: Windows failed to compile an old
one-argument bind_owner call in its canvas test. The Mac job was cancelled after
its test evidence and SDK artifacts had uploaded when the corrected provider
commit 5cba35103b9a9ccc98dfc034a0b08882ccc4fa16 started run 36867942532.
No SwiftEdit SDK adoption or consumer CPU improvement is proved by these logs.
The consumer idle probe now has a prepared regression gate requiring all hidden
children to be occluded with zero paints and frame deadlines in each interval;
its native validation must accompany the corrected SDK adoption.

The 5cba351 Windows job compiled successfully, then failed both picker link
tests: `trusted directory link enters canonical target` and `picker Open enters
selected folder link through canonical navigation`. This CI runner executed the
link fixtures that local Windows skipped. Each original assertion combined the
operation result with the expected path comparison, so the log did not establish
which part failed. Provider 10fa6642c3a86afa6d5ea1566845999d75a66878 adds operation,
path, canonical-path and last-error diagnostics without relaxing either check;
its native run is 36869160214. SDK adoption remains pending.

The diagnostic Windows job 110392608199 established actual refusal: `entered=0`,
with `canonical_alias` still ending in `folder-link` rather than `Folder` and
`last_error=requested location traverses a symbolic link`. The view selected and
activated the row successfully but reached the same refusal. The installed
Windows toolchain's canonical-path call did not resolve this fixture's directory
link. This is not merely a comparison of different path spellings. The exact
diagnostics were delivered to the provider coordinator for correction. Linux
passed in this run; Mac was still running when the Windows evidence arrived.

Candidate 9ecea5305d1e3365525da63ef154c04920042f22 adds native-handle Windows
directory resolution and the separate caret-damage change. Run 36872407186
passed Mac and Linux; both exported SDKs passed exact revision/platform,
archive-checksum and internal content-hash verification. Mac idle visibility
and Mac/Linux picker model/view tests passed with executed link fixtures.
Windows picker view now passed, but its model test failed with `picker fixture
directory already exists`. The fixture uses a PID-only directory name in two
successive scopes; cleanup/collision diagnosis remains provider-owned. This
candidate was not adopted because the Windows job was not green.

## Accepted provider candidate 0322371

Provider revision `032237152148ff76ca125eb382f43d589d3baeb9`, native run
`36874360894`, completed successfully on macOS ARM64, Windows x64 and Linux
x64. The Windows directory-link model and view cases executed without a skip;
the owned fixture cleanup and PID-root reuse check also passed. The macOS
visibility fixture passed. All three SDK archive receipts and internal installed
content hashes were verified before updating the consumer lock.

This candidate combines initial hidden-window occlusion, validated narrow caret
blink damage, and directory symlink navigation. SwiftEdit now gates the Mac idle
probe on all eight hidden dialogs remaining occluded with zero paints and zero
frame deadlines in both observation phases. Consumer Open and Save tests follow
an actual directory symlink and then return Home on macOS and Linux. These
consumer measurements must pass before claiming that the packaged app fixes the
reported idle work. The provider packaging receipt does not assert prepared-text
availability. File symlink opening remains a separate limitation.

## Consumer result and published refresh: 700157e

Native run `36876699339` and portable run `36876698899` passed on all three
platforms for source `700157e71997aa39aac4f61eced27113f40e7774` with SDK
`032237152148ff76ca125eb382f43d589d3baeb9`. The Mac ran 19 suites, including
actual Open/Save directory-link navigation and the controlled idle probe.
All 18 window observations were parsed, with raw rows retained in
`updated-700157e-controlled.txt`.

| Observation | Focused blank | Text focus cleared |
|---|---:|---:|
| CPU seconds | 0.089931 | 0.009495 |
| Elapsed seconds | 5.07364 | 5.06986 |
| One-core CPU | 1.77252% | 0.187283% |
| Main paints / frame deadlines | 9 / 9 | 0 / 0 |
| Main painted damage area | 342 | 0 |
| Each of eight hidden dialogs: paints / deadlines / wakes | 0 / 0 / 0 | 0 / 0 / 0 |

All hidden dialogs are occluded in both phases. Main focused painted area fell
from 4,796,820 to 342 across nine blinks. However, focused CPU remains close to
the earlier 1.8294–2.14822% observations; the owner-reported idle cost is not
considered fully resolved. Cleared-focus CPU was previously 0.405965–0.416568%.
These are individual CI observations, not statistical distributions or a
reproduction of the owner's machine. Clearing text focus is not app switching.
The candidate combines visibility and caret changes, so these CPU observations
do not assign separate causal percentages to either fix.

The packaged app observed 0.898% of one core across 10.022325375 seconds with
uncontrolled focus/occlusion, after five seconds of startup settling. The exact
manifest is retained in `updated-700157e-manifest.json`. Remaining focused work
and the native stack sample were referred to the toolkit coordinator.

A fresh local Windows Release build passed all 18 headless suites in 4.86 s.
Additional component checks against this SDK: caret status over a 266,240-byte
fixture, 1,000 samples, p50/p95/p99/worst = 0.0023/0.0041/0.0051/0.0117 ms;
formula viewport, 31 samples, 5.6851/6.7171/6.8363/6.8363 ms. These exclude native
presentation and are not a complete product lag certification.

## Provider experiment 4448dde: rejected comparison

The macOS job for provider run 36881268179 succeeded, but its optional paint-cost
experiment did not pass. The preserved receipt reports status `rejected`, exit
code 1, accepted_marker false, and 10.749817833 seconds wall time. The raw log
reports `cadence exceeded declared lateness bound`. Its ABBA header alone is not
an accepted four-interval result. No performance attribution or optimization
conclusion is drawn from this attempt. The coordinator received the rejection
and raw evidence. This remains separate from the successful native build/tests.

## Continued baseline: b4b6462

Native run 36890043148 passed on all three platforms; portable run 36890043097
also passed. With unchanged SDK 0322371, the Mac blank document observed
1.5718% of one core (0.079482 CPU seconds / 5.05675 elapsed) while focused and
0.0253201% (0.001283 / 5.06711) with text focus cleared. All 18 window rows were
parsed: each of the eight hidden dialogs remained occluded, with zero paints,
frame deadlines and scheduler wakes in both phases. The main window recorded
nine paints/deadlines/wakes while focused and zero after focus was cleared.
Focused painted damage area was 342; total measured presentation wall time was
3,070,167 ns. Raw rows are retained in baseline-b4b6462-controlled.txt.

This is another individual CI observation of the existing baseline, not a fix,
a statistical comparison, or a reproduction of the owner's approximately 7%.
The focus association persists. It does not establish which rendering or native
presentation stage consumes the remaining CPU, and clearing model text focus
is not an operating-system application switch. The owner confirmed the report
is for a blank document; the focused-idle defect remains open.

## SDK723cd7f consumer baseline

Source7415870 with SDK723cd7f completed the Mac idle probe in native run
36946592395. Focused CPU was1.02075% (.051213 CPU seconds /5.01719 elapsed),
and cleared text focus was.0162498% (.000819 /5.04006). Parsed all18 window rows:
all8 hidden dialogs remained occluded with zero paints/deadlines/scheduler wakes.
Main focused counts were9/9/9, painted area342, total presentation wall1,881,374ns;
cleared focus had zero activity. Raw rows: baseline-7415870-controlled.txt.
This individual CI observation is not a statistical improvement claim or a fix
for the owner's approximately7% focused blank-editor report. The new SDK includes
picker and diagnostic changes; the focused-idle defect remains open.
