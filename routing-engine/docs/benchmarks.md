# Benchmark method and executed measurements

Release C++17, GCC 16.2.0 / Windows UCRT64, local RoutingKit revision recorded in
dependencies.md. Seed 20261025. Alternating directed streets, two-way crossings,
and occasional parallel arcs. Both datasets are **synthetic**, not NYC.
Numbers are observed single runs and not latency guarantees; CPU model was not
retrieved due host inventory permissions. Timings use steady_clock. Checksums
and 20 Dijkstra comparisons per dataset guard benchmark query correctness.

| Measurement | 2,500 nodes / 7,494 arcs | 22,500 nodes / 68,364 arcs |
|---|---:|---:|
| Input generation including CSR, ms | 1.802 | 22.838 |
| Separate CSR rebuild, ms | 0.394 | 4.190 |
| Physical metric preparation, ms | 0.659 | 7.518 |
| Weight conversion, ms | 0.473 | 4.308 |
| Nested-dissection order, ms | 38.854 | 481.171 |
| Hierarchy construction, ms | 4.586 | 58.677 |
| Metric customization, ms | 3.470 | 125.445 |
| Fresh raw query mean, us | 29.160 | 302.628 |
| Reused raw query mean, us | 29.138 | 293.058 |
| Validated adapter query mean, us | 28.227 | 265.716 |
| Identical metric update, ms | 0.015 | 0.053 |
| Changed metric update, ms | 4.682 | 103.133 |
| Independent metric prepare, ms | 4.062 | 92.798 |
| Metric pointer publication, us | 54.5 | 40.1 |
| Three uncached preferences, ms | 11.475 | 288.564 |
| Five uncached preferences, ms | 19.300 | 515.593 |
| Shared adapter, 4 callers, queries/s | 24,925 | 3,694 |
| Independent adapters, 4 workers, queries/s | 89,040 | 11,932 |
| Whole benchmark peak resident bytes | 13,987,840 | 98,603,008 |
| Hierarchy vectors lower bound, bytes | 904,744 | 12,728,264 |
| Per metric/query estimate, bytes | 462,513 | 6,454,793 |

Queries per dataset: 1,000 and 2,000 respectively. Memory estimates exclude
allocator overhead and some RoutingKit indexes; peak RSS includes all benchmark
comparison objects at once, not a standalone production engine. Pointer
publication measurement is a prepared CCH metric pointer exchange, not full
SnapshotManager physical-profile preparation. Report names preserve that
distinction. Loaded-snapshot timing is available by adding a snapshot directory
as fourth argument; no real snapshot was supplied.

Reuse gave almost no raw-query benefit on the smaller fixture and about 3% on
the larger one in these runs. Do not infer that query-buffer reuse alone solved
latency. Customization is the dominant cost: bounded objective reuse and shared
topology avoid repeated work. Separate adapters improve concurrent throughput
but consume another metric/query allocation; default engine caps remain small.
Five preferences took substantially longer, so neighbor prefetch remains off.

Public Python worker benchmark, 200 repeated identical requests on the five-node
fixture, includes pipe IO and JSON parsing/serialization. Cache-disabled mean:
0.240ms; enabled mean: 0.239ms. P95: 0.378ms vs 0.364ms. This small difference is
not evidence of a production speedup; the fixture is too small and process/IO
noise dominates. Startup observations (163ms then 43ms) include cold filesystem
effects and must not be attributed to cache configuration. Cold route times
were approximately 0.605ms and 0.578ms.

Raw results: benchmark_results.json. Reproduce using the commands in validation.md.
Real network memory/latency, full profile update costs at NYC scale, repeated
trials and percentile distributions remain deployment work.