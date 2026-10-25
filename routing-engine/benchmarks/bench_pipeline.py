"""Compare public worker startup/cold routing/warm routing with and without caches.
Usage: python benchmarks/bench_pipeline.py CLI CONFIG [ITERATIONS]
The bundled fixture uses nodes 1->4 and petrol_demo; this is a fixture benchmark.
"""
import json
import pathlib
import statistics
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "integration"))
from engine_client import EngineProcess

exe, configuration = map(pathlib.Path, sys.argv[1:3])
iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 200
if not 10 <= iterations <= 100000:
    raise ValueError("iterations must be 10..100000")
original = {}
for line in configuration.read_text().splitlines():
    if line.strip() and not line.lstrip().startswith("#"):
        key, value = line.split("=", 1)
        original[key.strip()] = value.strip()
for key in ("graph_file", "traffic_file", "vehicle_profiles_file", "cch_topology_file"):
    if original.get(key):
        original[key] = str((configuration.parent / original[key]).resolve())
request = dict(source_node_id="1", target_node_id="4", vehicle_profile_id="petrol_demo",
               preference=0.5, departure_time_unix_seconds=1800000000)
results = {}
with tempfile.TemporaryDirectory(dir=exe.resolve().parent) as directory:
    for mode, cap in (("disabled", "0"), ("enabled", "6")):
        options = {**original, "max_cached_objectives": cap, "max_cached_routes": "64" if cap != "0" else "0"}
        path = pathlib.Path(directory) / (mode + ".conf")
        path.write_text("".join(f"{key}={value}\n" for key, value in options.items()))
        with EngineProcess(exe, path) as client:
            started = time.perf_counter()
            client.health()
            startup_ms = (time.perf_counter() - started) * 1000
            started = time.perf_counter()
            first = client.route(**request)
            cold_ms = (time.perf_counter() - started) * 1000
            durations = []
            for _ in range(iterations):
                started = time.perf_counter()
                response = client.route(**request)
                durations.append((time.perf_counter() - started) * 1000)
                assert response == first
            results[mode] = {"startup_ms": startup_ms, "cold_route_ms": cold_ms,
                             "warm_mean_ms": statistics.mean(durations),
                             "warm_p95_ms": sorted(durations)[int(iterations * 0.95) - 1]}
print(json.dumps({"dataset": "synthetic-five-node-fixture", "iterations": iterations,
                  "includes": "Python pipe, JSON serialization/parsing and public Engine", **results}, indent=2))