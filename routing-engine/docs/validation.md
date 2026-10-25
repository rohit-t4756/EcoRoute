# Validation and milestone gates — 2026-10-25

## Status

- **Milestone 1: Complete** for the documented standalone C++ v1 engine.
- **Milestone 2: Complete** for the documented v1 correctness, concurrency,
  optimization and synthetic-performance gates on the tested Windows toolchain.
  This is not a production-city, scientific-calibration or cross-platform claim.
- **Milestone 3: Partial**. Exporter → C++ → Python integration is executed.
  Real PostGIS/ML/Redis/FastAPI/React and gRPC deployment remain blocked or NOT RUN.

Explicit v1 limits and external follow-up criteria are retained in backlog.md,
model_review.md, architecture.md and dependencies.md. None are removed simply
because the regression suite passes.

## Executed evidence

| Check | Result |
|---|---|
| Original existing Debug build / 13 suites | PASS, 2.14s |
| Original separate clean Debug configuration / 13 suites | PASS, 1.84s |
| Integrated Release configure and build | PASS |
| Final clean-first Release rebuild, 60 build actions | PASS, no emitted compiler warnings |
| Final full CTest regression / 20 suites | PASS, 3.18s |
| CLI fixture, actual public engine | PASS, 2 distinct paths, exact selected recommendation |
| Four vehicle categories, stale/invalid/no-route/self requests | PASS |
| CCH versus independent integer Dijkstra, randomized/long-limit cases | PASS |
| Concurrent metric query/update, immutable bundle retention | PASS |
| Dynamic traffic changes fastest path and invalidates response cache | PASS |
| Graph v2 exporter → loader → CLI, uint64 IDs and geometry | PASS |
| Preprocessed order reuse and graph-version mismatch | PASS |
| Python persistent worker client/lifecycle | PASS |
| Python syntax compilation including optional FastAPI module | PASS |
| Empirical vehicle calibration, real NYC benchmark | NOT RUN; datasets not supplied |
| Actual HTTP/FastAPI, gRPC, PostgreSQL, Redis, ML/React | BLOCKED / NOT RUN; see dependency matrix |
| ASan/UBSan/TSan, Linux/MSVC execution | NOT RUN; installed toolchain/runtime availability limits |

The first integrated regression passed 19 suites and failed ExporterTests due
to Windows sandbox temporary-directory ACL denial before exporter logic ran.
The exporter passed outside the sandbox. Final all-20 regression was then run
outside that restriction. The failure was an environment issue; no acceptance
expectations were changed to force a pass.

Final registered suites: ResultTests, TypesTests, CCHTests, GraphTests,
RoutingGraphTests, DijkstraTests, CCHAdapterTests, CCHOrderTests, CostTests,
WeightBuilderTests, ObjectiveRoutingTests, AlternativeRoutesTests,
GraphLoaderTests, TrafficSnapshotTests, SnapperTests, RuntimeTests, EngineTests,
ServiceContractTests, ExporterTests, CliIntegrationTests.

## Milestone gate interpretation

M1 now includes a loadable graph, traffic/profile metric preparation, indexed
snapping, public Engine, route evaluation, three essential objective roles,
deduplication, JSON geometry, public errors and runnable CLI. Existing RoutingKit
and Dijkstra code paths remain in use.

M2 adds topology sharing, reusable synchronized query state, strong metric-update
publication, bounded objective/response caches, path-safe scale selection and
explicit rounding diagnostics. Mathematical/model limitations are justified and
recorded rather than hidden behind tests. Benchmarks cover preprocessing,
customization, queries, alternative batches, metric publication, throughput,
memory and public worker lifecycle. The accepted nearest-node and additive
energy-model v1 limitations remain material for real deployment.

M3 has executable canonical graph/traffic contracts, an optional read-only
PostGIS adapter, an executed local Python bridge, an unexecuted FastAPI example
and a proposed proto. No unknown teammate compatibility or live service success
is claimed.

## Reproduction and saved evidence

```text
cmake -S . -B build/integration -G Ninja -DCMAKE_BUILD_TYPE=Release -DECOROUTE_ROUTINGKIT_ROOT=<prefix>
cmake --build build/integration --clean-first -j 4
ctest --test-dir build/integration --output-on-failure
build/integration/ecoroute_bench 50 1000 4
build/integration/ecoroute_bench 150 2000 4
python benchmarks/bench_pipeline.py build/integration/ecoroute_cli config/engine.example.conf 200
```

CTest log: build/integration/Testing/Temporary/LastTest.log.
Baseline source/manifest: build/audit-baseline/. Source inventory and exact
change ledger are in docs; benchmark measurements are in benchmark_results.json.
No Git repository exists in this checkout, so final Git diff/status review was
replaced by file hash comparison and a saved unified source diff. No credentials
were added and no remote push/history operation was performed.