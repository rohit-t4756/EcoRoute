# Integration change report and verification matrix

**Source baselines:** Teammates' `running-branch` at commit `c0c2701`; routing engine from `feat/routing-engine-integration` committed at `96d3f43`. No Git history or working team branch was changed by this package.

## Main implementation changes

| Area | Files | What changed |
|---|---|---|
| C++ engine | `routing-engine/src/cost/cost_model.cpp` | Conservative road-category fallback speeds only if the DB lacks a valid speed limit; explicit speed always wins. Estimates are not calibrated. |
| C++ engine | `src/search/alternatives.cpp/.hpp`, `src/engine.cpp` | Engine-bound immutable CCH graph bypasses redundant full-graph comparison in request path, public generator still checks by default. |
| C++ cleanup | `include/ecoroute/types.hpp`, `src/search/alternatives.cpp` | Duplicate directive and typo cleanup; core hierarchy and shortest-path algorithms retained. |
| Graph integration | `scripts/integration/prepare_graph.py` | Verify actual `nodes`/`edges` PostGIS schema and export a v2 immutable graph snapshot; explicit synthetic demo fallback when DB empty; precompute CCH order once. |
| Traffic integration | `routing-engine/integration/redis_traffic_adapter.py` | Validated, version-atomic full traffic snapshots with exact directed-edge IDs and graph versions. |
| ML teammate | `app/ml_service.py` | Preserve original ONNX prediction/weather code; use Pune timezone, honor configurable 300-second refresh, publish complete immutable batches and legacy per-edge keys; demo-mode edge attributes from graph snapshot. |
| gRPC gateway | `routing-engine/integration/grpc_server.py`, `grpc_transport.py`, `service/gateway.proto` | Actual Protobuf Struct gRPC service between FastAPI and persistent C++ worker pool. Existing `service/routing.proto` is retained as a typed future proposal; C++ engine itself is not a native gRPC server. |
| C++ process bridge | `routing-engine/integration/engine_client.py` | Missing executable now returns structured `EngineError`; a restarted worker replays its last successful traffic snapshot. |
| API teammate | `app/main.py`, `app/models.py` | Replace fake route with true gRPC routing, exact preference, all four vehicle profiles, structured errors, Redis version-keyed route cache, bounded two-neighbor cache prefetch. |
| Frontend teammate | `app/frontend/src/App.jsx`, `vite.config.js`, `App.css` | Interpret real `routes` array and recommended ID; draw fastest, selected and lowest-emission paths, show physical metrics/errors; slider queries exact preference; click or geocoder endpoints; proxy API for other PCs. |
| PostGIS ingest teammate | `scripts/database/ingest_map.py` | Remove embedded DB credential; preserve OSM road categories, road surface and mph/kmh speed conversions; allow environment-selectable ingestion area. |
| Deployment | `docker-compose.yml`, `Dockerfile.routing`, `Dockerfile.api`, `Dockerfile.ml`, `Dockerfile.ingest`, requirements subsets, `.env.example`, `.dockerignore` | Standalone working service boundary, pinned RoutingKit compilation in Docker, health checks and reproducible configuration. |
| Cleanup | `.ingest_temp.py` removed; `.gitignore` no longer ignores all tests | Temporary ingestion hack removed; integration tests can be versioned. |

## Verified in the available Linux container

- C++ RoutingKit + engine compiled successfully on GCC 14.2 with CMake/Ninja.
- **20 of 20** registered CTest suites passed **after** the core cost-model and graph-validation optimization changes.
- Python `compileall` syntax checks passed for the integration modules.
- Graph exporter produced an integrity-checked schema-v2 synthetic Pune graph, and C++ successfully initialized and routed on it.
- Local gRPC gateway with **two persistent actual C++ subprocess workers** responded to Health, Route and UpdateTraffic.
- FastAPI TestClient sent actual gRPC requests and received real paths for petrol, diesel, hybrid and electric, with HTTP 422 for invalid preferences and out-of-coverage endpoints.
- **20 concurrent gRPC requests** completed and used the correct traffic version.
- Redis traffic-batch logic tested using a stub implementing `get` and `hgetall`; incomplete batches were rejected; workers recovered their last traffic update after termination.
- The optional neighbor cache is bounded in the Python process; real Redis connectivity still requires running the Docker services.

## Not independently verified here — do not misrepresent as complete

- **Full Docker Compose startup** (this environment does not provide the Docker engine or networked image/package registry access).
- **Real PostGIS live query/export** and OSMnx ingestion: no live PostGIS/OSMnx/Overpass access in the validation container. The SQL/schema mapping is implemented, but must be tested against your live DB.
- **Original ONNX inference** and periodic Redis publication against actual containers: ONNX Runtime and Redis clients were not available in the offline validation environment. The original predictor is reused, not replaced with fake production predictions.
- **Frontend npm build and Mapbox tiles:** npm packages were absent and downloads blocked by offline networking; Mapbox access requires a valid external token.
- **City-scale performance, correctness, legal turn restrictions, live energy/emissions calibration, and production load testing:** pending.

## Important product boundaries

1. If PostGIS is empty, a clearly labeled synthetic demo grid is used. These are actual CCH-computed paths on synthetic edges, **not real navigable streets**.
2. Energy/CO₂e are model estimates; country-specific fuel/grid calibrations and road legal access are not yet validated.
3. Up to three unique displayed routes, not five cards. Two neighboring slider preferences are asynchronously prefetched as separate versioned Redis cache entries when enabled. They might select duplicate physical paths.
4. Graphs are immutable for a service lifetime. Adding roads requires restarting the routing service and its ML publisher.
5. The current gRPC boundary is Python FastAPI → Python gRPC gateway → C++ subprocess pool; a native C++ gRPC server has **not** been claimed or implemented.
6. Demo credentials and open CORS/network settings are for local evaluation, not public production use.
