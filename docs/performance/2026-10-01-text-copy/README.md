# Streaming text-copy latency

Three 16 MiB fixtures, three trials each, measured on the local Windows host.
Every emitted copy was compared byte for byte with whole-source text_copy;
cancellation after 32 steps left no destination and preserved source revision.
Raw observations, executable/source hashes and method are retained alongside.

The 768 step samples per fixture had p99 wall times 0.4358 ms (ASCII),
0.2993 ms (Unicode), and 0.3076 ms (invalid bytes). Their worst observations were
0.4966, 0.4437, and 0.3789 ms respectively. Publication including final flush
was much slower: worst 44.2431, 62.3312, and 57.8577 ms respectively. This
identifies a remaining synchronous terminal pause; the feature is not yet
accepted as fully responsive. Publication p95/p99 equal the maximum because
there are only three samples per fixture, not because the tail is established.

Preparation worst was 1.0133 ms; cancellation worst was 0.2702 ms. No hard
latency bound follows from these uncontrolled local observations. OS flush and
publication should be moved off the interactive thread with explicit lifetime,
cancellation-before-publication and completion ownership. Cross-platform
measurements and interaction latency remain pending.


## Asynchronous publication follow-up

The terminal now transfers a fully prepared writer to one owned publication
thread after final source validation. It waits on a condition variable for at
most a requested 8 ms only while this operation is active, checks input between
waits, and collects the result when complete. Cancellation ends at publication
start. Ctrl+X defers exit until success; failures leave the terminal open. Worker
destruction joins outstanding work, and no worker borrows Session or terminal
state. There is no idle publication timer or detached thread.

The retained async-local run measured dispatch worst 0.2441 ms and completion
collection worst 0.1019 ms across nine copies. Total publication still reached
49.5776 ms. This is evidence that dispatch/collection avoid the earlier blocking
flush in this run, not proof that disk IO became faster. The runs were not paired;
background load and caches were uncontrolled. Step worst reached 1.0518 ms.
Only three publication samples per fixture were taken. Physical input latency,
native Mac/Linux timing and the full application lag audit remain pending.

The native build workflow now runs this benchmark on Windows, macOS and Linux,
retaining summary.txt, receipt.json and fixtures/samples.csv under
text-copy-evidence. Large source/copy fixtures are not uploaded. First native
measurements from this wiring are pending; local numbers above remain local.
