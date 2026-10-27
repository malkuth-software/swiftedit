# Standalone GUI.Forms build adoption

Compared with PlaySuite `origin/main` at `80f5ba8` on 2026-10-07, especially
`applications.yml`, `restore_toolkit_cache.py` and the public CMake targets.
SwiftEdit now uses the same reviewed GUI.Forms source pin, `e589142`, and its
LLVM 22 contract. `ci/dependencies.json` is the authoritative full-revision and
archive-digest lock. The File Manager picker is separately pinned at `964261f`.

## Reuse and boundaries

- GUI.Forms is checked out at `gui_forms/`, with its build at
  `.build/native-<platform>/gui-forms/`. This matches the provider cache layout.
- All components use named `clang`/`clang++` drivers, the provider's
  `cmake/llvm22.cmake`, and ccache with a workspace base directory and compiler
  content checking. Windows uses CLANG64, not the previous GCC/MINGW64 packages.
- The published cache archive digest, repository, revision and platform are
  checked before extraction. Path and link checks precede writes. An unavailable
  download permits source compilation; a mismatched payload is an error.
- Provider cache entries are imported before SwiftEdit's own cache restore.
  Compiler/environment identity separates CI cache keys. Compatible prior
  application objects remain available across source changes.
- The renderer has a separate source/output cache. macOS runtime imports are
  verified by the provider's payload/compiler/SDK validator and minimum-platform
  audit, with source rebuild on mismatch.
- GUI.Forms and the picker are built by their own CMake projects, installed
  together and consumed through `GUIForms::Application` and
  `FileManager::DocumentPickerView`. No provider-private implementation is
  copied into SwiftEdit. The picker-only target does not build backend services.
  Application owns the shared Core used by the document model and GUI; the
  consumer does not link a second static Core alongside it.
- SwiftEdit's compiler cache is saved after successful compilation, before
  tests/package startup. A cached object is never treated as test evidence.
- The duplicate core matrix is manual-only; its additional file-adapter test
  is now part of the normal SwiftEdit suite. Native tests and packaging remain
  required on every supported platform before release publication.

## Entry points

`prepare_dependencies.py --platform <platform> --restore-cache` prepares the
reviewed packages. `CMakePresets.json` defines matching configure/build/test
presets for `windows-x64`, `linux-x64` and `macos-arm64`. Local concurrency is
two jobs; dedicated CI runners use four.

`swiftedit-app` builds GUI/terminal/CLI; `swiftedit-check` builds and runs tests;
`swiftedit-package` creates and checks the platform archive. The Windows staging
wrapper uses this same toolchain and incremental build. It checks SDK content
fingerprints without cleaning reusable objects on every dependency refresh.

## Measured local validation

Windows x64, Clang 22.1.8, Release, two jobs, 2026-10-07:

- The pinned GUI.Forms and picker compiled and installed successfully.
- An unchanged application build reported `ninja: no work to do`, taking
  1.369 seconds including CMake regeneration for added convenience targets.
- After deleting only generated application outputs through Ninja's clean
  target, rebuilding took 20.543 seconds including linking. All 103 compilation
  calls were direct ccache hits; zero misses. This measures local warm-cache
  reconstruction, not the hit rate of the imported provider archive.
- Cache admission tests cover successful import, wrong digest, wrong revision
  and path traversal, with no writes on rejected input.
- LLVM's Windows DLL boundary exposed leaf scrollbar RTTI assumptions in the
  view tests. Tests now inspect the exported ScrollBar interface and explicitly
  check orientation. No production navigation assertions were relaxed.

Native CI results must be recorded separately; local headless checks do not
establish macOS/Linux packaging or physical input latency.

## Final cross-platform CI validation

Code revision `cee0affd7fc6f5e3124fbcec2b6f3be0eb8fd8ec` passed
[run 37730789965](https://github.com/falseywinchnet/swiftedit/actions/runs/37730789965)
on all three platforms. The run completed on 2026-10-08 UTC (2026-10-07 Pacific).

| Platform | Native tests passed | Direct cache hits | Preprocessed hits | Cache misses |
| --- | ---: | ---: | ---: | ---: |
| Windows x64 | 39 | 332 | 0 | 0 |
| Linux x64 | 39 | 449 | 0 | 1 |
| macOS arm64 | 40 | 449 | 7 | 0 |

These are cacheable compilation results from the final run, combining imported
provider entries with retained consumer entries. They exclude uncacheable
compiler probes. Native package startup and runtime dependency checks passed.
The five Python cache-admission tests also passed; the local clean-output
`swiftedit-check` and Windows staging wrapper each passed all 31 headless tests.

Downloaded CI archives were independently checked against their SHA-256
sidecars, exact source/toolkit/picker revisions, and manifest file hashes:
52 Windows files, 101 Linux files, and the three macOS executables. The receipt
is `BUILD_VALIDATION_2026-10-07.json`. This verification does not independently
certify unlisted macOS bundle files. LLVM runtime license notices were also
confirmed inside the Windows and macOS packages.

All changes are on `master`; no open SwiftEdit pull requests remained at
validation. These are CI artifacts, not a newly tagged GitHub release.
Physical input latency and interactive desktop dogfooding were not measured
as part of this build-system migration.
