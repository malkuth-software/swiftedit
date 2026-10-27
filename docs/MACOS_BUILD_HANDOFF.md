# MacBook dogfood implementation checkpoint

The owner's 2026-10-01 direction is a downloadable, usable SwiftEdit MacBook
application with cross-platform repository builds. This remains required; a CSV
library build or a passing source audit does not satisfy it.

## Current evidence

* No native SwiftEdit macOS package exists or has been verified.
* `portable-core.yml` now runs the real CSV/formula implementation and regression
  tests on Windows, macOS and Linux. Its artifacts are test diagnostics, not apps.
* Application startup now uses Windows Unicode arguments on Windows and ordinary
  process arguments on POSIX. Picker drive enumeration is Windows-only; POSIX
  uses the filesystem root. Local date insertion uses the platform localtime API
  and reports conversion failure. Windows build validation preserves current use.
* Native CMake now selects Windows or POSIX file/page adapters and creates a
  macOS app-bundle target. Whole-application Mac validation still awaits matching
  installed SDKs; only the standalone core/adapters have passed Mac CI.
* File Manager has a macos-arm64 dogfood app release, but its release assets do
  not supply the matching installed GUI.Forms/picker SDK required by SwiftEdit.
  The provider confirms no new prepared-text SDK is available yet.

## Resume in this order

1. Obtain a coherent installed public GUI.Forms Application + DocumentPicker SDK
   for macOS arm64 and Linux, including redistribution/runtime dependency data.
   Coordinate with toolkit owner; do not copy private provider implementation.
   SwiftEdit is private and has no repository secrets. Its GITHUB_TOKEN cannot
   read a different private repository. Mirror verified SDK-only archives into a
   pinned SwiftEdit dependency release, or configure narrowly scoped access.
   Do not expose a personal authentication token in workflows or logs.
2. Move Windows PagedFile code out of session.cpp into a platform adapter; add a
   POSIX descriptor implementation with bounded pread and mutation detection.
   Add a POSIX file adapter that preserves new-target non-overwrite, snapshot
   conflict detection, metadata, temporary cleanup and recoverability. POSIX
   advisory locks are not Windows deny-write-sharing; test and document that
   distinction instead of claiming equivalent mandatory protection.
3. Port process-ID test fixtures and writer-conflict tests. Update CMake platform
   source selection, shell32 gating and the macOS bundle target. Windows terminal
   code remains Windows-specific; the native app must not depend on it.
4. Add native build/package jobs for macOS/Windows/Linux consuming the pinned
   installed SDKs. macOS-26 arm64 matches File Manager's existing runner baseline;
   this does not establish support for Intel Macs or older macOS versions.
5. Bundle SwiftEdit.app with required dylibs/fonts/notices, fix and verify loader
   paths, ad-hoc sign the development bundle and verify startup plus fixture
   open/edit/save using native CI. Preserve test and source/hash receipts. Publish
   a clearly labeled dogfood archive and give the owner a working download link.
   Developer ID signing/notarization are separate from ad-hoc signing; do not
   claim either without actual verification.

The previous 3f26376 rendering checkpoint remains valid. A new public-API timing
probe shows installed TextStore::replace still costs about 41.6 ms median on a
1 MiB ASCII line; merely switching to that API will not remove edit latency.
Its samples are retained beside this handoff in performance/2026-10-01-navigation-probe/.
The full feature goal and final end-to-end lag review remain open.

2026-10-01 paged file portability checkpoint: PagedFile/Page moved to independent
paged_file.hpp. Windows retained-handle semantics are in paged_windows.cpp; the
actual Windows Session links that adapter. paged_posix.cpp uses bounded pread,
regular-file/NOFOLLOW checks and before/after identity/size/mtime/ctime checks.
POSIX cannot prevent uncooperative writes; changed pages are refused, not frozen
by a mandatory lock. Common tests cover exact raw bytes, EOF, offsets, budget and
32 MiB bounded tail reads; POSIX adds mutation, symlink and nonblocking FIFO tests.
Windows full 15-suite run passed (2.43s); standalone core/readers 2/2 passed.
CI now compiles both adapters on their actual runners. Mac/Linux result pending
for this commit; native application save adapter/SDK/package still unfinished.

Verified cross-platform follow-up: commit 27ffbfc passed run 36848596407 on
Windows-2022, macOS-26 and Ubuntu-24.04. Both CSV/formula and paged-reader tests
compiled and executed on each runner. POSIX-specific changed-file, symlink and
FIFO checks passed on macOS/Linux. Evidence:
https://github.com/falseywinchnet/swiftedit/actions/runs/36848596407
This proves the tested file-reader component, not a native app or downloadable
Mac bundle. Next: POSIX save/conflict semantics and installed macOS SDK packaging.

2026-10-01 POSIX save checkpoint: 3e655d4 passed real Windows/macOS/Linux CI run
36849322565. POSIX adapter creates sibling files privately, preserves existing
permissions/attributes, checks snapshots and metadata before publication, uses
atomic no-overwrite link publication for new files and atomic name exchange for
replacement. Cooperating writers are excluded by flock. A detected race during exchange verification
leaves displaced bytes at the reported sibling recovery path and reports
failure; it does not claim the visible target was unchanged after an uncertain
publication. POSIX uncooperative writers are not prevented by mandatory locks.
Deterministic test-only publication hooks prove new-target collision refusal and
displaced-content retention. Ordinary saves delete temporary files; no persistent
history is retained on success. Unsupported atomic exchange/metadata copy fails
without a destructive fallback. New files start private (0600).
Main CMake now selects platform adapters and creates a macOS app bundle; Windows
console target stays Windows-only. Fixture process IDs, permissions and cooperating
writer ownership are portable. Windows native build + 15 suites passed (2.10s),
69-file spelling audit clean. Full Mac GUI/SDK/bundle validation remains pending.
The modification-time follow-up is being rechecked on macOS/Linux CI.

Verified follow-up f12d33c: cross-platform run 36849715223 passed Windows, macOS
and Linux, including fresh modification time after metadata-preserving save.
https://github.com/falseywinchnet/swiftedit/actions/runs/36849715223
The POSIX publication hook is compiled only into the standalone adapter test
target; production builds have no injected callback. No native GUI Mac build or
bundle has been validated. Provider-installed SDK archives remain outstanding.

2026-10-01 SDK export gate resolved: provider commit
6def54ad2bf2089b57c09337c0b3f82cc1178187 passed native Windows x64, macOS ARM64,
and Linux x64 builds plus independent relocated Application/picker consumer link
checks in run 36850852233. All three downloaded ZIP hashes and installed-content
hashes were independently verified by tools/Verify-Sdk-Archive.py. SDK-only
archives are mirrored into SwiftEdit's private sdk-gui-forms-6def54a release and
pinned in ci/native-sdk-lock.json. No new prepared-text availability is asserted.

The owner confirmed Apple silicon with macOS 26. Native CI now builds/tests all
three platforms, including the owned native lifecycle fixture, and stages a Mac
app with fonts, libraries, notices, loader verification, ad-hoc signing, and a
bounded packaged startup check. Actual SwiftEdit native CI results are pending
this checkpoint; do not confuse successful provider exports with an app release.
Local existing-SDK Windows regression: 16 suites passed in 2.02 seconds, 71-file
spelling scan clean. Semantic review covered SDK pinning, new staging ownership,
child cleanup, inherited environment removal, and fail-before-archive ordering.
