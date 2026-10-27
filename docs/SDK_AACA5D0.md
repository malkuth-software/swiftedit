# GUI polish SDK adoption

Provider source: `aaca5d0bbbeb5f6454185996dfa6d8f1798a32c9`.
Provider native run: `36982408043`, all three platforms passed.
Durable packages: [sdk-aaca5d0](https://github.com/falseywinchnet/file_manager/releases/tag/sdk-aaca5d0).
This is an SDK prerelease checkpoint, not a SwiftEdit application release.

SwiftEdit independently ran `tools/Verify-Sdk-Archive.py` for all three supplied
archives, verifying revision/platform, archive SHA256 and both complete
installed-file fingerprints. The archive hashes are pinned in
`ci/native-sdk-lock.json`, which now records the provider repository explicitly.
Build-Native downloads that release's exact archive; it does not consume a
moving branch or transient Actions artifact. The frozen 723cd7f installation
and prior releases remain intact.

| Platform | Archive SHA256 |
|---|---|
| Windows x64 | `74ea97952170f0ca03af47b6eb55b3dd8330d432ed3666815b2c6fc6c5f5ae93` |
| macOS ARM64 | `5a67c9309918824cae1857bec0a9bc9eecf318c96da8f47d65051d9f672c8d9d` |
| Linux x64 | `de85367a75ee22324db9d1d0e22ad9e66742be2492ed3862e666715936b90039` |

The new host-option layouts and menu signatures require all consumers to
rebuild. The local Windows build uses only the matching installed GUI.Forms and
picker pair in `.build/provider-sdks/aaca5d0/windows-x64/installed`, with fresh
build output in `.build/swiftedit-sdk-aaca5d0`. PowerShell archive extraction
initially assigned future timestamps to the installed CMake files, causing
Ninja's repeated-configuration failure. Only future extraction timestamps were
normalized to current UTC; installed contents were not modified. CI's Python
extraction path does not preserve those archive timestamps.

Consumer changes:

- Native title is `filename - SwiftEdit` or `Untitled - SwiftEdit`, prefixed by
  `* ` for unsaved changes. Successful title text is cached so an unchanged
  title does not issue another native request. Title failure leaves document
  contents unchanged and reports the desired title in the status text.
- The duplicate filename row is removed; source, Markdown and CSV content begin
  directly below the 28-pixel menu bar.
- The provider distinguishes mouse and keyboard menu opening. CSV's explicit
  context-menu show call now requests pointer mode too.
- The native test checks title transitions and uses an ampersand/Unicode
  filename. Mac checks resolve the exact title against this process's visible
  AppKit windows; captures use the new title. The View-menu capture requests
  pointer mode. Headless layout checks reject reintroduction of the extra row.

The provider disclosed a separate new-Details pending-layout keyboard defect.
SwiftEdit's picker uses the legacy empty-column path, and this adoption does
not opt into the new Details API. Provider source remains coordinator-owned.

The full local Windows Release build passed with the matching installed pair;
all 25 headless tests passed in 21.00 seconds. The 132-file spelling audit,
Python syntax check and PowerShell parse check passed. No local native window
was launched. Source review checked title ownership/caching, lifecycle access,
failed-title preservation, explicit pointer mode and layout consistency.

Consumer run 36985848443 at 497f77a passed native tests and application packaging
on Windows, macOS and Linux. Core run 36985848589 also passed. Inspection of the
Mac View-menu and source captures confirms the Markdown active checkmark,
no pointer-opened selected row, removal of the filename row, and inset status
border. Exact native title transitions passed the AppKit assertions; the content
captures do not include native window chrome. Images and provenance are in
`docs/visual/2026-10-02-gui-polish`. This does not close the final physical
responsiveness audit.

SwiftEdit v0.3.3 is public at source 427f5066b4477d7fe20dfc3683a6dc02f68c66eb.
Tag run 36986875922 passed all three native jobs and publication. The release
contains nine assets: one archive, checksum and source/SDK manifest per platform.
The published Mac archive is `SwiftEdit-macos-arm64-427f5066b447.zip`.
