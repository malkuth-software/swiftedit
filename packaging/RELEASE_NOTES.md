SwiftEdit uses the standalone GUI.Forms toolkit and shared LLVM 22 build system.
Code pushes publish uniquely versioned packages after every native platform and
installer check passes. The version is selected before compilation and matches
the application, installers and package manifests.

The plain-text document view now shows draggable scrollbars when its content
exceeds the available height or width. Word Wrap removes horizontal scrolling;
wheel scrolling and caret movement stay synchronized with the visible bars.

The expanded feature set is still in development. The GUI currently uses its
existing 1 MiB UTF-8 / 4096-byte logical-line path; large-file operations are
available in the terminal. Large-file GUI editing, native printing and the
complete physical responsiveness audit remain outstanding.
