# SwiftEdit repository instructions

Current owner direction authorizes implementation now; historical Text Editor
interview-first gates are not a stop condition for this standalone repository.
Read docs/SWIFTEDIT_OBJECTIVES.md before changing text/save semantics. It records
the owner's 2026-09-30 interview and supersedes conflicting provisional choices
in historical docs/DECISIONS.md. Assistant proposals are not owner decisions.

Work in this repository with explicit working directories. No workers/subagents
or worktrees. Do not modify Plan Paint or File Manager/GUI.Forms source without
coordinated provider ownership. Consume installed public packages only. Preserve
the one-document, traditional-menu, owned-dialog topology. No browser runtime,
ribbon, tabs, IDE or sidebar. Tests mutate only their unique disposable fixtures.

Build and headless tests are authorized. Coordinate desktop launch/automation
because the owner is dogfooding other applications. Do not publish remotely
without checking the destination. Record measured test evidence honestly.

All authored source, tests and tooling must follow docs/PROGRAMMING_HOUSE_STYLE.md.
Read that exact owner-supplied style before edits. Audit semantic ownership, failure,
allocation and callback rules as well as spelling. The house-style correction
is complete; the owner's 2026-10-01 direction resumes unfinished implementation
and assigns this chat ownership of the repository. Preserve frozen SDKs and
previous published stages.
