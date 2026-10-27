# EcoRoute — integrated evaluation prototype

This self-contained ZIP includes the pinned RoutingKit **source files** under `routing-engine/third_party/RoutingKit/`. No Git submodule checkout is required when running the ZIP directly. The original GitHub integration branch manages RoutingKit as a submodule.

This repository combines the team's React/Mapbox frontend, FastAPI backend, ONNX congestion model, PostGIS network, Redis cache, and the independently tested C++17 RoutingKit CCH engine.

## Quick start (Docker Desktop)

1. Install Docker Desktop with the Linux container engine and Docker Compose v2.
2. Copy `.env.example` to `.env`. A working Mapbox public token is required to display the map; the frontend also retains the team's earlier public demo token as a fallback.
3. From the repository root:

   ```bash
   docker compose up --build -d
   docker compose ps
   ```

4. Open **http://localhost:5173**. API health: **http://localhost:8000/health**.
5. If PostGIS is empty, the site clearly indicates **Synthetic demonstration network**. Select **Use demo points**, then **Find routes**. CCH calculates the paths in the C++ engine; they are not mocked HTTP responses.
6. To watch ML traffic publication and routing health:

   ```bash
   docker compose logs -f routing ml_worker api
   ```

7. Stop containers with `docker compose down`. **Do not use `down -v`** unless you intend to erase databases and Redis volumes.

Detailed usage, data ingestion, remote access, troubleshooting, run verification and known limitations: **[docs/PROTOTYPE_RUN_GUIDE.md](docs/PROTOTYPE_RUN_GUIDE.md)**.

## Real road data

On first boot, if `nodes`/`edges` are empty, an explicitly **synthetic demo** snapshot is used. To ingest real Pune OSM streets (internet/Overpass access required):

```bash
docker compose --profile ingest run --rm map_ingest
# Restart the routing service so it exports the newly ingested graph;
# its new traffic version will then be published by the ML worker.
docker compose restart routing ml_worker
```

Note that default ingestion is **Shivaji Nagar, Pune**, not NYC. The synthetic network is not real road guidance. Real OSM routing still lacks turn restrictions and legal driving-access modeling.

## How the complete pipeline works

1. `scripts/integration/prepare_graph.py` reads the teammate PostGIS schema and exports validated, immutable schema-v2 snapshots; when no rows exist it creates an explicitly synthetic demo graph.
2. `ecoroute_preprocess` prepares a reusable CCH node order. The `routing` container starts a bounded pool of persistent C++ CLI workers behind a **real Protobuf/gRPC gateway**.
3. `app/ml_service.py` calculates ONNX congestion factors and atomically publishes a complete versioned batch to Redis every 300 seconds (configurable).
4. `redis_traffic_adapter.py` validates edge IDs, graph version, full coverage and factors, then publishes an immutable traffic snapshot to each engine worker. CCH **customizes weights**, not topology.
5. React sends an exact slider preference to FastAPI `/api/v1/route`. FastAPI asks the routing gRPC gateway; CCH returns up to three distinct real paths (fastest, selected, eco).
6. Redis stores version-keyed responses. Up to two neighboring preferences are prefetched into the cache asynchronously; the UI always displays at most three unique results.
7. Every C++ result includes physical metrics, GeoJSON geometry and caveats about uncalibrated emissions and missing turn restrictions.

**Transport clarification:** gRPC is currently provided by a Python process holding a pool of persistent C++ workers. It is not a C++-native gRPC server. The actual wire schema is `routing-engine/service/gateway.proto` using `google.protobuf.Struct`; `routing.proto` remains a typed contract proposal.

## Verification

The C++ engine was built and all **20/20 registered CTest suites passed** in the available Linux environment. Local integration tests exercised FastAPI → gRPC → C++ on a synthetic graph, concurrency, periodic traffic updates, all four vehicle profiles, invalid coordinates/preferences, and worker recovery. Docker Compose / PostGIS ingestion / the frontend build require networked external dependencies and must still be verified on the evaluation machine. **See [docs/INTEGRATION_REPORT.md](docs/INTEGRATION_REPORT.md)** for what was tested and not tested.
