# Session document-page producer measurements

Recorded 2026-10-02T06:54:14Z on Windows x64, AMD EPYC 9354, GCC 16.2.0
(MSYS2 Rev4), CMake Release (`-O3 -DNDEBUG`). Public GUI.Forms SDK:
723cd7f9f8016d854f667f41e0c5dbcbe4cc0adf. Application source:
452e0b029dc44d8aaf7714157c6c2fd07349ae3c, plus this benchmark and its CMake target.
The benchmark does not change the production producer.

Executable SHA256:
`709908625868b647b0f846fc40e053fa7b0d013cbe8835c548ffc3e5f276ca86`.

Three 65,536-byte fixtures contain ASCII `a`, NUL controls, or illegal UTF-8
bytes `FF`. Each is opened into an editable resident Session before timing.
Every trial constructs a fresh producer, performs eight 8 KiB reads, projects
the complete source and publishes its page. Output display bytes and mapping
count are compared with the existing DisplayPage implementation outside timing;
source equality and unchanged dirty state are also checked. The separate
document-projection suite verifies installed-SDK acceptance and mapping cases.

Each fixture has five unrecorded warmup trials and 30 recorded trials.
`samples.csv` contains all 990 recorded stage samples; no outlier is removed.
Percentiles use nearest ranks. With 30 samples, p99 is the maximum.

| Fixture | Projection p50 ms | Projection p95 ms | Projection p99 / max ms | Worst read step ms | Worst construction ms | Worst publication ms |
|---|---:|---:|---:|---:|---:|---:|
| ASCII | 4.8678 | 6.3921 | 7.1728 | 0.0237 | 0.0098 | 0.0003 |
| NUL controls | 5.4282 | 6.3799 | 6.5230 | 0.0947 | 0.0158 | 0.0002 |
| Illegal bytes | 5.7291 | 6.5512 | 6.6787 | 0.0691 | 0.0097 | 0.0003 |

Construction timing includes request validation and source-buffer reservation.
Read timing includes copying from the resident Session. Projection includes
segmentation, visible labels, map construction and disposal of temporary source
and display storage before return. Publication includes the revision check and
ownership handoff. Fixture creation/open, reference projection, correctness
checks and CSV writes are outside the measured operations.

These timings exclude slow storage, native shaping, rasterization, GUI input
delivery and presentation. This is not a test of an integrated editor or a
latency guarantee. The producer's 64 KiB D1 source budget differs from the
unexported prepared-window renderer's tighter display/metadata admission; these
fixtures do not prove renderer admission or long-line support. Scheduling and
cache state are uncontrolled. The final projection still runs as one bounded
step; the observed results justify retaining it for this stage, not claiming a
hard upper bound on every machine.

```powershell
cmake --build .build/swiftedit-sdk-723cd7f --target swiftedit-document-projection-bench --parallel 2
.build/swiftedit-sdk-723cd7f/swiftedit-document-projection-bench.exe samples.csv
```

Use the same compiler and installed-SDK runtime search paths as the headless
test suite. The executable creates and removes only its own unique temporary
fixture directory.
