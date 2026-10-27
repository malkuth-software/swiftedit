# Independent document windows

File > New Window and Ctrl/Cmd+Shift+N launch a blank, independent SwiftEdit
process. File > New continues to replace the current document after its existing
unsaved-work review. New Window does not ask to save, clear selection, change
source, or share document history. Each process owns its normal editor and
dialogs. Launch acceptance does not prove that later GUI startup succeeded.
Mac menu labels now show Command shortcuts. File > Close Window and Cmd+W use
the existing close request and unsaved-work check; they close this editor's
window rather than requesting closure of another process's document.

The coordinating provider chat reconciled this consumer-only boundary on
2026-10-01. Independent windows are the owner requirement; an in-process
implementation was an earlier implementation preference, not an owner mandate.
There is no new shared provider API and the installed SDK stays pinned.

## Source and ownership review

`new_window.cpp` resolves the actual executable independently of working
directory and argv[0]. Windows uses GetModuleFileNameW; macOS uses
_NSGetExecutablePath followed by canonical resolution, including the executable
inside a relocated application bundle. Linux resolves /proc/self/exe. The
editor owns a named callback containing only that absolute path, not an editor
reference. Launch errors go through the existing error dialog.

Windows supplies the absolute executable separately from its quoted mutable
command line to CreateProcessW, passes no document arguments, disables handle
inheritance, and releases both returned process handles. No shell is involved.
See [Microsoft's CreateProcessW contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).

POSIX prepares storage and descriptor bounds before fork. The child redirects
standard streams to /dev/null, keeps only a close-on-exec error pipe, starts a
new session and forks the independent process. The original child is reaped;
the final child execs the exact image and reports exec failure through the pipe.
After fork, only prepared scalars/pointers and async-signal-safe system operations
are used; no GUI, logging, allocation or C++ destruction runs there. Linux uses
close_range with a descriptor-loop fallback; Darwin's kernel per-process limit
bounds its close loop. SwiftEdit does not alter process descriptor limits.
See [fork restrictions](https://man7.org/linux/man-pages/man2/fork.2.html) and
[Apple's executable-path contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dyld.3.html).

Launch is synchronous until exec acceptance/error, with no recurring timer,
polling thread or retained process handle. Child startup latency and actual
packaged-app window interaction still need native dogfood measurements.

## Validation scope

All 20 local headless suites passed in 3.61 seconds. New editor checks verify
dirty source/selection preservation, absence of a save prompt and failure
reporting. The 100-file spelling audit passed; callback ownership, descriptor
cleanup, child-only execution and error paths were reviewed separately.

The opt-in native CI helper launches a copied executable with spaces, an
ampersand and Unicode in its filename, verifies its independent completion and
actual executable location, and checks relative/missing/non-executable path
refusal. No local native launch was performed. This helper does not exercise
two real editor windows or prove packaged macOS application activation behavior.

The first adapter run (0af3cf5, native run 36885936459) passed the macOS helper
in 0.07 seconds; all three native build jobs passed. The launch helper is
still narrower than the two-window product requirement.

A separate opt-in `independent-window-native` fixture now opens two actual
Editor/Application instances in separate processes. It invokes the first
editor's New Window command while the first document is dirty and selected,
checks preservation and the child's blank initial state, copies selected Unicode text
through the native clipboard to the child, closes the first GUI, and verifies
the child retains the pasted text and accepts another edit after shutdown.
Clipboard transfer occurs while its source window is alive; persistence after
source shutdown depends on the desktop clipboard service and is not asserted
by this fixture. Both use the normal nine-window topology and stop their owned
timers before closing. The fixture also records a single New Window command
return-time observation, excluding later child GUI startup; it is not a latency
distribution or a threshold gate. Packaged macOS activation/placement and
keyboard interaction remain separate dogfood checks.

The two-editor lifecycle fixture without clipboard transfer passed on all three
native platforms at a7390b8, run 36886655054 (macOS 3.70 seconds, Linux 0.41
seconds). The separate core run 36886655139 also passed.

ASCII clipboard transfer passed on all three platforms at a011391, native run
36887223711: macOS 3.25 seconds, Windows 0.64 seconds, Linux 0.48 seconds for
the complete two-editor lifecycle fixture. Raw logs are retained locally under
.build/new-window-a011391.

The Unicode fixture passed on all three platforms at `7518073`, native run
36887657481 and core 36887657412. It transfers an accented character, a space
and an emoji exactly, retains them after parent GUI shutdown and then edits the
child. Single command-return observations were 17.8737 ms Mac, 1.169 ms Windows
and 2.08374 ms Linux. These include the fixture's self-path lookup and exclude
later child GUI startup; they are not percentile distributions or a performance
pass threshold. Raw logs are under .build/new-window-7518073. Production launch
and clipboard code is unchanged from the published a011391 package.
Local compilation and the 101-file spelling audit pass.

The Mac branch closes each editor through a routed Cmd+W key event, then checks
the other editor's continued lifetime. This passed all three native jobs in
run36890043148 at b4b6462. The later c968ae8 fixture also routes Copy/Paste through
the focused document: Meta+C/V on Mac, Control+C/V elsewhere. All three native
jobs36946000203 and core36946000230 passed. The Mac raw log confirms the complete
Unicode clipboard/lifetime fixture passed in4.31s. This covers toolkit event
routing, not physical keyboard translation. Raw evidence is retained under
.build/native-c968ae8-evidence/mac.
