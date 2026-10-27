# Status bar depth

The owner requested visible depth in the status bar. Inspection of `before.png`
confirmed that the earlier two-tone top edge still left the status content
looking flat. The revised painting adds a complete inset border: shadow on the
top/left and highlight on the bottom/right, with the text inset inside it. It
uses existing theme colors and adds no timers, subscriptions or frame requests.

`before.png` is the unmodified Mac AppKit view capture from source c7b81ed,
native run 36981694678, artifact `native-evidence-macos-arm64`, original path
`native-visual-editor-source-status-3238/view.png`. That run passed all three
platforms. This is a view redraw, not a compositor screenshot.

Revised source 73ddcf5: local Windows build passed; editor/views tests passed
2/2 in 2.06 seconds; the 129-file spelling audit passed. Native run 36982556378
passed Windows, macOS and Linux tests and packaging. `after.png` is its
unmodified `native-evidence-macos-arm64` capture from
`native-visual-editor-source-status-3990/view.png`. Inspection confirms the
complete inset border and readable text inside it. The captures have different
fixture filenames; they are not a pixel-equality test. No physical-desktop
latency conclusion follows from the images. Filename-row removal and pointer
menu focus remain separate changes awaiting the reviewed provider SDK.
