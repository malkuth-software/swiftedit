# Focused Mac CPU trace: preliminary sample inspection

The existing provider recording was exported successfully by SwiftEdit workflow
[36967715119](https://github.com/falseywinchnet/swiftedit/actions/runs/36967715119).
The artifact `focused-cpu-samples-36967715119` contains seven XML tables, export
logs and a receipt with each table's SHA-256. This is analysis of the existing
provider run 36963650387, attempt 076b0fda4a7943d1b97fffc20b12c0d8; no new
application workload was run. The exporter is `tools/Export-Mac-Profile.py`.

The focused workload completed its 120.084828291-second interval. Its process
clock reported 0.4496546380459528 percent of one core. This was the toolkit test
workload on a hosted Mac, not SwiftEdit on the owner's MacBook. Both cleared
workloads remain invalid, and comparison_accepted remains false.

The exported PointsOfInterest table contains one CpuProfileHold End event at
119022297500 ns, PID 35723, signpost ID 13, matching the workload receipt. No
Begin event is present. Consequently the complete interval cannot be selected
using a captured begin/end pair. Do not claim a complete aligned comparison.

Preliminary counting procedure: parse time-profile.xml; resolve every XML `ref`
against its document-wide `id`; retain rows for PID 35723, state Running and
sample-time strictly before the matching End timestamp. Resolve tagged-backtrace,
backtrace and frame references before counting names. Count a host frame at most
once per stack. This subset contains 510 samples, each with weight 1000000 ns.
The whole time-profile table has 549 rows; time-sample has 557 rows. These two
tables are alternative representations and must not be added together.

| Thread | Selected samples |
|---|---:|
| Main, 0x16420 | 308 |
| 0x1642f | 85 |
| 0x16424 | 73 |
| 0x1642b | 43 |
| 0x16425 | 1 |

| Inclusive host frame | Selected stacks containing it |
|---|---:|
| GUIFormsView drawRect: | 69 |
| GUIFormsView drawRetainedRect: | 69 |
| GUIFormsView collectDamage | 25 |
| GUIFormsView scheduledWake | 22 |
| GUIFormsView armWakeTimer | 5 |
| GUIFormsView startDisplayLinkIfNeeded | 1 |

Inclusive counts overlap. They are neither exclusive CPU time nor event counts.
The most frequent leaf symbol is mach_msg2_trap (116); syscall names in running
samples must not be interpreted as measured blocked time. Some exported names
are deduplicated symbols, so exhaustive symbol resolution is not established.
510 samples exceed the recording's 200-sample inspectability guard, but that
guard is explicitly not a statistical confidence criterion.

This establishes inspectable stacks involving retained drawing and scheduled
wakes in the focused recording. It does not establish the cause of the owner's
7% CPU observation, a complete focused/cleared comparison, or a performance fix.
