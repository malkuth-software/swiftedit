# Development limitations

The expanded owner objectives and feature-by-feature implementation status are
in SWIFTEDIT_OBJECTIVES.md. The CLI command protocol and conventional Windows
terminal share the byte-faithful session; the GUI has not yet migrated to it.
CSV now has a native table view, formula entry/results and Convert to Value in
the menu and context menu. Its source still shares the current GUI text-widget
limits; broad desktop and responsiveness validation remain in progress. Grid
paste inserts plain clipboard text into the selected cell; rectangular clipboard
import and column resizing are not implemented.

- Release v0.3.3 has validated Windows x64, macOS ARM64 and Linux x64 packages.
  Each platform package includes the GUI, interactive terminal and CLI. Native
  terminal and application tests run on all three systems; current development
  revisions still require their own CI and packaging evidence.
- The current GUI.Forms multiline provider has a 1 MiB UTF-8 document limit and
  4096-byte logical-line limit. Files beyond either limit are refused intact.
  This is a toolkit development bound, not a final product decision.
- Plain-text GUI scrolling currently uses the mouse wheel and caret reveal,
  without visible scrollbars. Markdown and CSV have their own scrollbars.
  Tabs use four-space stops. Home/End address the visual row;
  Ctrl+Home/End address the document. Full bidi visual caret navigation, IME
  composition, accessibility text ranges and native screen-reader behavior
  require further provider validation.
- Font selection currently chooses a bundled toolkit role, size, bold and italic;
  it is not an arbitrary installed-font-family chooser.
- Case-insensitive search folds only ASCII A-Z. Literal non-ASCII text searches
  exactly. Query characters can be flagged as one-grapheme wildcards by right-click.
  Normalized Ctrl+? key events are covered by headless tests; the owned AppKit
  synthetic Control+Shift+/ path passed in run 36981694678. This does not establish
  physical keyboard delivery across platforms. Find Next and Replace All yield and
  cancel stale work; snapshot/final-publication latency still needs measurement.
  No regex, whole-word
  or selection-only modes.
- Character Inspector and separate Unicode/control picker dialogs are implemented.
  The current document TextBox still lacks the required visible-control glyph view;
  picker previews are inert, but full document presentation needs provider integration.
  NUL cannot be copied through the null-terminated Windows text clipboard; its
  picker Copy action refuses intact while Insert preserves the literal byte.
- No printing/page setup, syntax
  colors, settings persistence, recovery files, installer, file association or
  OS-default changes.
- External changes are checked during saving. There is no filesystem watcher,
  automatic reload or merge view. A conflict opens Save Over/New Copy, then editable
  filename/encoding/endings and a reviewed destination warning before explicit Save.
  Any intervening destination change is refused; there is no unchecked force-save.
- Save As may ask for overwrite twice: the picker confirms its observation;
  the writer obtains fresh byte/identity evidence and confirms that destination
  before writing. This conservative behavior avoids silently treating an old
  picker observation as a durable reservation.
- The save adapter refuses hard links, reparse paths and alternate stream paths.
  Existing security/stream metadata uses Windows ReplaceFile behavior, but a
  broad ACL/ADS/encryption/volume-failure corpus has not been measured. Power-loss
  recovery and the final name-replacement race are not eliminated.
- File Manager picker enumeration remains synchronous. Very large or slow
  directories may block that owned dialog; this is provider-owned work.
- Development source now uses the reviewed aaca5d0 SDK for dynamic document
  titles and pointer/keyboard menu opening. The filename row is removed and its
  space returned to the document. Consumer native tests and packaging passed on
  all three systems in run 36985848443; Mac captures were inspected. These fixes
  are published in v0.3.3; v0.3.2 retains the old row/menu focus. Tag run
  36986875922 passed all three native jobs and publication with nine assets.

The owner interview now provides direction. These gaps remain implementation
work, not claims of a finished Malkuth release.
