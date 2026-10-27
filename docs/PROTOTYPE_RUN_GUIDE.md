# EcoRoute integrated prototype — run guide

## Requirements

- Docker Desktop running Linux containers, Docker Compose v2, approximately 4+ GB free RAM for the first image builds.
- A reliable internet connection for first-run Python/npm packages, container images, Mapbox tiles/geocoding, optional OSMnx ingestion, and optional Open-Meteo weather.
- A Mapbox **public browser** access token: set `VITE_MAPBOX_TOKEN` in `.env`. An earlier team public token is retained in the frontend as a fallback, but it can expire or be restricted.
- Ports **5173** (website), **8000** (API), **5432** (PostGIS), **6379** (Redis) must be available, or change the exposed ports in Compose. PostGIS and Redis should not be publicly exposed.

## Start a complete demo without a pre-existing city database

From the unzipped project root (not inside `routing-engine/`):

```powershell
Copy-Item .env.example .env
# Optionally edit .env to set VITE_MAPBOX_TOKEN
# Docker Desktop must already be running.
docker compose up --build -d
docker compose ps
docker compose logs --tail=100 routing api ml_worker frontend
```

Linux/macOS: use `cp .env.example .env` instead.

The **routing container must become healthy**. It exports a PostGIS graph if one exists; otherwise it generates a clearly labeled **81-node, 288-directed-edge synthetic Pune grid**, builds a CCH order, then starts two workers. The `ml_worker` loads the team's ONNX model and emits versioned Redis batches.

Visit `http://localhost:5173`, click **Use demo points** followed by **Find routes**. You should see a real computed route from the C++ engine, up to three unique paths, and time/distance/CO₂e/fuel/electricity estimates. Move the slider to make another exact-preference request; Redis may serve a version-matched cached result. Nearby slider preferences are warmed in the background.

Check the API independently:

```powershell
Invoke-RestMethod http://localhost:8000/health
$body = @{start_lon=73.844;start_lat=18.513;end_lon=73.868;end_lat=18.537;vehicle_type='petrol';preference=0.5} | ConvertTo-Json
Invoke-RestMethod -Uri http://localhost:8000/api/v1/route -Method Post -ContentType 'application/json' -Body $body
```

You should see `graph_version`, `traffic_version`, `recommended_route_id`, `routes`, geometry, physical metrics and warnings. The `graph_version` beginning with `synthetic-demo-` signifies non-real roads.

Other API cases: vehicle `'diesel'`, `'hybrid'` and `'electric'`; `preference` from `0` (lowest CO₂e) to `1` (fastest). Invalid input gets HTTP 422; disconnected/out-of-coverage requests return explicit errors, **never fabricated paths**.

## Load actual Pune roads from OpenStreetMap

The repository includes an **optional** OSMnx importer in the team's code, mapped to their existing PostGIS schema. This is a real external network download and may fail if Overpass is unavailable. Configure `.env`:

```dotenv
INGEST_ADDRESS=Shivaji Nagar, Pune, India
INGEST_RADIUS_METERS=2000
```

Then run once:

```powershell
docker compose --profile ingest run --rm map_ingest
```

Verify records in PostGIS:

```powershell
docker compose exec database psql -U ecoroute -d ecoroute -c "SELECT (SELECT count(*) FROM nodes) AS nodes, (SELECT count(*) FROM edges) AS edges;"
```

Use the user/database specified by your `.env` if different. Existing data is preserved; **avoid repeated imports of the same area without cleaning the tables**, since parallel duplicate segments can accumulate.

Once real rows exist:

```powershell
docker compose restart routing ml_worker
Invoke-RestMethod http://localhost:8000/health
```

Graph source should now be `postgis`, and route snapping and map geometry should operate on the imported roads. If the importer and exporter disagree on data (edge IDs, types, directed geometry), the routing container fails clearly instead of silently using a corrupted graph. For larger data, graph preprocessing and worker memory may increase substantially. Do not treat this as production-legal road navigation: traffic estimates are approximate; turn restrictions, road legal access and validated real fuel/CO₂e calibration are not implemented.

## All components and their contracts

| Container | Role | Integration |
|---|---|---|
| `database` | PostgreSQL 15 + PostGIS | `nodes`, `edges` from teammate schema |
| `redis` | ML batches and versioned route-result cache | `traffic:batch:<version>` + `traffic:latest`, `route:v2:<hash>` |
| `routing` | Immutable graph, CCH preprocessing, persistent C++ workers, gRPC gateway, Redis reader | Listens internally on port 50051 |
| `api` | Browser HTTP/JSON, versioned route-result caching, bounded neighboring preference prefetch | Calls gRPC `routing:50051` |
| `ml_worker` | Loads original ONNX predictor and weather data, generates 5-minute batches | Publishes Redis batches atomically |
| `frontend` | React + Mapbox UI, via Vite `/api` proxy | Accessible at host:5173 |
| `map_ingest` (optional profile) | OSMnx street-network import | PostGIS, run once deliberately |

**Browser cross-device use:** Open `http://<host-LAN-IP>:5173` from a device on the same permitted LAN, ensuring port 5173 is reachable through the host firewall. The frontend uses the Vite `/api` proxy, **not `localhost:8000` on the visitor's PC**. For an internet-facing deployment use HTTPS, authenticated/admin-protected operations, restricted credentials and firewall rules, and a production reverse proxy; this compose configuration is for local/LAN demonstration.

**Traffic behavior:** Predictions are recalculated every `ML_INTERVAL_SECONDS` (default 300). The new batch is committed to Redis with an immutable version and a latest pointer published last, then pulled by the gateway every two seconds. A graph-version mismatch or incomplete batch is rejected. The C++ engine retains the previous snapshot if an update fails, and routes in progress use their captured snapshot. This is not a promise of true real-time measured traffic; it is ONNX-based prediction, currently refreshed periodically.

## Tests and debugging

```powershell
docker compose ps
docker compose logs -f routing api ml_worker
docker compose logs --tail=100 frontend
docker compose exec redis redis-cli GET traffic:latest
docker compose exec redis redis-cli --scan --pattern 'route:v2:*'
```

The C++ engine may also be built directly on Windows using `routing-engine/docs/windows_setup.md`, or on Linux:

```bash
cmake -S routing-engine -B routing-engine/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build routing-engine/build/release --parallel 4
ctest --test-dir routing-engine/build/release --output-on-failure
```

The `tests/test_integrated_pipeline.py` Python smoke test additionally exercises the real C++ process, versioned traffic updates, multiple profiles, 12 concurrent local callers and process recovery. Run with a built CLI, or set `ECOROUTE_TEST_CLI=/absolute/path/to/ecoroute_cli`.

Troubleshooting:

- **`/health` unavailable:** `docker compose logs routing`; inspect PostGIS schema, snapshot validation, compiler build or graph preprocessing failures.
- **Map blank:** configure a working Mapbox token and internet access; verify browser console for Mapbox errors.
- **No route/out of coverage:** select locations inside the loaded graph, or use demo points while on synthetic graph. Node snap distance is deliberately bounded.
- **Redis unavailable:** the API can bypass result caching, but ML refresh requires Redis. Routing starts with explicitly configured free-flow estimates.
- **Traffic remains `explicit-free-flow`:** check `docker compose logs ml_worker`, ONNX model compatibility, batch freshness, and `traffic:latest` Redis key.
- **Graph remains synthetic after ingest:** restart routing/ML, check DB row counts and `routing` logs. The graph is intentionally loaded once at service startup.
- **Dependency/pip/npm failure:** first builds need external package registries; ensure Docker has network access.

Stop with `docker compose down` and preserve volumes. Only `docker compose down -v` deletes saved database/Redis data.
