# EcoRoute routing engine

A C++17 routing module using **RoutingKit Customizable Contraction Hierarchies**
with inertial-flow nested-dissection ordering. The engine loads versioned directed
road snapshots, applies vehicle and traffic models, and returns at most three
unique fastest / exact preference / lowest-emissions routes.

The public engine, persistent CLI worker, traffic publication and bounded caches
are implemented. Energy/CO2e are **uncalibrated estimates**. Node snapping is v1;
turn restrictions and legal-access data are not represented. See
[architecture](docs/architecture.md), [data contract](docs/data_contract.md),
[API](docs/api.md), [backlog](docs/backlog.md), [validation](docs/validation.md)
and the [file-by-file change ledger](docs/engineering_changes.md).

## Build and run

Use CMake >=3.24, Ninja, a C++17 compiler, and a matching built RoutingKit library
with ZLIB, OpenMP and Threads. Supply the dependency prefix; no personal path is
hardcoded in CMake. The tested host is Windows / MSYS2 UCRT64 GCC 16.2.0.

```powershell
cmake --preset release -DECOROUTE_ROUTINGKIT_ROOT=C:/path/to/RoutingKit
cmake --build --preset release -j 4
ctest --preset release
build/release/ecoroute_cli --config config/engine.example.conf --source 1 --target 4 --vehicle petrol_demo --preference 0.5 --departure 1800000000
```

On Linux use the same commands and paths for your installation. Linux/MSVC have
not been executed in this audit. For the executed dependency revision and local
build flags, see [dependencies](docs/dependencies.md).

The demonstration returns two physical paths: 72 seconds / 2,000 metres and
180 seconds / 1,000 metres. These are synthetic fixture attributes, not surveyed
distances or calibrated vehicle specifications. Profiles ending in `_demo` are
arithmetic examples, not manufacturer data. Fixture traffic is valid for the
explicit demonstration timestamp.

```powershell
build/release/ecoroute_cli --config config/engine.example.conf --health
build/release/ecoroute_cli --config config/engine.example.conf --serve
build/release/ecoroute_preprocess fixtures/toy_graph build/toy.order
build/release/ecoroute_bench 150 2000 4
```

The preprocessor saves a validated nested-dissection **order**, not a binary
hierarchy. Add `cch_topology_file=../build/toy.order` to a configuration to reuse
it; hierarchy construction still runs once per engine.

Configuration uses strict `key=value` and profile CSV formats. Unknown/duplicate
keys and invalid values fail startup. Relative paths resolve against the config
file. The previous empty YAML placeholders were replaced by runnable examples;
there is no YAML parser.

## Integration

- `scripts/export_snapshot.py`: atomic graph v2 exporter from canonical JSON,
  or optional PostGIS queries in one read-only repeatable-read transaction.
- `integration/engine_client.py`: standard-library Python client for a persistent
  C++ worker, request serialization, timeouts and worker cleanup.
- `integration/fastapi_app.py`: optional FastAPI example exposing
  `POST /api/v1/route` and `GET /health`.
- `service/routing.proto`: proposed contract for future teammate agreement.
  No gRPC server is built or claimed functional.

Live PostGIS, ML, Redis, FastAPI and React teammate integration remains unverified:
those repositories, services and optional Python packages were not provided.
The local exporter → C++ → Python contract is executable and tested.