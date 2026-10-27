# GUI polish capture receipt

Source: 497f77a037e3d3a0cd3c0dcb8f6f97653bd5609f.
Native run: https://github.com/falseywinchnet/swiftedit/actions/runs/36985848443.
All three native platform jobs and packaging passed.

`markdown-menu.png` is the owned Mac content capture from
`native-visual-editor-markdown-menu-4485/view.png`. Inspection confirms a check
beside Markdown Rendered View and Status Bar, no selected popup row after
pointer opening, and no duplicate filename row.

`source-status.png` is from `native-visual-editor-source-status-4485/view.png`.
Inspection confirms source begins immediately below the menu and the inset
status border fits its text. These are rendered content captures, not physical
screen/input observations. Native chrome is excluded; exact title transitions
are covered separately by the native test's AppKit window lookup assertions.
