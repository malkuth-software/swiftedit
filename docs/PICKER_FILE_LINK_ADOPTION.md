# File-link picker adoption probe

The SDK pin now targets723cd7f9f8016d854f667f41e0c5dbcbe4cc0adf. Provider native
run36945290227 passed on all three platforms, including picker model/view cases
without skips. All archive receipts and internal content hashes were verified.
The previous0322371 SDK supports directory links but not file-link opening.

`notepad-editor-tests --picker-file-links` is an explicit consumer adoption
probe. It is compiled against installed public headers and uses the retained picker
controls. It is now registered as picker-file-links when NOTEPAD_NATIVE_TESTS
is enabled, so coordinated native CI must execute it before a release claim.
It creates one uniquely owned disposable directory containing a relative file
symlink and its target. It then:

1. Navigates the Open picker to the fixture and selects the visible alias.
2. Activates Open and checks that SwiftEdit displays the target's original text
   and records the canonical target path.
3. Edits and saves through the normal editor command.
4. Checks the target's exact bytes, clean document state and preserved symlink.

The probe requires actual symlink creation and never reports a skipped fixture
as a pass. A fresh local Windows Release build against723cd7f passed all20
ordinary suites in5.51s. This desktop's symlink privilege limitation means the
actual alias probe must run in native CI. At17025dc, native36947473385 passed
the probe on all three platforms. Windows uses native link creation and verifies
reparse data preservation plus reading revised contents through the alias;
its MinGW filesystem helpers cannot reliably create/inspect this fixture.
The verified Mac package is published as v0.2.5-dogfood.20261001.

Run the registered probe on macOS and Linux and in the native Windows runner
before publishing the picker fix. The provider's own cases
remain responsible for profile/filter/root policy, broken/cyclic/stale aliases
and unchanged Save/Export refusal; this probe verifies SwiftEdit integration.
