# SwiftEdit

A native C++20 text editor built with the installed GUI.Forms and File Manager
Document Picker packages. Traditional dropdown menus, owned dialogs, plain-text
clipboard, no ribbon, browser runtime or persistent recovery files.

**Downloads:** [SwiftEdit releases](https://github.com/falseywinchnet/swiftedit/releases).
Choose a versioned cross-platform release and download the installer for your
platform. These are public downloads; GitHub sign-in is not required.

| Platform | Installer | Installation |
|---|---|---|
| Apple silicon, macOS 14+ (CI tests macOS 15) | `SwiftEdit-*-macos-arm64.pkg` | Installs `SwiftEdit.app` into Applications |
| Windows x64 | `SwiftEdit-*-windows-x64-setup.exe` | Per-user installation, Start menu shortcut and uninstall entry |
| Linux x64, Ubuntu 24.04-compatible runtime, X11 | `SwiftEdit-*-linux-amd64.deb` | Install with `sudo apt install ./SwiftEdit-*-linux-amd64.deb`; launch from the desktop menu or `swiftedit` |

Portable ZIPs and Linux tarballs remain available. Extract the entire archive
before launching the application. Releases through v0.3.10 contain portable archives only.

Mac builds are ad-hoc signed and not notarized; Windows builds are unsigned.
Each release includes SHA-256 checksums and source/SDK manifests. The terminal
editor is included on all three platforms. Builds after v0.3.0 also include the
command-session CLI on Mac at `SwiftEdit.app/Contents/MacOS/swiftedit-cli`;
v0.3.0 includes the CLI on Windows and Linux only.

Native CI builds and tests Windows x64, macOS ARM64 and Linux x64 on code pushes.
It builds matching public packages from the standalone source revisions in
`ci/dependencies.json`, importing verified provider compiler caches. After all three native jobs and packaged startup checks
and installer checks pass, master pushes automatically publish one immutable
release containing all three installers and portable archives. Following
PlaySuite, the version is selected before building: baseline major/minor plus
baseline patch + workflow run number. Numbers can skip; reruns keep their version.
Pull requests and ordinary manual runs build and test without publishing.
Manual runs can request publication from master; explicit version tags remain
supported. Documentation-only pushes are skipped. Historical downloads remain available.
The
[first release evidence](docs/DOGFOOD_2026-10-01.md) records the exact checks.

This development checkpoint adds the SwiftEdit identity, separate as-opened
restore, last-save undo boundary, grapheme character/selection counts, explicit
LF/CRLF conversion, date/time insertion (F5), bracket-based filename suggestions,
and confirmation before pasting more than 500,000 bytes.

`swiftedit-cli.exe` provides a separate explicit command session with byte-faithful
editing, exact-context preview/commit, stale revision guards, bounded file pages,
read-only mode at 16 MiB, invalid-byte save refusal and Save Text Copy. CSV
commands preserve unaffected source spelling and offer strict exact arithmetic.
See [command protocol](docs/COMMAND_PROTOCOL.md) for commands and examples.

The GUI and command model are not yet unified. The GUI still uses the public
TextBox's 1 MiB / 4096-byte-line bounds and supports UTF-8 plus BOM-marked UTF-16.
The command model preserves raw bytes and only publishes valid UTF-8. Native
Markdown/CSV views, the conventional full-screen terminal and remaining provider
features are tracked in [the objective ledger](docs/SWIFTEDIT_OBJECTIVES.md).
This is not a claim that the full expanded product is finished.

## Build and run

SwiftEdit follows the standalone GUI.Forms / PlaySuite LLVM 22 build contract.
The exact GUI.Forms and File Manager picker revisions and published compiler-cache
hashes are recorded in `ci/dependencies.json`. The old `ci/native-sdk-lock.json`
is retained only as historical release evidence; current CI does not use it.

Install LLVM 22.1.x, CMake 3.25+, Ninja, ccache, Python 3.12+, Git and GitHub CLI.
Windows uses MSYS2 **CLANG64**; put its `bin` directory on PATH. macOS uses
Homebrew `llvm@22`, with the provider's verified LLVM runtime for macOS 14.
Linux uses LLVM 22 and the X11/ATK dependencies listed in the native workflow.

```sh
python -B tools/prepare_dependencies.py --platform windows-x64 --restore-cache
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64
```

Use `linux-x64` or `macos-arm64` for the other hosts. Preparation checks out the
pinned providers, authenticates the published GUI.Forms cache, and builds the
public installed GUI.Forms and picker packages with the same toolchain.
Cache misses compile from pinned source. Integrity failures stop the import.
The source directory layout matches the provider's cache contract. Provider
sources stay in ignored directories; SwiftEdit links their exported CMake targets.

`cmake --build --preset windows-x64 --target swiftedit-app` builds the GUI,
terminal and CLI. `swiftedit-check` builds the test executables and runs CTest.
`swiftedit-package` creates the platform archive and performs packaged startup
checks; run it on a dedicated runner or during coordinated desktop validation.
Native window tests are opt-in locally (`-DNOTEPAD_NATIVE_TESTS=ON`) and enabled
on dedicated CI runners. Defaults use two local compiler jobs.

`tools/Build-Windows.ps1` is the Windows build/test/stage convenience command.
It discovers Clang on PATH (or accepts `-Toolchain` / `SWIFTEDIT_TOOLCHAIN`),
prepares dependencies, incrementally builds, tests, and stages `dist/SwiftEdit`.
It checks SDK fingerprints around validation without deleting usable objects.
Use `-NativeTests` for coordinated self-closing native tests; `-BuildDirectory`
and `-StageDirectory` remain available. Running copies are protected.

CI saves compiler objects immediately after a successful build, before testing
and packaging. Renderer output is cached separately. All three platforms must
pass before automatic release publication. CI's `Build-Native.py --installers`
also checks installation, installed payload hashes and GUI/CLI startup; Windows
checks reinstall and document-preserving uninstall, and Linux checks package removal.
Installer installation tests run only on dedicated GitHub Actions runners.
The packaging tools retain their
runtime-closure checks, native launch checks, checksums and source manifests.

Launch `dist/SwiftEdit/SwiftEdit.exe`, optionally with one quoted Unicode path.
Run `dist/SwiftEdit/swiftedit-cli.exe` with stdin/stdout pipes for command sessions.

## Ownership and boundaries

This is an independent repository at `C:/Users/Shadow/notepad`; the source
directory and historical internal C++ namespace have not been renamed.
GUI.Forms owns native editing mechanics, File Manager owns the picker, SwiftEdit
owns interpretation, session behavior and publication. See
[known gaps](docs/KNOWN_GAPS.md) and [validation](docs/VALIDATION.md).

## MacBook and cross-platform builds

Native GUI and terminal builds, tests and packaging run on Windows, macOS and
Linux in [the native workflow](.github/workflows/native-builds.yml). Download
applications from Releases; CI diagnostic artifacts contain additional test and
performance evidence. Older Mac-only releases remain available.

The expanded feature set is not complete. The owner reports that blank-window
Mac CPU usage appears resolved in dogfooding; GUI polish remains in progress. See the
[objective ledger](docs/SWIFTEDIT_OBJECTIVES.md) for remaining work.
