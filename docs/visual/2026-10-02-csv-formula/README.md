# Native CSV formula integration

Source: 6daa15e41b8369c758934a044f6a85617b920b96.
Run: 36982228372, macOS job 110759085823.
Artifact: `native-evidence-macos-arm64`.
Original capture: `native-visual-editor-csv-formula-11216/view.png`.

All 32 Mac tests passed in 63.08 seconds before a newer source push cancelled
the job during packaging. The overall cancelled workflow is not a successful
cross-platform packaging gate. Linux completed successfully; Windows was
cancelled. The newer 73ddcf5 run retains these tests.

Visual inspection of the unmodified `formula.png` confirms B1's source entry
shows `=A1*4` while B1 in the grid displays `8`, with A1 containing `2`. Text
and selection fit the cells. The unsaved marker is visible. This capture uses
the older status-bar edge and duplicate filename row, not the pending polish.

The native editor test verifies Window-dispatched Enter, the declared CSV menu
command, the cell context command, Undo restoring the formula after either
conversion, exact formula bytes on Save, and clean shutdown. The capture occurs
after bounded formula calculation readiness and before conversion.

Limits: synthetic framework input and semantic command invocation exercise the
native-window application wiring; they do not prove physical pointer/keyboard
delivery. The image is an AppKit view redraw, not a compositor screenshot.
It is not a complete CSV desktop or responsiveness acceptance test.
