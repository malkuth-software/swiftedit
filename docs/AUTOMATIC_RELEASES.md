# Automatic installer releases

SwiftEdit follows PlaySuite's publication flow: a code push to `master` selects
one version, builds the three native platforms with imported GUI.Forms and
retained consumer caches, tests the applications and installers, then publishes
the complete tested set as an immutable GitHub release.

## Triggers and version identity

- Code pushes to `master` publish after all native jobs pass.
- Pull requests build and test without publication.
- Manual workflow dispatch builds without publication unless `publish` is
  selected. Manual publication is restricted to `master`.
- Explicit `v` tags remain supported within the source baseline major/minor
  series, at or above its baseline patch version.
- Documentation-only pushes are skipped. A newer branch run supersedes older
  build work; publication checks the current remote branch before tagging and
  again before making a draft public.

Like PlaySuite, automatic versions use the baseline major/minor and
`baseline patch + workflow run number`. Skipped or failed runs can leave gaps.
Reruns retain their version. The selection happens before CMake configuration;
the app's version, macOS bundle version, installer versions and external
manifests all use that selection. Packaging rejects a different configured
version. Published assets are never silently replaced.

## Installer behavior and CI verification

| Platform | Installer | Checks on the dedicated runner |
| --- | --- | --- |
| Windows x64 | Per-user NSIS setup, Start menu shortcut, uninstall entry | Install into a path containing spaces; compare payload hashes; launch installed GUI and CLI; reinstall; uninstall while preserving a user document |
| Apple silicon macOS | `.pkg` installs `SwiftEdit.app` into `/Applications` | Install with the system installer; compare payload hashes; verify app signature; launch installed GUI and CLI |
| Ubuntu 24.04-compatible Linux x64 | `.deb` installs a desktop entry and `swiftedit`, `swiftedit-terminal`, `swiftedit-cli` commands | Install with apt; compare payload hashes; launch installed GUI and CLI under an owned virtual display; remove package |

Portable ZIPs and tarballs are retained. Native regression tests and the
existing portable startup, dependency closure, terminal and CLI checks remain
required. Local builds do not install packages into the developer's system;
installer installation tests require a dedicated GitHub Actions runner.

The publisher requires all three installer receipts, exact source and dependency
revisions, matching release versions, and matching archive/installer SHA-256
sidecars. It creates a tag at the tested commit, uploads the complete set to a
draft, verifies GitHub's asset digests and only then makes the release public.
Windows/Linux uninstall checks are separate from the application regression
suite; macOS uses the normal Applications bundle installation model.

Installers are unsigned; the macOS application is ad-hoc signed and not
notarized. Automatic publication delivers downloadable releases; an installed
copy does not automatically update itself.

## First published installer set

[v0.3.203](https://github.com/falseywinchnet/swiftedit/releases/tag/v0.3.203)
was automatically published from `b01f3162d8b969a7d4dbc70caf557c0e96f43510` by
[run 37739753849](https://github.com/falseywinchnet/swiftedit/actions/runs/37739753849).
Windows and Linux each passed 39 native tests; macOS passed 40. All installer
checks above passed. Cacheable compilations reused 331/332 Windows, 449/450
Linux and 455/456 macOS entries. These counts include direct and preprocessed hits.

The public downloads were independently retrieved and checked against archive
and installer sidecars, source/dependency/version identities and installation
receipts. Manifest-listed archive hashes covered 52 Windows files, 101 Linux
files and the three macOS executables. This does not independently certify
unlisted macOS bundle files. See `releases/v0.3.203-verification.json` for the
digests and exact evidence. Local headless checks passed 31 tests; publication
contract checks passed nine, and cache-admission checks passed five.
