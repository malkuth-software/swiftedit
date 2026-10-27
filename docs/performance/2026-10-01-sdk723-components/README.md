# SDK723 component observations

Windows Release, installed SDK723cd7f, local .build/swiftedit-sdk-723cd7f.
Benchmarked application code is unchanged from c968ae8; subsequent changes
through17025dc concern fixtures, SDK pin and documentation. Benchmarks ran
sequentially, with their existing correctness assertions and warmup exclusions.
No desktop host was launched. These measurements exclude native presentation,
host scheduling waits and physical input latency; they are not a full lag scan.

| Component | samples | p50 ms | p95 ms | p99 / worst ms |
|---|---:|---:|---:|---:|
| Caret status,266240 bytes |1000|0.0023|0.0025|0.0043 /0.0113|
| Repeated CSV formulas,876543 bytes |31|1.4554|2.1218|2.1346|
| Distinct CSV formulas,657927 bytes,total completion |31|44.1149|66.808|99.334|
| Distinct CSV initial call |31|2.3196|2.6318|2.6445|
| Distinct CSV worst processing slice per sample |31|2.5691|3.9613|5.0638|

Distinct computation yields across19 median slices,26 p95 and36 maximum. Total
completion time is not one uninterrupted UI stall. One formula remains
synchronous under its existing bounds; the2ms budget is checked between formulas,
not a hard intra-formula deadline. Worst observed slice5.0638ms is above the
previous2.6716ms observation. Host load and repeatability were not controlled,
so neither improvement nor SDK-caused regression is established. Actual Mac
focused blank CPU remains separately unresolved. Raw sample CSVs are adjacent.
