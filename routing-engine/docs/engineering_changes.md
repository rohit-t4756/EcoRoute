# Engineering changes — exact source ledger

2026-10-25. Baseline: build/audit-baseline/manifest.json and source/. No Git metadata exists; no push/commit/history rewrite occurred. Generated build products/logs/temporary data/backups are excluded. Deleted originals are preserved.

Significant corrections: unguarded mutable CCH state became shared immutable topology with synchronized reusable queries and prepared replacement metrics; per-edge-only limits gained a graph-safe path budget and explicit quantization error; epsilon preferences became exact; scores gained objective association; invalid EV efficiency/directed geometry now fail; complete traffic/profile bundles publish atomically and caches retain exact snapshot identity. The full formulas, verification and limitations are in architecture.md, model_review.md and validation.md.

Milestone gates: M1 Complete; M2 Complete for documented v1 on the tested platform; M3 Partial. Evidence: final clean build and 20 suites PASS; measured synthetic benchmarks. External services, calibration, real NYC scale and untested platforms remain explicit backlog items.

## Batch 0 Build

**File:** `.clang-format`  
**Action:** Modified  
**Reason:** Complete or harden the 0 Build scope.  
**Details:** Replaced placeholder with LLVM style, four-space indentation, 100 columns and preserved include order.  
**Compatibility:** No mass reformat.  
**Tests:** Configuration review.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `.gitignore`  
**Action:** Modified  
**Reason:** Complete or harden the 0 Build scope.  
**Details:** Removed pasted prose/UI markup; retained generated/local/private ignores; added CMakeUserPresets and Python artifacts.  
**Compatibility:** Ignore rules corrected; no source data removed.  
**Tests:** Source inventory/reference review.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `CMakeLists.txt`  
**Action:** Modified  
**Reason:** Complete or harden the 0 Build scope.  
**Details:** Removed duplicate alternatives entry and empty graph_io target input; consolidated test helpers; added real CLI/preprocessor/benchmark and runtime configuration/JSON/order modules, 20 CTest registrations, optional Python checks, warning flags, sanitizer opt-in and portable Debug/Release presets. RoutingKit discovery uses supplied prefix/environment and transitive ZLIB/OpenMP/Threads.  
**Compatibility:** C++17 and existing CTest names retained; alternatives test executable name standardized.  
**Tests:** Clean configure, final clean-first 60-action rebuild and 20 suites PASS; no emitted warnings.  
**Remaining issues:** Linux/MSVC and sanitizer runtimes not verified; dependencies.md.

**File:** `CMakePresets.json`  
**Action:** Modified  
**Reason:** Complete or harden the 0 Build scope.  
**Details:** Removed duplicate alternatives entry and empty graph_io target input; consolidated test helpers; added real CLI/preprocessor/benchmark and runtime configuration/JSON/order modules, 20 CTest registrations, optional Python checks, warning flags, sanitizer opt-in and portable Debug/Release presets. RoutingKit discovery uses supplied prefix/environment and transitive ZLIB/OpenMP/Threads.  
**Compatibility:** C++17 and existing CTest names retained; alternatives test executable name standardized.  
**Tests:** Clean configure, final clean-first 60-action rebuild and 20 suites PASS; no emitted warnings.  
**Remaining issues:** Linux/MSVC and sanitizer runtimes not verified; dependencies.md.

## Batch 1/2/3 Documentation

**File:** `README.md`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2/3 Documentation scope.  
**Details:** Replaces placeholders with actual build/use, lifecycle/cache/scaling/snapping, graph/traffic/config contracts, CLI/Python/API examples and honest integration limitations.  
**Compatibility:** Documentation describes implemented behavior.  
**Tests:** Executed commands/contracts checked; external claims labeled NOT RUN.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/api.md`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2/3 Documentation scope.  
**Details:** Replaces placeholders with actual build/use, lifecycle/cache/scaling/snapping, graph/traffic/config contracts, CLI/Python/API examples and honest integration limitations.  
**Compatibility:** Documentation describes implemented behavior.  
**Tests:** Executed commands/contracts checked; external claims labeled NOT RUN.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/architecture.md`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2/3 Documentation scope.  
**Details:** Replaces placeholders with actual build/use, lifecycle/cache/scaling/snapping, graph/traffic/config contracts, CLI/Python/API examples and honest integration limitations.  
**Compatibility:** Documentation describes implemented behavior.  
**Tests:** Executed commands/contracts checked; external claims labeled NOT RUN.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/data_contract.md`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2/3 Documentation scope.  
**Details:** Replaces placeholders with actual build/use, lifecycle/cache/scaling/snapping, graph/traffic/config contracts, CLI/Python/API examples and honest integration limitations.  
**Compatibility:** Documentation describes implemented behavior.  
**Tests:** Executed commands/contracts checked; external claims labeled NOT RUN.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 2 Persistence

**File:** `apps/preprocess_main.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Persistence scope.  
**Details:** Added versioned nested-dissection order save/load and real preprocessor. Binds graph version, CSR ordering/IDs and coordinates with canonical fingerprint; validates permutation, rejects mismatches and existing destination.  
**Compatibility:** cch_topology_file denotes an order cache, not a serialized hierarchy.  
**Tests:** CLI order reuse and mismatch checks PASS.  
**Remaining issues:** Full hierarchy persistence deferred; fingerprint is not authentication; do not concurrently write one cache path.

**File:** `src/runtime/order_cache.cpp`  
**Action:** Created  
**Reason:** Complete or harden the 2 Persistence scope.  
**Details:** Added versioned nested-dissection order save/load and real preprocessor. Binds graph version, CSR ordering/IDs and coordinates with canonical fingerprint; validates permutation, rejects mismatches and existing destination.  
**Compatibility:** cch_topology_file denotes an order cache, not a serialized hierarchy.  
**Tests:** CLI order reuse and mismatch checks PASS.  
**Remaining issues:** Full hierarchy persistence deferred; fingerprint is not authentication; do not concurrently write one cache path.

**File:** `src/runtime/order_cache.hpp`  
**Action:** Created  
**Reason:** Complete or harden the 2 Persistence scope.  
**Details:** Added versioned nested-dissection order save/load and real preprocessor. Binds graph version, CSR ordering/IDs and coordinates with canonical fingerprint; validates permutation, rejects mismatches and existing destination.  
**Compatibility:** cch_topology_file denotes an order cache, not a serialized hierarchy.  
**Tests:** CLI order reuse and mismatch checks PASS.  
**Remaining issues:** Full hierarchy persistence deferred; fingerprint is not authentication; do not concurrently write one cache path.

## Batch 1/3 CLI

**File:** `apps/route_cli.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/3 CLI scope.  
**Details:** Implements actual configured external-ID/coordinate routing, strict argument parsing, JSON results/errors, health, persistent 64KiB-bounded command worker, traffic updates and EOF/quit lifecycle.  
**Compatibility:** New executable interface; no shell/service dependency.  
**Tests:** CLI/worker/exporter integration PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 0/3 Cleanup

**File:** `apps/server_main.cpp`  
**Action:** Deleted  
**Reason:** Complete or harden the 0/3 Cleanup scope.  
**Details:** Deletes comment-only server sources; real persistent worker is route_cli; proposed proto retained.  
**Compatibility:** No working server removed.  
**Tests:** Build/reference scan PASS.  
**Remaining issues:** gRPC server intentionally pending agreement.

**File:** `service/grpc_service.cpp`  
**Action:** Deleted  
**Reason:** Complete or harden the 0/3 Cleanup scope.  
**Details:** Deletes comment-only server sources; real persistent worker is route_cli; proposed proto retained.  
**Compatibility:** No working server removed.  
**Tests:** Build/reference scan PASS.  
**Remaining issues:** gRPC server intentionally pending agreement.

## Batch 2/3 Benchmark

**File:** `benchmarks/bench_pipeline.py`  
**Action:** Created  
**Reason:** Complete or harden the 2/3 Benchmark scope.  
**Details:** Adds public Python-worker startup/cold/warm and enabled/disabled-cache comparison with identical response assertion.  
**Compatibility:** New optional script.  
**Tests:** 200 requests per mode executed.  
**Remaining issues:** Tiny fixture has negligible speed difference; no production claim.

## Batch 2 Benchmark

**File:** `benchmarks/bench_routes.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Benchmark scope.  
**Details:** Replaces placeholder with seeded directed-grid or supplied-snapshot timings for input/CSR/physical/weights/order/hierarchy/customization/query reuse/update/clone/publication/three-five objectives/concurrency/RSS and Dijkstra checks.  
**Compatibility:** New optional executable.  
**Tests:** Two sizes executed; raw measurements in benchmark_results.json.  
**Remaining issues:** Synthetic single runs; estimates/publication measurement scope labeled.

## Batch 1 Configuration assets

**File:** `config/engine.example.conf`  
**Action:** Created  
**Reason:** Complete or harden the 1 Configuration assets scope.  
**Details:** Replaces empty planned YAML with runnable strict settings/relative fixture paths and petrol/diesel/hybrid/electric CSV demo profiles.  
**Compatibility:** Format migration explicit.  
**Tests:** Engine/CLI configuration flows PASS.  
**Remaining issues:** All demo consumption/emissions inputs synthetic, not validated specifications.

**File:** `config/vehicle_profiles.example.csv`  
**Action:** Created  
**Reason:** Complete or harden the 1 Configuration assets scope.  
**Details:** Replaces empty planned YAML with runnable strict settings/relative fixture paths and petrol/diesel/hybrid/electric CSV demo profiles.  
**Compatibility:** Format migration explicit.  
**Tests:** Engine/CLI configuration flows PASS.  
**Remaining issues:** All demo consumption/emissions inputs synthetic, not validated specifications.

## Batch 0/1 Asset replacement

**File:** `config/engine.example.yaml`  
**Action:** Deleted  
**Reason:** Complete or harden the 0/1 Asset replacement scope.  
**Details:** Deletes empty/comment-only assets after adding runnable configuration/versioned fixture replacements. Originals remain in baseline backup.  
**Compatibility:** Explicit format/directory migration.  
**Tests:** Replacement integration PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `config/vehicle_profiles.example.yaml`  
**Action:** Deleted  
**Reason:** Complete or harden the 0/1 Asset replacement scope.  
**Details:** Deletes empty/comment-only assets after adding runnable configuration/versioned fixture replacements. Originals remain in baseline backup.  
**Compatibility:** Explicit format/directory migration.  
**Tests:** Replacement integration PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `fixtures/toy_network.csv`  
**Action:** Deleted  
**Reason:** Complete or harden the 0/1 Asset replacement scope.  
**Details:** Deletes empty/comment-only assets after adding runnable configuration/versioned fixture replacements. Originals remain in baseline backup.  
**Compatibility:** Explicit format/directory migration.  
**Tests:** Replacement integration PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `fixtures/toy_traffic.csv`  
**Action:** Deleted  
**Reason:** Complete or harden the 0/1 Asset replacement scope.  
**Details:** Deletes empty/comment-only assets after adding runnable configuration/versioned fixture replacements. Originals remain in baseline backup.  
**Compatibility:** Explicit format/directory migration.  
**Tests:** Replacement integration PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 0 Audit

**File:** `docs/audit_baseline.md`  
**Action:** Created  
**Reason:** Complete or harden the 0 Audit scope.  
**Details:** Records original file/module status, 13-suite clean/existing baseline, no-Git provenance, defects and design priorities.  
**Compatibility:** Documentation only.  
**Tests:** Original baseline executed.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch All Evidence

**File:** `docs/backlog.md`  
**Action:** Created  
**Reason:** Complete or harden the All Evidence scope.  
**Details:** Adds mathematical/source-linked model review, dependency revision/availability, original 30 caveats plus discovered issues, explicit milestone gates and actual validation/benchmark evidence.  
**Compatibility:** No invented external or empirical success.  
**Tests:** Checked against tool outputs and final code.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/benchmark_results.json`  
**Action:** Created  
**Reason:** Complete or harden the All Evidence scope.  
**Details:** Adds mathematical/source-linked model review, dependency revision/availability, original 30 caveats plus discovered issues, explicit milestone gates and actual validation/benchmark evidence.  
**Compatibility:** No invented external or empirical success.  
**Tests:** Checked against tool outputs and final code.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/benchmarks.md`  
**Action:** Created  
**Reason:** Complete or harden the All Evidence scope.  
**Details:** Adds mathematical/source-linked model review, dependency revision/availability, original 30 caveats plus discovered issues, explicit milestone gates and actual validation/benchmark evidence.  
**Compatibility:** No invented external or empirical success.  
**Tests:** Checked against tool outputs and final code.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/dependencies.md`  
**Action:** Created  
**Reason:** Complete or harden the All Evidence scope.  
**Details:** Adds mathematical/source-linked model review, dependency revision/availability, original 30 caveats plus discovered issues, explicit milestone gates and actual validation/benchmark evidence.  
**Compatibility:** No invented external or empirical success.  
**Tests:** Checked against tool outputs and final code.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/model_review.md`  
**Action:** Created  
**Reason:** Complete or harden the All Evidence scope.  
**Details:** Adds mathematical/source-linked model review, dependency revision/availability, original 30 caveats plus discovered issues, explicit milestone gates and actual validation/benchmark evidence.  
**Compatibility:** No invented external or empirical success.  
**Tests:** Checked against tool outputs and final code.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `docs/validation.md`  
**Action:** Created  
**Reason:** Complete or harden the All Evidence scope.  
**Details:** Adds mathematical/source-linked model review, dependency revision/availability, original 30 caveats plus discovered issues, explicit milestone gates and actual validation/benchmark evidence.  
**Compatibility:** No invented external or empirical success.  
**Tests:** Checked against tool outputs and final code.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch All Exact handoff

**File:** `docs/engineering_changes.md`  
**Action:** Created  
**Reason:** Complete or harden the All Exact handoff scope.  
**Details:** Adds exhaustive per-file ledger, initial/current file classification and unified source diff. Meta-report files excluded from diff payload to avoid recursive self-inclusion; included in ledger.  
**Compatibility:** Generated build products excluded from source ledger.  
**Tests:** SHA-256 baseline coverage verified.  
**Remaining issues:** Filesystem comparison used because no Git metadata exists.

**File:** `docs/repository_inventory.md`  
**Action:** Created  
**Reason:** Complete or harden the All Exact handoff scope.  
**Details:** Adds exhaustive per-file ledger, initial/current file classification and unified source diff. Meta-report files excluded from diff payload to avoid recursive self-inclusion; included in ledger.  
**Compatibility:** Generated build products excluded from source ledger.  
**Tests:** SHA-256 baseline coverage verified.  
**Remaining issues:** Filesystem comparison used because no Git metadata exists.

**File:** `docs/source_changes.patch`  
**Action:** Created  
**Reason:** Complete or harden the All Exact handoff scope.  
**Details:** Adds exhaustive per-file ledger, initial/current file classification and unified source diff. Meta-report files excluded from diff payload to avoid recursive self-inclusion; included in ledger.  
**Compatibility:** Generated build products excluded from source ledger.  
**Tests:** SHA-256 baseline coverage verified.  
**Remaining issues:** Filesystem comparison used because no Git metadata exists.

## Batch 1 Fixture

**File:** `fixtures/toy_graph/edges.csv`  
**Action:** Created  
**Reason:** Complete or harden the 1 Fixture scope.  
**Details:** Adds versioned directed graph with shuffled rows, parallel arc, time/emission tradeoff and isolated node; preserves v1 backward-compatibility exercise.  
**Compatibility:** Replaces empty single CSV skeleton.  
**Tests:** Engine/CLI/order/concurrency flows PASS.  
**Remaining issues:** Synthetic attributes; v2 geometry exercised in exporter test.

**File:** `fixtures/toy_graph/metadata.txt`  
**Action:** Created  
**Reason:** Complete or harden the 1 Fixture scope.  
**Details:** Adds versioned directed graph with shuffled rows, parallel arc, time/emission tradeoff and isolated node; preserves v1 backward-compatibility exercise.  
**Compatibility:** Replaces empty single CSV skeleton.  
**Tests:** Engine/CLI/order/concurrency flows PASS.  
**Remaining issues:** Synthetic attributes; v2 geometry exercised in exporter test.

**File:** `fixtures/toy_graph/nodes.csv`  
**Action:** Created  
**Reason:** Complete or harden the 1 Fixture scope.  
**Details:** Adds versioned directed graph with shuffled rows, parallel arc, time/emission tradeoff and isolated node; preserves v1 backward-compatibility exercise.  
**Compatibility:** Replaces empty single CSV skeleton.  
**Tests:** Engine/CLI/order/concurrency flows PASS.  
**Remaining issues:** Synthetic attributes; v2 geometry exercised in exporter test.

## Batch 1/2 Fixture

**File:** `fixtures/toy_traffic/factors.csv`  
**Action:** Created  
**Reason:** Complete or harden the 1/2 Fixture scope.  
**Details:** Adds graph-bound timestamped traffic with shuffled IDs and a newer congestion update that changes fastest path.  
**Compatibility:** Replaces empty toy_traffic.csv.  
**Tests:** Dynamic route/cache publication checks PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `fixtures/toy_traffic/metadata.txt`  
**Action:** Created  
**Reason:** Complete or harden the 1/2 Fixture scope.  
**Details:** Adds graph-bound timestamped traffic with shuffled IDs and a newer congestion update that changes fastest path.  
**Compatibility:** Replaces empty toy_traffic.csv.  
**Tests:** Dynamic route/cache publication checks PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `fixtures/toy_traffic_updated/factors.csv`  
**Action:** Created  
**Reason:** Complete or harden the 1/2 Fixture scope.  
**Details:** Adds graph-bound timestamped traffic with shuffled IDs and a newer congestion update that changes fastest path.  
**Compatibility:** Replaces empty toy_traffic.csv.  
**Tests:** Dynamic route/cache publication checks PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `fixtures/toy_traffic_updated/metadata.txt`  
**Action:** Created  
**Reason:** Complete or harden the 1/2 Fixture scope.  
**Details:** Adds graph-bound timestamped traffic with shuffled IDs and a newer congestion update that changes fastest path.  
**Compatibility:** Replaces empty toy_traffic.csv.  
**Tests:** Dynamic route/cache publication checks PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/2 Engine

**File:** `include/ecoroute/engine.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Engine scope.  
**Details:** Implemented move-only Engine initialization, coordinate/external-ID routing, status and traffic publication. Integrates graph/profile/traffic loading, order cache, node index and CCH. Captures one immutable snapshot per request; bounded exact-objective and response LRUs share topology; emits essential unique roles, geometry/metrics, recommendation and error handling.  
**Compatibility:** New public API; object lifetime must outlive concurrent calls.  
**Tests:** EngineTests, CLI/exporter/worker/concurrency/cache tests PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `src/engine.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Engine scope.  
**Details:** Implemented move-only Engine initialization, coordinate/external-ID routing, status and traffic publication. Integrates graph/profile/traffic loading, order cache, node index and CCH. Captures one immutable snapshot per request; bounded exact-objective and response LRUs share topology; emits essential unique roles, geometry/metrics, recommendation and error handling.  
**Compatibility:** New public API; object lifetime must outlive concurrent calls.  
**Tests:** EngineTests, CLI/exporter/worker/concurrency/cache tests PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1 API

**File:** `include/ecoroute/types.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1 API scope.  
**Details:** Added include guard; changed unused max_routes default 5 to product limit 3; clarified advisory detours; added roles/preferences/objective scores, geometry/detour flags, response versions/snapping/warnings and runtime/cache settings.  
**Compatibility:** Intentional default/cap change to three visible routes; structures extended.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 3 Python

**File:** `integration/engine_client.py`  
**Action:** Created  
**Reason:** Complete or harden the 3 Python scope.  
**Details:** Adds standard-library persistent subprocess bridge, validated encoding, serialized requests, JSON/error handling, deadline termination, restart and cleanup.  
**Compatibility:** No shell invocation.  
**Tests:** CLI worker roundtrip PASS; syntax check PASS.  
**Remaining issues:** Restart reloads initial configured traffic; backend must republish current state.

## Batch 3 HTTP

**File:** `integration/fastapi_app.py`  
**Action:** Created  
**Reason:** Complete or harden the 3 HTTP scope.  
**Details:** Adds optional FastAPI/Pydantic2 health/route adapter with strict fields, lifecycle worker and HTTP error mapping.  
**Compatibility:** Optional example, not unseen teammate backend compatibility.  
**Tests:** Syntax check PASS; HTTP/import NOT RUN.  
**Remaining issues:** Required Python packages and teammate backend unavailable.

## Batch 3 Producer

**File:** `scripts/export_snapshot.py`  
**Action:** Created  
**Reason:** Complete or harden the 3 Producer scope.  
**Details:** Adds canonical JSON or optional explicit-query PostGIS exporter, exact IDs, mph conversion, strict finite/geometry validation, v2 count/CRC manifest, staging/flush/atomic rename with cooperative lock and no overwrite. PostGIS uses read-only repeatable-read/server cursors/environment DSN and redacted driver failure details.  
**Compatibility:** New graph v2 producer.  
**Tests:** ExporterTests including real CLI roundtrip PASS.  
**Remaining issues:** Real DB/schema/psycopg absent; staging cleanup/power-loss limits documented.

## Batch 3 gRPC proposal

**File:** `service/routing.proto`  
**Action:** Modified  
**Reason:** Complete or harden the 3 gRPC proposal scope.  
**Details:** Defines proposed Route/Health schema including endpoint variants, roles, metrics, versions, snapping and scoring; explicitly marks server/bindings unimplemented.  
**Compatibility:** Proposal only.  
**Tests:** Schema review; protoc/gRPC NOT RUN.  
**Remaining issues:** Requires teammate transport agreement and dependencies.

## Batch 2 Model

**File:** `src/cost/cost_model.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Model scope.  
**Details:** Exposes validateVehicleProfile for startup validation; enforces EV battery-to-wheel efficiency <=1. Preserves delay-only auxiliary consumption and disjoint gravity credits. Documents uncalibrated and segmentation behavior.  
**Compatibility:** Previously accepted physically invalid EV parameter now rejected.  
**Tests:** CostTests and four-profile engine/runtime checks PASS.  
**Remaining issues:** No empirical calibration; arbitrary segmentation/elevation/state limits in model_review.md.

**File:** `src/cost/cost_model.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Model scope.  
**Details:** Exposes validateVehicleProfile for startup validation; enforces EV battery-to-wheel efficiency <=1. Preserves delay-only auxiliary consumption and disjoint gravity credits. Documents uncalibrated and segmentation behavior.  
**Compatibility:** Previously accepted physically invalid EV parameter now rejected.  
**Tests:** CostTests and four-profile engine/runtime checks PASS.  
**Remaining issues:** No empirical calibration; arbitrary segmentation/elevation/state limits in model_review.md.

## Batch 1/2 Traffic

**File:** `src/cost/traffic_snapshot.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Traffic scope.  
**Details:** Implements immutable traffic metadata/factors, external edge-ID to CSR alignment, strict loader, finite multiplier/upper bounds, explicit missing/free-flow policy, version-text checks and UTC validity validation.  
**Compatibility:** New support API; default missing/stale/invalid rejection.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `src/cost/traffic_snapshot.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Traffic scope.  
**Details:** Implements immutable traffic metadata/factors, external edge-ID to CSR alignment, strict loader, finite multiplier/upper bounds, explicit missing/free-flow policy, version-text checks and UTC validity validation.  
**Compatibility:** New support API; default missing/stale/invalid rejection.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 2 Numerical

**File:** `src/cost/weight_builder.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Numerical scope.  
**Details:** Preserves fixed-scale API and adds graph-safe CchWeightSet. Skips inactive dimensions, uses extended intermediates, preserves zero/tiny-positive weights, bounds simple paths with rounding reserve, reports effective scale/error and rejects nonrepresentable scale/bounds.  
**Compatibility:** Legacy builder stays fixed-scale; Engine uses explicit safe scaling.  
**Tests:** Long-path, finite-bound, error-bound and endpoint tests PASS.  
**Remaining issues:** Quantization/normalization limitations exposed, not hidden.

**File:** `src/cost/weight_builder.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Numerical scope.  
**Details:** Preserves fixed-scale API and adds graph-safe CchWeightSet. Skips inactive dimensions, uses extended intermediates, preserves zero/tiny-positive weights, bounds simple paths with rounding reserve, reports effective scale/error and rejects nonrepresentable scale/bounds.  
**Compatibility:** Legacy builder stays fixed-scale; Engine uses explicit safe scaling.  
**Tests:** Long-path, finite-bound, error-bound and endpoint tests PASS.  
**Remaining issues:** Quantization/normalization limitations exposed, not hidden.

## Batch 0 Cleanup

**File:** `src/graph/graph_io.cpp`  
**Action:** Deleted  
**Reason:** Complete or harden the 0 Cleanup scope.  
**Details:** Deletes empty superseded loader placeholders after reference/build review; graph_loader retained.  
**Compatibility:** No functional implementation removed.  
**Tests:** Clean build/reference scan PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `src/graph/graph_io.hpp`  
**Action:** Deleted  
**Reason:** Complete or harden the 0 Cleanup scope.  
**Details:** Deletes empty superseded loader placeholders after reference/build review; graph_loader retained.  
**Compatibility:** No functional implementation removed.  
**Tests:** Clean build/reference scan PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/3 Graph export

**File:** `src/graph/graph_loader.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/3 Graph export scope.  
**Details:** Preserves v1; adds v2 geometry_wkt 2D LINESTRING, required counts/CRCs over exact parsed bytes, metadata BOM handling, locale-independent finite numeric parsing, malformed data checks and schema comments.  
**Compatibility:** Backward-compatible v1 plus strict v2.  
**Tests:** GraphLoaderTests and actual exporter-to-C++ roundtrip PASS.  
**Remaining issues:** CRC is corruption detection; live PostGIS producer unverified.

**File:** `src/graph/graph_loader.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/3 Graph export scope.  
**Details:** Preserves v1; adds v2 geometry_wkt 2D LINESTRING, required counts/CRCs over exact parsed bytes, metadata BOM handling, locale-independent finite numeric parsing, malformed data checks and schema comments.  
**Compatibility:** Backward-compatible v1 plus strict v2.  
**Tests:** GraphLoaderTests and actual exporter-to-C++ roundtrip PASS.  
**Remaining issues:** CRC is corruption detection; live PostGIS producer unverified.

## Batch 1 Graph

**File:** `src/graph/road_network.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1 Graph scope.  
**Details:** Allows nonempty isolated-node graph with zero edges. Rejects nonempty geometry with fewer than two points or endpoints inconsistent with directed nodes within 1e-6 degrees. Updates contract comments.  
**Compatibility:** Stricter geometry rejection corrects invalid input; isolated-node support added.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `src/graph/road_network.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1 Graph scope.  
**Details:** Allows nonempty isolated-node graph with zero edges. Rejects nonempty geometry with fewer than two points or endpoints inconsistent with directed nodes within 1e-6 degrees. Updates contract comments.  
**Compatibility:** Stricter geometry rejection corrects invalid input; isolated-node support added.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/2 Snapping

**File:** `src/graph/snapper.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Snapping scope.  
**Details:** Implements balanced 3D unit-sphere k-d tree, bounded great-circle search, directional source/target eligibility, deterministic ID ties, antimeridian/pole canonicalization.  
**Compatibility:** New nearest-node v1 implementation.  
**Tests:** SnapperTests and coordinate EngineTests PASS.  
**Remaining issues:** No edge projection, heading, access restrictions or farther-component retry.

**File:** `src/graph/snapper.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Snapping scope.  
**Details:** Implements balanced 3D unit-sphere k-d tree, bounded great-circle search, directional source/target eligibility, deterministic ID ties, antimeridian/pole canonicalization.  
**Compatibility:** New nearest-node v1 implementation.  
**Tests:** SnapperTests and coordinate EngineTests PASS.  
**Remaining issues:** No edge projection, heading, access restrictions or farther-component retry.

## Batch 1 Configuration

**File:** `src/runtime/configuration.cpp`  
**Action:** Created  
**Reason:** Complete or harden the 1 Configuration scope.  
**Details:** Added strict key=value configuration with relative paths, BOM/comments, duplicate/unknown-key and finite/range validation; nine-column vehicle CSV supports four types, optional paired elevation parameters, duplicate/profile count checks and public vehicle validation.  
**Compatibility:** New .conf/CSV implementation replaces empty YAML plans; no YAML parser.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `src/runtime/configuration.hpp`  
**Action:** Created  
**Reason:** Complete or harden the 1 Configuration scope.  
**Details:** Added strict key=value configuration with relative paths, BOM/comments, duplicate/unknown-key and finite/range validation; nine-column vehicle CSV supports four types, optional paired elevation parameters, duplicate/profile count checks and public vehicle validation.  
**Compatibility:** New .conf/CSV implementation replaces empty YAML plans; no YAML parser.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/3 JSON

**File:** `src/runtime/json.cpp`  
**Action:** Created  
**Reason:** Complete or harden the 1/3 JSON scope.  
**Details:** Added escaped JSON status/errors/responses, decimal-string uint64 IDs, explicit GeoJSON lon/lat, Point for self-route, physical metrics, roles and per-objective diagnostics.  
**Compatibility:** New schema_version=1 transport helpers.  
**Tests:** ServiceContractTests and Python parsed roundtrips PASS.  
**Remaining issues:** Consumes validated DTOs; UTF-8 interchange required.

## Batch 1/2 Snapshots

**File:** `src/runtime/snapshot_manager.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Snapshots scope.  
**Details:** Implements private immutable traffic/profile metric bundles, profile/graph validation, CSR preparation, atomic shared ownership publication, old-reader retention and strong failure preservation. Conflicting current version, older or same-time different versions rejected; identical publication idempotent.  
**Compatibility:** Different versions require strictly increasing validity start.  
**Tests:** Runtime/Engine concurrent reads/publication PASS.  
**Remaining issues:** Retired version identifiers must not be reused by producers.

**File:** `src/runtime/snapshot_manager.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Snapshots scope.  
**Details:** Implements private immutable traffic/profile metric bundles, profile/graph validation, CSR preparation, atomic shared ownership publication, old-reader retention and strong failure preservation. Conflicting current version, older or same-time different versions rejected; identical publication idempotent.  
**Compatibility:** Different versions require strictly increasing validity start.  
**Tests:** Runtime/Engine concurrent reads/publication PASS.  
**Remaining issues:** Retired version identifiers must not be reused by producers.

## Batch 1/2 Alternatives

**File:** `src/search/alternatives.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Alternatives scope.  
**Details:** Replaces epsilon preference dedup with exact comparison; associates score/scale/error per objective; extracts evaluator; adds optional ObjectiveQuery runtime cache hook. Uses safe weights and atomic default query. Retains essential routes, exact arc-path dedup, optional dominance/detour filtering and deterministic order.  
**Compatibility:** Existing calls compatible; path.total_cost belongs only to first score.  
**Tests:** AlternativeRoutesTests and EngineTests PASS.  
**Remaining issues:** Finite preferences are not complete Pareto frontier; Engine prefetch off.

**File:** `src/search/alternatives.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Alternatives scope.  
**Details:** Replaces epsilon preference dedup with exact comparison; associates score/scale/error per objective; extracts evaluator; adds optional ObjectiveQuery runtime cache hook. Uses safe weights and atomic default query. Retains essential routes, exact arc-path dedup, optional dominance/detour filtering and deterministic order.  
**Compatibility:** Existing calls compatible; path.total_cost belongs only to first score.  
**Tests:** AlternativeRoutesTests and EngineTests PASS.  
**Remaining issues:** Finite preferences are not complete Pareto frontier; Engine prefetch off.

## Batch 2 CCH

**File:** `src/search/cch_adapter.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 CCH scope.  
**Details:** Preserves RoutingKit CCH, shares immutable topology, adds independent withWeights clones, persistent query/mutex and atomic shortestPathWithWeights. Identical metrics skip customization; replacement prepared before publication. Stable empty-weight sentinel avoids zero-edge UB. Reachability separates overflow from NoRoute; validates reconstruction and translates exceptions.  
**Compatibility:** Existing APIs retained; separate update/query calls are not a combined transaction.  
**Tests:** Random/long/zero-edge/concurrent CCH-Dijkstra checks and benchmarks PASS.  
**Remaining issues:** One adapter serializes query buffers; separate adapters cost memory.

**File:** `src/search/cch_adapter.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 CCH scope.  
**Details:** Preserves RoutingKit CCH, shares immutable topology, adds independent withWeights clones, persistent query/mutex and atomic shortestPathWithWeights. Identical metrics skip customization; replacement prepared before publication. Stable empty-weight sentinel avoids zero-edge UB. Reachability separates overflow from NoRoute; validates reconstruction and translates exceptions.  
**Compatibility:** Existing APIs retained; separate update/query calls are not a combined transaction.  
**Tests:** Random/long/zero-edge/concurrent CCH-Dijkstra checks and benchmarks PASS.  
**Remaining issues:** One adapter serializes query buffers; separate adapters cost memory.

## Batch 1 Evaluation

**File:** `src/search/route_evaluator.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1 Evaluation scope.  
**Details:** Replaces placeholders with shared connectivity, endpoint/arc bounds, finite nonnegative metric and accumulation-overflow validation. Removes local duplication from alternatives.  
**Compatibility:** New common helper used for physical totals.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `src/search/route_evaluator.hpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1 Evaluation scope.  
**Details:** Replaces placeholders with shared connectivity, endpoint/arc bounds, finite nonnegative metric and accumulation-overflow validation. Removes local duplication from alternatives.  
**Compatibility:** New common helper used for physical totals.  
**Tests:** PASS: relevant final Release CTest coverage (20/20 overall).  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 2 Changed-behavior checks

**File:** `tests/test_alternatives.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Changed-behavior checks scope.  
**Details:** Retains original checks and adds random/updated CCH-Dijkstra comparisons, zero-edge/overflow/concurrency, safe-scale/error bounds, exact endpoint-neighbor preferences and evaluator failures.  
**Compatibility:** No relaxed costs or algorithm substitution.  
**Tests:** All relevant suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_cch_adapter.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Changed-behavior checks scope.  
**Details:** Retains original checks and adds random/updated CCH-Dijkstra comparisons, zero-edge/overflow/concurrency, safe-scale/error bounds, exact endpoint-neighbor preferences and evaluator failures.  
**Compatibility:** No relaxed costs or algorithm substitution.  
**Tests:** All relevant suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_weight_builder.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 2 Changed-behavior checks scope.  
**Details:** Retains original checks and adds random/updated CCH-Dijkstra comparisons, zero-edge/overflow/concurrency, safe-scale/error bounds, exact endpoint-neighbor preferences and evaluator failures.  
**Compatibility:** No relaxed costs or algorithm substitution.  
**Tests:** All relevant suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/3 Integration checks

**File:** `tests/test_cli.py`  
**Action:** Created  
**Reason:** Complete or harden the 1/3 Integration checks scope.  
**Details:** Adds actual public routes/metrics/profiles/errors/exactness/concurrency, JSON escaping/IDs/lon-lat, executable order/worker updates and exporter integrity through C++. Service test covers implemented JSON, not gRPC.  
**Compatibility:** New integration acceptance.  
**Tests:** All four suites PASS; sandbox exporter ACL failure resolved by outside-sandbox execution.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_engine.cpp`  
**Action:** Created  
**Reason:** Complete or harden the 1/3 Integration checks scope.  
**Details:** Adds actual public routes/metrics/profiles/errors/exactness/concurrency, JSON escaping/IDs/lon-lat, executable order/worker updates and exporter integrity through C++. Service test covers implemented JSON, not gRPC.  
**Compatibility:** New integration acceptance.  
**Tests:** All four suites PASS; sandbox exporter ACL failure resolved by outside-sandbox execution.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_export_snapshot.py`  
**Action:** Created  
**Reason:** Complete or harden the 1/3 Integration checks scope.  
**Details:** Adds actual public routes/metrics/profiles/errors/exactness/concurrency, JSON escaping/IDs/lon-lat, executable order/worker updates and exporter integrity through C++. Service test covers implemented JSON, not gRPC.  
**Compatibility:** New integration acceptance.  
**Tests:** All four suites PASS; sandbox exporter ACL failure resolved by outside-sandbox execution.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_service.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/3 Integration checks scope.  
**Details:** Adds actual public routes/metrics/profiles/errors/exactness/concurrency, JSON escaping/IDs/lon-lat, executable order/worker updates and exporter integrity through C++. Service test covers implemented JSON, not gRPC.  
**Compatibility:** New integration acceptance.  
**Tests:** All four suites PASS; sandbox exporter ACL failure resolved by outside-sandbox execution.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/2 Changed-behavior checks

**File:** `tests/test_cost.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Changed-behavior checks scope.  
**Details:** Retains existing tests; adds isolated/directed geometry, v2/BOM/count/CRC, EV bound and uniform/nonuniform splitting/unit checks.  
**Compatibility:** No polishing of already-passing tests.  
**Tests:** Graph/GraphLoader/Cost suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_graph.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Changed-behavior checks scope.  
**Details:** Retains existing tests; adds isolated/directed geometry, v2/BOM/count/CRC, EV bound and uniform/nonuniform splitting/unit checks.  
**Compatibility:** No polishing of already-passing tests.  
**Tests:** Graph/GraphLoader/Cost suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_graph_loader.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 Changed-behavior checks scope.  
**Details:** Retains existing tests; adds isolated/directed geometry, v2/BOM/count/CRC, EV bound and uniform/nonuniform splitting/unit checks.  
**Compatibility:** No polishing of already-passing tests.  
**Tests:** Graph/GraphLoader/Cost suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1/2 New-module checks

**File:** `tests/test_runtime.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1/2 New-module checks scope.  
**Details:** Adds focused alignment/validity/policy, spherical indexed snapping, per-profile immutable bundle and failed/idempotent/conflicting/same-time/concurrent publication acceptance.  
**Compatibility:** Runtime test replaces empty placeholder.  
**Tests:** All three suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_snapper.cpp`  
**Action:** Created  
**Reason:** Complete or harden the 1/2 New-module checks scope.  
**Details:** Adds focused alignment/validity/policy, spherical indexed snapping, per-profile immutable bundle and failed/idempotent/conflicting/same-time/concurrent publication acceptance.  
**Compatibility:** Runtime test replaces empty placeholder.  
**Tests:** All three suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

**File:** `tests/test_traffic_snapshot.cpp`  
**Action:** Created  
**Reason:** Complete or harden the 1/2 New-module checks scope.  
**Details:** Adds focused alignment/validity/policy, spherical indexed snapping, per-profile immutable bundle and failed/idempotent/conflicting/same-time/concurrent publication acceptance.  
**Compatibility:** Runtime test replaces empty placeholder.  
**Tests:** All three suites PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.

## Batch 1 Changed-default check

**File:** `tests/test_types.cpp`  
**Action:** Modified  
**Reason:** Complete or harden the 1 Changed-default check scope.  
**Details:** Changes only default route count expectation from five to three according to product requirement.  
**Compatibility:** Not an arbitrary passing-value adjustment.  
**Tests:** TypesTests PASS.  
**Remaining issues:** Documented v1 limitations and external blockers remain in backlog.md.
